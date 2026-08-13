"""AG Player voice-clone Worker protocol. Model code stays in Adapter Workers."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import socket
import stat
import sys
import threading
import uuid

MAX_FRAME_BYTES = 1024 * 1024
OPERATIONS = {"hello", "capabilities", "load", "generate", "cancel", "unload", "shutdown"}


class WorkerError(Exception):
    def __init__(self, code: str, message: str, retryable: bool = False, details: dict | None = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.retryable = retryable
        self.details = details or {}


class CancellationToken:
    def __init__(self) -> None:
        self._event = threading.Event()

    def cancel(self) -> None:
        self._event.set()

    def raise_if_cancelled(self) -> None:
        if self._event.is_set():
            raise WorkerError("CANCELLED", "The request was cancelled")


class _ContractEngine:
    """Framework-free engine used only by the real socket contract harness."""

    def __init__(self, delegate) -> None:
        self._delegate = delegate

    def validate_generation(self, payload: dict, parameters: dict) -> None:
        self._delegate.validate_generation(payload, parameters)

    def load(self, model_root: pathlib.Path, parameters: dict, token: CancellationToken) -> None:
        token.raise_if_cancelled()

    def generate(self, text, reference, output, parameters, token, progress, request_id) -> None:
        if text != "__contract_long_running__":
            raise WorkerError("CONTRACT_TEST_MODE", "Generation is disabled in contract-test mode")
        progress("generate", request_id, "contract-running", 0.1)
        try:
            with output.open("wb") as stream:
                stream.write(b"RIFF-contract")
                stream.flush()
                while True:
                    token.raise_if_cancelled()
                    stream.write(b".")
                    stream.flush()
                    threading.Event().wait(0.02)
        finally:
            output.unlink(missing_ok=True)

    def unload(self) -> None:
        return None


def _is_link(path: pathlib.Path) -> bool:
    if path.is_symlink():
        return True
    is_junction = getattr(os.path, "isjunction", None)
    if is_junction and is_junction(path):
        return True
    try:
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
        return bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))
    except FileNotFoundError:
        return False


def _reject_link_ancestors(path: pathlib.Path) -> None:
    current = path
    while True:
        if current.exists() and _is_link(current):
            raise WorkerError("UNSAFE_PATH", "Links and reparse points are not allowed")
        if current.parent == current:
            return
        current = current.parent


def existing_directory(value: str, label: str) -> pathlib.Path:
    path = pathlib.Path(value).expanduser()
    if not path.is_absolute() or not path.exists() or not path.is_dir():
        raise WorkerError("INVALID_PATH", f"{label} must be an existing absolute directory")
    _reject_link_ancestors(path)
    return path.resolve(strict=True)


def existing_file(value: str, label: str) -> pathlib.Path:
    path = pathlib.Path(value).expanduser()
    if not path.is_absolute() or not path.exists() or not path.is_file():
        raise WorkerError("INVALID_PATH", f"{label} must be an existing absolute file")
    _reject_link_ancestors(path)
    return path.resolve(strict=True)


def safe_output_path(output_root: pathlib.Path, relative: str) -> pathlib.Path:
    candidate_text = relative.strip()
    candidate = pathlib.PurePath(candidate_text)
    if not candidate_text or candidate.is_absolute() or candidate.drive or ".." in candidate.parts:
        raise WorkerError("INVALID_PATH", "outputPath must be relative to outputRoot")
    target = (output_root / candidate).resolve(strict=False)
    try:
        target.relative_to(output_root)
    except ValueError as error:
        raise WorkerError("INVALID_PATH", "outputPath escapes outputRoot") from error
    _reject_link_ancestors(target.parent)
    target.parent.mkdir(parents=True, exist_ok=True)
    return target


def temporary_output_path(target: pathlib.Path) -> pathlib.Path:
    return target.with_name(f".{target.name}-{uuid.uuid4().hex}.tmp.wav")


def with_defaults(schema: dict, values: dict) -> dict:
    result = {item["key"]: item["default"] for item in schema["parameters"]}
    result.update(values)
    return result


def validate_parameters(schema: dict, values: object, allow_hidden_defaults: bool = False) -> dict:
    if not isinstance(values, dict):
        raise WorkerError("INVALID_PARAMETERS", "parameters must be an object")
    controls = {item["key"]: item for item in schema["parameters"]}
    unknown = sorted(set(values) - set(controls))
    if unknown:
        raise WorkerError("INVALID_PARAMETERS", f"Unknown parameter: {unknown[0]}")
    merged = with_defaults(schema, values)
    for key, control in controls.items():
        visible_when = control.get("visibleWhen")
        visible = not visible_when or merged.get(visible_when["key"]) == visible_when["equals"]
        hidden_default = (
            allow_hidden_defaults
            and key in values
            and type(values[key]) is type(control["default"])
            and values[key] == control["default"]
        )
        if not visible and key in values and not hidden_default:
            raise WorkerError("INVALID_PARAMETERS", f"{key} is not available in the selected mode")
        if not visible:
            continue
        value = merged[key]
        kind = control["type"]
        valid = {
            "bool": lambda item: isinstance(item, bool),
            "enum": lambda item: isinstance(item, str) and item in control["options"],
            "int": lambda item: isinstance(item, int) and not isinstance(item, bool),
            "double": lambda item: isinstance(item, (int, float)) and not isinstance(item, bool),
            "string": lambda item: isinstance(item, str),
            "file": lambda item: isinstance(item, str),
        }[kind](value)
        if not valid:
            raise WorkerError("INVALID_PARAMETERS", f"Invalid value for {key}")
        if kind in {"int", "double"}:
            if "minimum" in control and value < control["minimum"]:
                raise WorkerError("INVALID_PARAMETERS", f"{key} is below its minimum")
            if "maximum" in control and value > control["maximum"]:
                raise WorkerError("INVALID_PARAMETERS", f"{key} is above its maximum")
        if kind in {"string", "file"} and len(value) > control.get("maximumLength", len(value)):
            raise WorkerError("INVALID_PARAMETERS", f"{key} is too long")
        if control.get("required") and (value == "" or value is None):
            raise WorkerError("INVALID_PARAMETERS", f"{key} is required")
    return merged


class _Transport:
    def __init__(self, socket_name: str):
        if os.name == "nt":
            import ctypes
            import msvcrt

            self._reader = open("\\\\.\\pipe\\" + socket_name, "r+b", buffering=0)
            self._writer = os.fdopen(os.dup(self._reader.fileno()), "wb", buffering=0)
            self._pipe_handle = msvcrt.get_osfhandle(self._reader.fileno())
            self._kernel32 = ctypes.windll.kernel32
            self._socket = None
        else:
            self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._socket.connect(socket_name)
            self._reader = self._socket.makefile("rb", buffering=0)
            self._writer = self._socket.makefile("wb", buffering=0)
        self._write_lock = threading.Lock()

    def read(self, size: int) -> bytes:
        if os.name == "nt":
            import ctypes

            available = ctypes.c_ulong()
            while True:
                if not self._kernel32.PeekNamedPipe(
                    self._pipe_handle, None, 0, None, ctypes.byref(available), None
                ):
                    return b""
                if available.value:
                    return os.read(self._reader.fileno(), min(size, available.value))
                threading.Event().wait(0.01)
        return self._reader.read(size)

    def write_json(self, message: dict) -> None:
        frame = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
        with self._write_lock:
            self._writer.write(frame)

    def close(self) -> None:
        self._reader.close()
        self._writer.close()
        if self._socket is not None:
            self._socket.close()


class WorkerServer:
    def __init__(self, args: argparse.Namespace, engine, schema: dict):
        self.args = args
        self.engine = engine
        self.schema = schema
        self.transport = _Transport(args.socket)
        self.model_root = existing_directory(args.model_root, "modelRoot")
        self.output_root = existing_directory(args.output_root, "outputRoot")
        self._active: dict[str, CancellationToken] = {}
        self._active_lock = threading.Lock()
        self._active_changed = threading.Condition(self._active_lock)
        self._loaded = False
        self._stop = False

    def _envelope(self, kind: str, operation: str, request_id: str) -> dict:
        return {
            "kind": kind,
            "operation": operation,
            "requestId": request_id,
            "adapterId": self.args.adapter_id,
            "adapterVersion": self.args.adapter_version,
            "protocolVersion": self.args.protocol_version,
        }

    def respond(self, operation: str, request_id: str, payload: dict | None = None) -> None:
        message = self._envelope("response", operation, request_id)
        message["payload"] = payload or {}
        self.transport.write_json(message)

    def progress(self, operation: str, request_id: str, stage: str, value: float | None) -> None:
        message = self._envelope("progress", operation, request_id)
        message.update({"stage": stage, "progress": value})
        self.transport.write_json(message)

    def send_error(self, operation: str, request_id: str, error: WorkerError) -> None:
        message = self._envelope("error", operation, request_id)
        message["error"] = {
            "code": error.code,
            "message": error.message,
            "retryable": error.retryable,
            "details": error.details,
        }
        self.transport.write_json(message)

    def _validate_request(self, message: object) -> tuple[str, str, dict]:
        if not isinstance(message, dict):
            raise WorkerError("PROTOCOL_ERROR", "Worker request must be an object")
        allowed = {"kind", "operation", "requestId", "adapterId", "adapterVersion", "protocolVersion", "payload"}
        if set(message) - allowed or message.get("kind") != "request":
            raise WorkerError("PROTOCOL_ERROR", "Worker request envelope is invalid")
        operation = message.get("operation")
        request_id = message.get("requestId")
        if operation not in OPERATIONS or not isinstance(request_id, str) or not request_id:
            raise WorkerError("PROTOCOL_ERROR", "Worker operation or requestId is invalid")
        if (message.get("adapterId"), message.get("adapterVersion"), message.get("protocolVersion")) != (
            self.args.adapter_id, self.args.adapter_version, self.args.protocol_version
        ):
            raise WorkerError("IDENTITY_MISMATCH", "Worker identity does not match the Adapter")
        payload = message.get("payload")
        if not isinstance(payload, dict):
            raise WorkerError("PROTOCOL_ERROR", "Worker payload must be an object")
        return operation, request_id, payload

    def _finish_task(self, request_id: str) -> None:
        with self._active_changed:
            self._active.pop(request_id, None)
            self._active_changed.notify_all()

    def _run_load(self, request_id: str, parameters: dict, token: CancellationToken) -> None:
        try:
            self.progress("load", request_id, "loading", None)
            token.raise_if_cancelled()
            self.engine.load(self.model_root, parameters, token)
            token.raise_if_cancelled()
            self._loaded = True
            self.respond("load", request_id, {"loaded": True})
        except Exception as error:  # Adapter boundary
            self.send_error("load", request_id, map_error(error, "MODEL_LOAD_FAILED"))
        finally:
            self._finish_task(request_id)

    def _run_generate(self, request_id: str, payload: dict, parameters: dict, token: CancellationToken) -> None:
        try:
            reference = existing_file(payload.get("referenceAudioPath", ""), "referenceAudioPath")
            output = safe_output_path(self.output_root, payload.get("outputPath", ""))
            self.progress("generate", request_id, "preparing", 0.0)
            self.engine.load(self.model_root, parameters, token)
            token.raise_if_cancelled()
            self.engine.generate(payload["text"], reference, output, parameters, token, self.progress, request_id)
            token.raise_if_cancelled()
            if not output.exists() or output.stat().st_size == 0:
                raise WorkerError("OUTPUT_INVALID", "The Adapter did not produce a valid audio file")
            self.progress("generate", request_id, "finalizing", 1.0)
            self.respond("generate", request_id, {"outputPath": payload["outputPath"]})
        except Exception as error:  # Adapter boundary
            self.send_error("generate", request_id, map_error(error, "GENERATION_FAILED"))
        finally:
            self._finish_task(request_id)

    def _start_task(self, request_id: str, target, *arguments) -> None:
        with self._active_lock:
            if self._active:
                raise WorkerError("BUSY", "The Worker already has an active request", True)
            token = CancellationToken()
            self._active[request_id] = token
        threading.Thread(target=target, args=(request_id, *arguments, token), daemon=True).start()

    def handle(self, message: object) -> None:
        operation, request_id, payload = self._validate_request(message)
        if operation in {"hello", "capabilities", "unload", "shutdown"} and payload:
            raise WorkerError("PROTOCOL_ERROR", f"{operation} payload must be empty")
        if operation == "hello":
            self.respond(operation, request_id, {"workerVersion": "1.0.0"})
        elif operation == "capabilities":
            self.respond(operation, request_id, {"schema": self.schema})
        elif operation == "load":
            requested_root = payload.get("modelRoot")
            if requested_root:
                requested_path = existing_directory(requested_root, "modelRoot")
                if requested_path != self.model_root:
                    raise WorkerError("INVALID_PATH", "load modelRoot does not match the launch root")
            parameters = validate_parameters(
                self.schema, payload.get("parameters", {}), allow_hidden_defaults=True
            )
            if self.args.contract_test:
                self._loaded = True
                self.respond(operation, request_id, {"loaded": True})
            else:
                self._start_task(request_id, self._run_load, parameters)
        elif operation == "generate":
            if not isinstance(payload.get("text"), str) or not payload["text"].strip():
                raise WorkerError("INVALID_REQUEST", "generate text is required")
            parameters = validate_parameters(self.schema, payload.get("parameters"))
            self.engine.validate_generation(payload, parameters)
            if not self._loaded:
                raise WorkerError("MODEL_NOT_LOADED", "Load the model before generation")
            self._start_task(request_id, self._run_generate, payload, parameters)
        elif operation == "cancel":
            target_id = payload.get("targetRequestId")
            if not isinstance(target_id, str) or not target_id:
                raise WorkerError("INVALID_REQUEST", "cancel targetRequestId is required")
            with self._active_changed:
                token = self._active.get(target_id)
                if token:
                    token.cancel()
                    while target_id in self._active:
                        self._active_changed.wait()
            self.respond(operation, request_id)
        elif operation == "unload":
            with self._active_lock:
                if self._active:
                    raise WorkerError("BUSY", "Cancel the active request before unloading", True)
            self.engine.unload()
            self._loaded = False
            self.respond(operation, request_id)
        elif operation == "shutdown":
            with self._active_lock:
                for token in self._active.values():
                    token.cancel()
            self.respond(operation, request_id)
            self._stop = True

    def run(self) -> int:
        buffer = bytearray()
        try:
            while not self._stop:
                chunk = self.transport.read(65536)
                if not chunk:
                    return 0
                buffer.extend(chunk)
                if len(buffer) > MAX_FRAME_BYTES and b"\n" not in buffer:
                    return 2
                while b"\n" in buffer:
                    frame, _, remaining = buffer.partition(b"\n")
                    buffer = bytearray(remaining)
                    if not frame:
                        continue
                    if len(frame) > MAX_FRAME_BYTES:
                        return 2
                    operation = "hello"
                    request_id = "protocol-error"
                    try:
                        message = json.loads(frame.decode("utf-8"))
                        if isinstance(message, dict):
                            if isinstance(message.get("operation"), str):
                                operation = message["operation"]
                            if isinstance(message.get("requestId"), str):
                                request_id = message["requestId"]
                        self.handle(message)
                    except WorkerError as error:
                        self.send_error(operation, request_id, error)
                    except (UnicodeDecodeError, json.JSONDecodeError):
                        return 2
                    except Exception as error:
                        self.send_error(operation, request_id, map_error(error, "PROTOCOL_ERROR"))
            return 0
        finally:
            self.transport.close()


def map_error(error: Exception, fallback_code: str) -> WorkerError:
    if isinstance(error, WorkerError):
        return error
    message = str(error).strip() or type(error).__name__
    lowered = message.lower()
    if "out of memory" in lowered:
        return WorkerError("OUT_OF_MEMORY", "The inference runtime ran out of memory")
    return WorkerError(fallback_code, message, details={"exceptionType": type(error).__name__})


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    result.add_argument("--voice-clone-worker", action="store_true")
    result.add_argument("--contract-test", action="store_true")
    result.add_argument("--socket", required=True)
    result.add_argument("--model-root", required=True)
    result.add_argument("--output-root", required=True)
    result.add_argument("--adapter-id", required=True)
    result.add_argument("--adapter-version", required=True)
    result.add_argument("--protocol-version", required=True, type=int)
    return result


def run_worker(engine, schema: dict, expected_adapter_id: str) -> int:
    args = parser().parse_args()
    if not args.voice_clone_worker or args.adapter_id != expected_adapter_id or args.protocol_version != 1:
        raise SystemExit("Invalid Worker launch contract")
    os.environ["HF_HUB_OFFLINE"] = "1"
    os.environ["TRANSFORMERS_OFFLINE"] = "1"
    os.environ["HF_DATASETS_OFFLINE"] = "1"
    os.environ["MODELSCOPE_OFFLINE"] = "1"
    if args.contract_test:
        forbidden = [name for name in sys.modules if name.split(".", 1)[0] in {"torch", "transformers", "qwen_tts", "indextts", "cosyvoice"}]
        if forbidden:
            raise SystemExit(f"Contract mode imported model frameworks: {forbidden[0]}")
        engine = _ContractEngine(engine)
    return WorkerServer(args, engine, schema).run()
