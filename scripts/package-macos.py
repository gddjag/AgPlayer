#!/usr/bin/env python3
"""Stage and validate an AgPlayer macOS 13 Universal development DMG.

This is a packaging gate, not macOS feature-parity, launch or audio acceptance.
Only --notary-profile opts into uploading an artifact to Apple's notary service.

Deployment/signing references:
https://doc.qt.io/archives/qt-6.7/macos-deployment.html
https://developer.apple.com/library/archive/technotes/tn2206/
https://developer.apple.com/documentation/security/customizing-the-notarization-workflow
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import mmap
import os
from pathlib import Path
import plistlib
import posixpath
import re
import shutil
import struct
import subprocess
import sys
import tempfile


ARCHITECTURES = ("arm64", "x86_64")
MAX_MINIMUM = (13, 0, 0)
CPU_NAMES = {0x0100000C: "arm64", 0x01000007: "x86_64"}
THIN_MAGICS = {b"\xcf\xfa\xed\xfe": ("<", 32), b"\xfe\xed\xfa\xcf": (">", 32),
               b"\xce\xfa\xed\xfe": ("<", 28), b"\xfe\xed\xfa\xce": (">", 28)}
FAT_MAGICS = {b"\xca\xfe\xba\xbe": (">", 20), b"\xbe\xba\xfe\xca": ("<", 20),
              b"\xca\xfe\xba\xbf": (">", 32), b"\xbf\xba\xfe\xca": ("<", 32)}
REQUIRED_LICENSES = (
    "assets/licenses/AgPlayer-Icons-License.txt", "assets/licenses/Lucide-Icons-License.txt",
    "assets/licenses/RemixIcon-Apache-2.0.txt", "LICENSES/lossless-mp3-window-NOTICE.md",
    "LICENSES/FFmpeg-LGPL-2.1-or-later.txt", "LICENSES/runtime/README.md",
    "LICENSES/runtime/Qt-LGPL-3.0-only.txt", "LICENSES/runtime/Qt-GPL-3.0-only.txt",
    "LICENSES/runtime/FFmpeg-LGPL-2.1-or-later.txt", "LICENSES/runtime/SoundTouch-LGPL-2.1-only.txt",
    "LICENSES/runtime/LAME-LGPL-2.0-only.txt", "LICENSES/runtime/Opus-BSD-3-Clause.txt",
    "LICENSES/runtime/libogg-BSD-3-Clause.txt", "LICENSES/runtime/libvorbis-BSD-3-Clause.txt",
    "LICENSES/runtime/miniaudio-Unlicense-or-MIT-0.txt", "LICENSES/runtime/ONNX-Runtime-MIT.txt",
)
MODEL_SUFFIXES = {".onnx", ".pt", ".pth", ".ckpt", ".safetensors", ".gguf", ".mlmodel", ".mlmodelc"}


class PackageError(RuntimeError):
    pass


@dataclass(frozen=True)
class MachSlice:
    arch: str
    minimum: tuple
    filetype: int
    dependencies: tuple
    rpaths: tuple
    install_id: str | None


def version_string(version):
    return ".".join(str(number) for number in version)


def parse_version(value):
    if not isinstance(value, str) or not re.fullmatch(r"\d+(?:\.\d+){0,2}", value):
        raise PackageError(f"Invalid macOS version: {value!r}")
    parts = tuple(int(part) for part in value.split("."))
    return parts + (0,) * (3 - len(parts))


def architecture(cpu, subtype):
    name = CPU_NAMES.get(cpu, f"cpu-{cpu:#x}")
    subtype &= 0xFFFFFF  # Ignore CPU capability bits, retain ISA requirements.
    if name == "x86_64" and subtype != 3:
        return f"x86_64-subtype-{subtype}"
    if name == "arm64" and subtype not in (0, 1):
        return f"arm64-subtype-{subtype}"
    return name


def _thin(data, offset, length):
    magic = data[offset:offset + 4]
    if magic not in THIN_MAGICS:
        raise PackageError("Invalid Mach-O slice header")
    endian, header_size = THIN_MAGICS[magic]
    if length < header_size:
        raise PackageError("Truncated Mach-O header")
    _, cpu, subtype, filetype, count, command_bytes, _ = struct.unpack_from(endian + "7I", data, offset)
    if header_size + command_bytes > length or count > command_bytes // 8:
        raise PackageError("Truncated Mach-O load commands")
    arch = architecture(cpu, subtype)
    cursor, end = offset + header_size, offset + header_size + command_bytes
    minimum, dependencies, rpaths, install_id = None, [], [], None
    for _ in range(count):
        if cursor + 8 > end:
            raise PackageError("Truncated Mach-O load command")
        command, size = struct.unpack_from(endian + "2I", data, cursor)
        if size < 8 or size % 4 or cursor + size > end:
            raise PackageError("Invalid Mach-O load command size")
        if command in (0x32, 0x24):
            required = 24 if command == 0x32 else 16
            if size < required:
                raise PackageError("Truncated minimum macOS load command")
            if command == 0x32:
                platform, value = struct.unpack_from(endian + "2I", data, cursor + 8)
                if platform != 1:
                    raise PackageError(f"{arch}: build platform {platform} is not macOS")
            else:
                value = struct.unpack_from(endian + "I", data, cursor + 8)[0]
            current = (value >> 16, (value >> 8) & 255, value & 255)
            if minimum is not None and current != minimum:
                raise PackageError(f"{arch}: conflicting minimum macOS load commands")
            minimum = current
        elif command in (0xC, 0xD, 0x80000018, 0x8000001F, 0x20, 0x80000023, 0x8000001C):
            required = 12 if command == 0x8000001C else 24
            if size < required:
                raise PackageError("Truncated dylib/rpath load command")
            string_offset = struct.unpack_from(endian + "I", data, cursor + 8)[0]
            if not required <= string_offset < size:
                raise PackageError("Invalid dylib/rpath load command string")
            raw = data[cursor + string_offset:cursor + size]
            if b"\0" not in raw:
                raise PackageError("Unterminated dylib/rpath load command string")
            value = raw.split(b"\0", 1)[0].decode("utf-8", errors="strict")
            if not value:
                raise PackageError("Empty dylib/rpath load command string")
            if command == 0xD:
                install_id = value
            elif command == 0x8000001C:
                rpaths.append(value)
            else:
                dependencies.append(value)
        cursor += size
    if cursor != end:
        raise PackageError("Mach-O load command count does not match size")
    if minimum is None:
        raise PackageError(f"{arch}: missing minimum macOS load command")
    return MachSlice(arch, minimum, filetype, tuple(dependencies), tuple(rpaths), install_id)


def read_macho(path):
    """Read all FAT32/FAT64 slices, without depending on a host lipo/otool."""
    path = Path(path)
    with path.open("rb") as stream:
        magic = stream.read(4)
        if magic not in THIN_MAGICS and magic not in FAT_MAGICS:
            return {}
        try:
            with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
                if magic in THIN_MAGICS:
                    item = _thin(data, 0, len(data))
                    return {item.arch: item}
                endian, entry_size = FAT_MAGICS[magic]
                if len(data) < 8:
                    raise PackageError("Truncated FAT header")
                count = struct.unpack_from(endian + "I", data, 4)[0]
                table_end = 8 + count * entry_size
                if not count or table_end > len(data):
                    raise PackageError("Invalid FAT architecture table")
                slices, extents = {}, []
                for index in range(count):
                    position = 8 + index * entry_size
                    cpu, subtype = struct.unpack_from(endian + "2I", data, position)
                    offset, length = struct.unpack_from(endian + ("QQ" if entry_size == 32 else "II"), data, position + 8)
                    if offset < table_end or not length or offset + length > len(data):
                        raise PackageError("Invalid FAT slice bounds")
                    if any(offset < other_end and offset + length > start for start, other_end in extents):
                        raise PackageError("Overlapping FAT slices")
                    extents.append((offset, offset + length))
                    item = _thin(data, offset, length)
                    if item.arch != architecture(cpu, subtype) or item.arch in slices:
                        raise PackageError("FAT CPU mismatch or duplicate architecture")
                    slices[item.arch] = item
                return slices
        except (PackageError, UnicodeError, struct.error, ValueError) as error:
            raise PackageError(f"{path}: {error}") from error


def is_inside(path, root):
    return path == root or root in path.parents


def bundle_files(app):
    """Validate links before copying or traversing; never follow directory links."""
    app = Path(app).resolve(strict=True)
    files = []
    for directory, directories, names in os.walk(app, followlinks=False):
        for name in directories + names:
            path = Path(directory) / name
            if path.suffix.lower() in MODEL_SUFFIXES or re.match(r"(?:lib)?onnxruntime.*\.(?:dylib|so|dll)$", name, re.I):
                raise PackageError(f"Optional model/runtime must remain external: {path}")
            if path.is_symlink():
                try:
                    target = path.resolve(strict=True)
                except (OSError, RuntimeError) as error:
                    raise PackageError(f"Broken or cyclic symlink: {path}") from error
                if not is_inside(target, app):
                    raise PackageError(f"Bundle symlink escapes bundle: {path} -> {target}")
            elif path.is_file():
                files.append(path)
            elif not path.is_dir():
                raise PackageError(f"Unsupported bundle filesystem entry: {path}")
    return sorted(files)


def system_path(value):
    normalized = posixpath.normpath(value)
    return normalized.startswith("/usr/lib/") or normalized.startswith("/System/Library/")


def expanded_path(value, loader, executable):
    for token, base in (("@loader_path", loader.parent), ("@executable_path", executable.parent)):
        if value == token or value.startswith(token + "/"):
            return (base / value[len(token):].lstrip("/")).resolve()
    if value.startswith("/") or Path(value).is_absolute():
        return Path(value).resolve()
    raise PackageError(f"Unsupported relative dependency/rpath: {value}")


def resolve_dependency(value, loader, executable, rpaths, app):
    if system_path(value):
        return None
    if value.startswith("@rpath/"):
        candidates = [directory / value[len("@rpath/"):] for directory in rpaths]
    elif value.startswith("@loader_path/") or value.startswith("@executable_path/"):
        candidates = [expanded_path(value, loader, executable)]
    else:
        raise PackageError(f"{loader}: external/absolute dependency is not portable: {value}")
    for candidate in candidates:
        candidate = candidate.resolve()
        if not is_inside(candidate, app):
            raise PackageError(f"{loader}: dependency escapes bundle: {value}")
        if candidate.is_file():
            return candidate
    raise PackageError(f"{loader}: unresolved bundle dependency: {value}")


def validate_bundle(app):
    app = Path(app).resolve(strict=True)
    files = bundle_files(app)
    manifest_path = app / "Contents/Info.plist"
    if not manifest_path.is_file():
        raise PackageError("Missing Contents/Info.plist")
    with manifest_path.open("rb") as stream:
        manifest = plistlib.load(stream)
    if manifest.get("CFBundleExecutable") != "AgPlayer":
        raise PackageError("CFBundleExecutable must be AgPlayer")
    declared = parse_version(manifest.get("LSMinimumSystemVersion"))
    if declared > MAX_MINIMUM:
        raise PackageError(f"Info.plist requires macOS {version_string(declared)}, exceeds 13.0")
    for arch, version in manifest.get("LSMinimumSystemVersionByArchitecture", {}).items():
        if parse_version(version) > MAX_MINIMUM:
            raise PackageError(f"Info.plist {arch} requires macOS {version}, exceeds 13.0")
    executables = [(app / "Contents/MacOS" / name).resolve() for name in ("AgPlayer", "AgSeparationWorker")]
    inventory = {path: slices for path in files if (slices := read_macho(path))}
    for executable in executables:
        if executable.resolve() not in inventory:
            raise PackageError(f"Missing Mach-O executable: {executable}")
        if any(item.filetype != 2 for item in inventory[executable.resolve()].values()):
            raise PackageError(f"Required main/worker is not a Mach-O executable: {executable}")
    for path, slices in inventory.items():
        missing = set(ARCHITECTURES) - slices.keys()
        if missing:
            raise PackageError(f"{path}: missing Universal slices: {', '.join(sorted(missing))}")
        if len({item.filetype for item in slices.values()}) != 1:
            raise PackageError(f"{path}: inconsistent Mach-O file types across architectures")
        if slices["arm64"].filetype == 2 and os.name == "posix" and not path.stat().st_mode & 0o111:
            raise PackageError(f"{path}: missing executable permission")
        for item in slices.values():
            if item.filetype not in (2, 6, 8):
                raise PackageError(f"{path}: unsupported deployed Mach-O type {item.filetype}")
            if item.minimum > MAX_MINIMUM:
                raise PackageError(f"{path} ({item.arch}) requires macOS {version_string(item.minimum)}, exceeds 13.0")
            for rpath in item.rpaths:
                if not rpath.startswith(("@loader_path", "@executable_path")):
                    raise PackageError(f"{path}: nonportable rpath: {rpath}")
            if item.install_id and not item.install_id.startswith(("@rpath/", "@loader_path/", "@executable_path/")):
                raise PackageError(f"{path}: external/absolute dylib ID: {item.install_id}")
    records = {path: {"path": path.relative_to(app).as_posix(), "slices": {
        arch: {"minimum_macos": version_string(item.minimum), "dependencies": list(item.dependencies),
               "rpaths": list(item.rpaths)} for arch, item in slices.items()}} for path, slices in inventory.items()}
    visited = set()

    def visit(path, executable, arch, inherited, chain):
        item = inventory[path][arch]
        own = tuple(expanded_path(value, path, executable) for value in item.rpaths)
        if any(not is_inside(directory, app) for directory in own):
            raise PackageError(f"{path}: rpath escapes bundle")
        rpaths = tuple(dict.fromkeys(own + inherited))
        key = (path, executable, arch, rpaths)
        if key in visited or path in chain:
            return
        visited.add(key)
        for dependency in item.dependencies:
            target = resolve_dependency(dependency, path, executable, rpaths, app)
            if target is not None:
                if target not in inventory or arch not in inventory[target]:
                    raise PackageError(f"{path}: dependency is not a compatible Mach-O: {dependency}")
                visit(target, executable, arch, rpaths, chain | {path})

    entrypoints = [path for path, slices in inventory.items() if slices["arm64"].filetype == 2]
    for executable in entrypoints:
        for arch in ARCHITECTURES:
            visit(executable.resolve(), executable, arch, (), set())
    # Plugins and dlopen-only libraries are not necessarily in LC_LOAD_DYLIB.
    # Check those in the GUI process, using its runpath stack, independently.
    main = executables[0]
    for path in inventory:
        for arch in ARCHITECTURES:
            if not any(key[:3] == (path, main, arch) for key in visited) and path not in entrypoints:
                inherited = tuple(expanded_path(value, main, main) for value in inventory[main][arch].rpaths)
                visit(path, main, arch, inherited, set())
    return {"required_architectures": list(ARCHITECTURES), "maximum_deployment_target": "13.0.0",
            "declared_minimum_macos": version_string(declared), "mach_o": list(records.values()),
            "validation_method": "FAT/Mach-O load commands for every deployed slice; static dependency resolution"}


def run(args, *, check=True):
    result = subprocess.run([str(value) for value in args], capture_output=True, text=True, check=False)
    output = (result.stdout or "") + (result.stderr or "")
    if check and result.returncode:
        raise PackageError(f"{Path(str(args[0])).name} failed ({result.returncode}): {output[-6000:]}")
    return output


def deploy_qt(app, qt_root, repo_root):
    tool = qt_root / "bin/macdeployqt"
    if not tool.is_file():
        raise PackageError(f"Missing macdeployqt: {tool}")
    # Qt 6.7 defaults to no signing and does not accept -no-codesign.
    # Later releases default to ad-hoc; disable it when the option exists.
    help_text = run([tool, "-help"], check=False)
    args = [tool, app, "-verbose=2", f"-qmldir={repo_root / 'app/qml'}",
            f"-executable={app / 'Contents/MacOS/AgSeparationWorker'}"]
    if "-no-codesign" in help_text:
        args.append("-no-codesign")
    output = run(args)
    # Some Qt deployment failures are logged without a nonzero process exit.
    if re.search(r"(?im)^\s*(?:ERROR|Error):", output):
        raise PackageError(f"macdeployqt reported an error: {output[-6000:]}")


def clean_development_rpaths(app):
    """Remove only search paths that cannot be used from the deployed bundle.

    macdeployqt recursively deploys non-system dylib dependencies, including
    FFmpeg. Missing dependencies remain a hard validation failure; do not
    silently search Homebrew or infer an unrelated library by its filename.
    """
    for path in bundle_files(app):
        slices = read_macho(path)
        rpaths = {rpath for item in slices.values() for rpath in item.rpaths
                  if not rpath.startswith(("@loader_path", "@executable_path"))}
        for rpath in sorted(rpaths):
            run(["install_name_tool", "-delete_rpath", rpath, path])


def add_resources(app, repo_root, temporary):
    required = [repo_root / value for value in REQUIRED_LICENSES]
    required += [repo_root / "THIRD-PARTY-NOTICES.md", repo_root / "assets/brand/agplayer-icon.png"]
    for path in required:
        if not path.is_file():
            raise PackageError(f"Required packaging resource missing: {path}")
    resources = app / "Contents/Resources"
    resources.mkdir(parents=True, exist_ok=True)
    shutil.copy2(repo_root / "THIRD-PARTY-NOTICES.md", resources)
    licenses = resources / "licenses"
    shutil.copytree(repo_root / "assets/licenses", licenses, dirs_exist_ok=True)
    shutil.copytree(repo_root / "LICENSES", licenses, dirs_exist_ok=True)
    iconset = temporary / "AgPlayer.iconset"
    iconset.mkdir()
    source = repo_root / "assets/brand/agplayer-icon.png"
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            name = f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png"
            run(["sips", "-z", str(size * scale), str(size * scale), source, "--out", iconset / name])
    run(["iconutil", "-c", "icns", iconset, "-o", resources / "AgPlayer.icns"])
    with (app / "Contents/Info.plist").open("rb") as stream:
        if plistlib.load(stream).get("CFBundleIconFile") not in ("AgPlayer", "AgPlayer.icns"):
            raise PackageError("Info.plist CFBundleIconFile must reference AgPlayer.icns")


def sign_bundle(app, inventory, identity, temporary):
    main_entitlements = temporary / "qml.entitlements"
    main_entitlements.write_bytes(plistlib.dumps({"com.apple.security.cs.allow-jit": True}))
    # The worker loads an optional, separately supplied ONNX Runtime library.
    # Keep its library-validation exception confined to this child executable.
    worker_entitlements = temporary / "worker.entitlements"
    worker_entitlements.write_bytes(plistlib.dumps({"com.apple.security.cs.disable-library-validation": True}))
    nested = {app / record["path"] for record in inventory["mach_o"]}
    nested.discard(app / "Contents/MacOS/AgPlayer")
    nested.update(path for path in app.rglob("*") if path.is_dir() and not path.is_symlink()
                  and path.suffix in (".framework", ".app", ".bundle", ".xpc", ".appex"))
    targets = sorted(nested, key=lambda path: (-len(path.parts), str(path))) + [app]
    for path in targets:
        args = ["codesign", "--force", "--sign", identity, "--options", "runtime"]
        args.append("--timestamp" if identity != "-" else "--timestamp=none")
        if path == app:
            args += ["--entitlements", main_entitlements]
        elif path == app / "Contents/MacOS/AgSeparationWorker":
            args += ["--entitlements", worker_entitlements]
        run(args + [path])
    # --deep is verification-only; never use it to force-sign nested code.
    run(["codesign", "--verify", "--deep", "--strict", "--all-architectures", "--verbose=2", app])
    details = run(["codesign", "--display", "--verbose=4", app])
    return {"mode": "ad-hoc" if identity == "-" else "Developer ID Application", "identity": identity,
            "verified": True, "hardened_runtime": True, "details": details.strip(),
            "qml_entitlements": {"com.apple.security.cs.allow-jit": True},
            "worker_entitlements": {"com.apple.security.cs.disable-library-validation": True}}


def package_app(app, qt_root, output_dir, *, sign_identity=None, notary_profile=None, repo_root=None):
    identity = sign_identity or "-"
    if identity != "-" and not re.fullmatch(r"Developer ID Application: .+ \([A-Z0-9]{10}\)", identity):
        raise PackageError("--sign-identity must be '-' or a full Developer ID Application certificate name")
    if notary_profile is not None and (identity == "-" or not notary_profile.strip()):
        raise PackageError("--notary-profile requires a Developer ID Application identity and nonempty keychain profile")
    if sys.platform != "darwin":
        raise PackageError("Packaging requires macOS; portable Python tests do not validate macOS runtime behavior")
    app, qt_root, output_dir = Path(app).resolve(), Path(qt_root).resolve(), Path(output_dir).resolve()
    repo_root = Path(repo_root).resolve() if repo_root else Path(__file__).resolve().parents[1]
    if not app.is_dir() or app.suffix != ".app":
        raise PackageError(f"Expected an existing .app bundle: {app}")
    if is_inside(output_dir, app):
        raise PackageError("Output directory must be outside the source app")
    bundle_files(app)
    for name in ("AgPlayer", "AgSeparationWorker"):
        if not (app / "Contents/MacOS" / name).is_file():
            raise PackageError(f"Missing required executable: {name}")
    with (app / "Contents/Info.plist").open("rb") as stream:
        version = plistlib.load(stream).get("CFBundleShortVersionString", "")
    if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise PackageError("CFBundleShortVersionString must contain an explicit major.minor.patch version")
    mode = "development" if identity == "-" else (
        "developer-id-notarized-development" if notary_profile else "developer-id-unnotarized-development")
    stem = f"AgPlayer-{version}-macOS-universal-{mode}"
    output_dir.mkdir(parents=True, exist_ok=True)
    final_dmg, final_report = output_dir / f"{stem}.dmg", output_dir / f"{stem}.validation.json"
    final_app = output_dir / "staging/AgPlayer.app"
    if final_dmg.exists() or final_report.exists() or final_app.exists():
        raise PackageError(f"Refusing to overwrite existing packaging output: {stem}")
    with tempfile.TemporaryDirectory(prefix=".agplayer-macos-", dir=output_dir) as directory:
        temporary = Path(directory)
        image_root = temporary / "image"
        image_root.mkdir()
        staged_app = image_root / "AgPlayer.app"
        shutil.copytree(app, staged_app, symlinks=True)
        bundle_files(staged_app)
        deploy_qt(staged_app, qt_root, repo_root)
        clean_development_rpaths(staged_app)
        add_resources(staged_app, repo_root, temporary)
        report = validate_bundle(staged_app)
        report.update({"status": "validated-development-package", "version": version,
                       "functional_acceptance": {"complete": False, "required": [
                           "macOS 13.0 Intel and Apple Silicon launch, playback and hardware QA",
                           "all Windows 1.0.3 features, including the external separation runtime"]},
                       "license_inventory": "Existing bundled notices describe Windows; macOS deployed attributions require review",
                       "signing": sign_bundle(staged_app, report, identity, temporary),
                       "notarization": {"status": "not-requested"}})
        (image_root / "Applications").symlink_to("/Applications", target_is_directory=True)
        disk_image = temporary / f"{stem}.dmg"
        run(["hdiutil", "create", "-volname", f"AgPlayer {version}", "-srcfolder", image_root,
             "-format", "UDZO", "-fs", "HFS+", disk_image])
        if identity != "-":
            run(["codesign", "--force", "--sign", identity, "--timestamp", disk_image])
            run(["codesign", "--verify", "--strict", disk_image])
        if notary_profile:
            response = json.loads(run(["xcrun", "notarytool", "submit", disk_image,
                                       "--keychain-profile", notary_profile, "--wait", "--output-format", "json"]))
            if response.get("status") != "Accepted":
                raise PackageError(f"Notarization failed: {response.get('status', 'missing status')}; submission {response.get('id')}")
            run(["xcrun", "stapler", "staple", disk_image])
            run(["xcrun", "stapler", "validate", disk_image])
            report["notarization"] = {"status": "Accepted", "submission_id": response.get("id"),
                                      "stapled": True, "staple_verified": True, "container": "dmg"}
        if not disk_image.is_file() or not disk_image.stat().st_size:
            raise PackageError("hdiutil did not create a nonempty disk image")
        digest = hashlib.sha256()
        with disk_image.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        checksum = digest.hexdigest()
        report.update({"artifact": str(final_dmg), "report": str(final_report), "app": str(final_app), "sha256": checksum})
        staged_report = temporary / "validation.json"
        staged_report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        final_app.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(staged_app), final_app)
        shutil.move(str(disk_image), final_dmg)
        shutil.move(str(staged_report), final_report)
        return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True, help="Universal AgPlayer.app to copy into staging")
    parser.add_argument("--qt-root", type=Path, required=True, help="Qt macOS root containing bin/macdeployqt")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--sign-identity", help="Full Developer ID Application certificate name; default: ad-hoc development")
    parser.add_argument("--notary-profile", help="Explicit opt-in to upload DMG using a stored notarytool keychain profile")
    args = parser.parse_args(argv)
    try:
        report = package_app(args.app, args.qt_root, args.output_dir,
                             sign_identity=args.sign_identity, notary_profile=args.notary_profile)
    except (PackageError, OSError, ValueError, plistlib.InvalidFileException) as error:
        print(json.dumps({"status": "failed", "error": str(error)}, ensure_ascii=False), file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
