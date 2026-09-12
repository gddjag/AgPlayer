"""Portable policy tests; these fixtures are not a macOS runtime smoke test."""

import importlib.util
import json
from pathlib import Path
import plistlib
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "package-macos.py"
spec = importlib.util.spec_from_file_location("package_macos", SCRIPT)
packaging = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = packaging
if SCRIPT.exists():
    spec.loader.exec_module(packaging)


def command_string(command, value, prefix_words=3):
    encoded = value.encode() + b"\0"
    size = (prefix_words * 4 + len(encoded) + 7) & ~7
    prefix = [command, size, prefix_words * 4] + [0] * (prefix_words - 3)
    return struct.pack("<" + "I" * prefix_words, *prefix) + encoded.ljust(size - prefix_words * 4, b"\0")


def thin(arch, minimum=(13, 0, 0), deps=(), rpaths=(), filetype=2, legacy=False, platform=1, install_id=None, subtype=None):
    cpu = {"arm64": 0x0100000C, "x86_64": 0x01000007}[arch]
    subtype = subtype if subtype is not None else (3 if arch == "x86_64" else 0)
    version = minimum[0] << 16 | minimum[1] << 8 | minimum[2]
    commands = [struct.pack("<IIII", 0x24, 16, version, version) if legacy else
                struct.pack("<IIIIII", 0x32, 24, platform, version, version, 0)]
    commands += [command_string(0xC, dep, 6) for dep in deps]
    commands += [command_string(0x8000001C, path) for path in rpaths]
    if install_id:
        commands += [command_string(0xD, install_id, 6)]
    data = b"".join(commands)
    return struct.pack("<IIIIIIII", 0xFEEDFACF, cpu, subtype, filetype, len(commands), len(data), 0, 0) + data


def universal(path, arches=("arm64", "x86_64"), overrides=None, **kwargs):
    slices = [thin(arch, **{**kwargs, **(overrides or {}).get(arch, {})}) for arch in arches]
    offset = 8 + 20 * len(slices)
    headers = []
    for arch, data in zip(arches, slices):
        cpu = {"arm64": 0x0100000C, "x86_64": 0x01000007}[arch]
        subtype = struct.unpack_from("<I", data, 8)[0]
        headers.append(struct.pack(">IIIII", cpu, subtype, offset, len(data), 0))
        offset += len(data)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack(">II", 0xCAFEBABE, len(slices)) + b"".join(headers + slices))
    path.chmod(0o755)
    return path


class BundleFixture(unittest.TestCase):
    def setUp(self):
        self.assertTrue(hasattr(packaging, "validate_bundle"), "macOS bundle validator is not implemented")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.app = self.root / "AgPlayer.app"
        self.main = universal(self.app / "Contents/MacOS/AgPlayer", rpaths=["@executable_path/../Frameworks"])
        self.worker = universal(self.app / "Contents/MacOS/AgSeparationWorker")
        self.resources = self.app / "Contents/Resources"
        self.resources.mkdir()
        self.plist = {"CFBundleExecutable": "AgPlayer", "CFBundleShortVersionString": "1.0.3",
                      "CFBundleIdentifier": "com.agplayer.player", "LSMinimumSystemVersion": "13.0",
                      "CFBundleIconFile": "AgPlayer.icns"}
        self.write_plist()

    def write_plist(self):
        (self.app / "Contents/Info.plist").write_bytes(plistlib.dumps(self.plist))


class BundlePolicyTests(BundleFixture):
    def test_all_slices_including_plugin_are_reported(self):
        library = universal(self.app / "Contents/Frameworks/libaudio.dylib", filetype=6,
                            deps=["/usr/lib/libSystem.B.dylib"], legacy=True)
        universal(self.main, deps=["@rpath/libaudio.dylib"], rpaths=["@executable_path/../Frameworks"])
        plugin = universal(self.app / "Contents/PlugIns/audio/libplugin.dylib", filetype=6,
                           deps=["@rpath/libaudio.dylib"])
        result = packaging.validate_bundle(self.app)
        records = {entry["path"]: entry for entry in result["mach_o"]}
        self.assertEqual(len(records), 4)
        self.assertEqual(set(records[plugin.relative_to(self.app).as_posix()]["slices"]), {"arm64", "x86_64"})
        self.assertEqual(records[library.relative_to(self.app).as_posix()]["slices"]["arm64"]["minimum_macos"], "13.0.0")

    def test_missing_worker_rejected(self):
        self.worker.unlink()
        with self.assertRaisesRegex(packaging.PackageError, "AgSeparationWorker"):
            packaging.validate_bundle(self.app)

    def test_single_architecture_nested_plugin_rejected(self):
        universal(self.app / "Contents/PlugIns/libbad.dylib", arches=("arm64",), filetype=6)
        with self.assertRaisesRegex(packaging.PackageError, "x86_64"):
            packaging.validate_bundle(self.app)

    def test_one_slice_requiring_13_3_rejected(self):
        universal(self.worker, overrides={"x86_64": {"minimum": (13, 3, 0)}})
        with self.assertRaisesRegex(packaging.PackageError, "13.3.0"):
            packaging.validate_bundle(self.app)

    def test_specialized_cpu_is_not_general_universal_support(self):
        universal(self.worker, overrides={"x86_64": {"subtype": 8}})
        with self.assertRaisesRegex(packaging.PackageError, "x86_64"):
            packaging.validate_bundle(self.app)

    def test_non_macos_build_version_rejected(self):
        universal(self.worker, overrides={"arm64": {"platform": 2}})
        with self.assertRaisesRegex(packaging.PackageError, "macOS"):
            packaging.validate_bundle(self.app)

    def test_external_absolute_dependency_rejected_even_when_file_exists(self):
        external = universal(self.root / "dev/libaudio.dylib", filetype=6)
        universal(self.main, deps=[external.as_posix()])
        with self.assertRaisesRegex(packaging.PackageError, "external|absolute"):
            packaging.validate_bundle(self.app)

    def test_missing_rpath_dependency_rejected(self):
        universal(self.worker, deps=["@rpath/libabsent.dylib"], rpaths=["@executable_path/../Frameworks"])
        with self.assertRaisesRegex(packaging.PackageError, "libabsent"):
            packaging.validate_bundle(self.app)

    def test_worker_does_not_inherit_main_rpaths(self):
        universal(self.app / "Contents/Frameworks/libaudio.dylib", filetype=6)
        universal(self.worker, deps=["@rpath/libaudio.dylib"])
        with self.assertRaisesRegex(packaging.PackageError, "libaudio"):
            packaging.validate_bundle(self.app)

    def test_development_rpath_rejected(self):
        universal(self.worker, rpaths=["/opt/homebrew/lib"])
        with self.assertRaisesRegex(packaging.PackageError, "rpath"):
            packaging.validate_bundle(self.app)

    def test_dependency_cannot_escape_via_loader_path(self):
        universal(self.worker, deps=["@loader_path/../../../dev/libaudio.dylib"])
        with self.assertRaisesRegex(packaging.PackageError, "escap"):
            packaging.validate_bundle(self.app)

    def test_escaping_symlink_rejected(self):
        external = self.root / "external"
        external.mkdir()
        try:
            (self.resources / "outside").symlink_to(external, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symlinks unavailable on this host: {error}")
        with self.assertRaisesRegex(packaging.PackageError, "symlink.*escap"):
            packaging.validate_bundle(self.app)

    def test_escaping_symlink_policy_without_os_symlink_privileges(self):
        link = self.resources / "link"
        link.touch()
        original_resolve, original_is_symlink = Path.resolve, Path.is_symlink
        def resolve(path, **kwargs):
            return self.root / "outside" if path == link else original_resolve(path, **kwargs)
        def is_symlink(path):
            return path == link or original_is_symlink(path)
        with mock.patch.object(Path, "resolve", resolve), mock.patch.object(Path, "is_symlink", is_symlink):
            with self.assertRaisesRegex(packaging.PackageError, "symlink.*escap"):
                packaging.bundle_files(self.app)

    def test_internal_framework_symlinks_accepted_without_duplicate_slices(self):
        framework = self.app / "Contents/Frameworks/QtCore.framework"
        universal(framework / "Versions/A/QtCore", filetype=6)
        try:
            (framework / "Versions/Current").symlink_to("A", target_is_directory=True)
            (framework / "QtCore").symlink_to("Versions/Current/QtCore")
        except OSError as error:
            self.skipTest(f"symlinks unavailable on this host: {error}")
        self.assertEqual(len(packaging.validate_bundle(self.app)["mach_o"]), 3)

    def test_model_weights_rejected(self):
        (self.resources / "model.onnx").write_bytes(b"model")
        with self.assertRaisesRegex(packaging.PackageError, "external|model"):
            packaging.validate_bundle(self.app)

    def test_corrupt_load_commands_rejected(self):
        data = bytearray(thin("arm64"))
        struct.pack_into("<I", data, 36, 0)
        self.worker.write_bytes(data)
        with self.assertRaisesRegex(packaging.PackageError, "load command"):
            packaging.validate_bundle(self.app)


class PackageFlowTests(BundleFixture):
    def setUp(self):
        super().setUp()
        self.repo = self.root / "source"
        for relative in ["assets/licenses", "LICENSES/runtime", "app/qml", "assets/brand"]:
            (self.repo / relative).mkdir(parents=True)
        (self.repo / "THIRD-PARTY-NOTICES.md").write_text("Windows inventory\n", encoding="utf-8")
        (self.repo / "assets/brand/agplayer-icon.png").write_bytes(b"source icon")
        for relative in packaging.REQUIRED_LICENSES:
            (self.repo / relative).parent.mkdir(parents=True, exist_ok=True)
            (self.repo / relative).write_text("license", encoding="utf-8")
        self.qt = self.root / "Qt/macos"
        (self.qt / "bin").mkdir(parents=True)
        (self.qt / "bin/macdeployqt").write_bytes(b"tool")
        self.output = self.root / "dist"
        self.commands = []
        self.staged = None

    def fake_run(self, argv, **kwargs):
        args = [str(value) for value in argv]
        self.commands.append(args)
        output = ""
        tool = Path(args[0]).name
        if tool == "macdeployqt":
            if len(args) == 2 and args[1].startswith("-"):
                output = "Usage: macdeployqt app-bundle [options]\n-no-codesign\n"
            else:
                self.staged = Path(args[1])
                self.assertNotEqual(self.staged, self.app)
                self.assertIn("-qmldir=" + str(self.repo / "app/qml"), args)
                self.assertIn("-executable=" + str(self.staged / "Contents/MacOS/AgSeparationWorker"), args)
        elif tool == "sips":
            Path(args[args.index("--out") + 1]).write_bytes(b"sized icon")
        elif tool == "iconutil":
            Path(args[args.index("-o") + 1]).write_bytes(b"icns")
        elif tool == "codesign" and "--display" in args:
            output = "Signature=adhoc\n" if "--sign" not in " ".join(sum(self.commands, [])) else "Executable=fixture\n"
        elif tool == "hdiutil":
            self.assertTrue((self.staged / "Contents/Resources/AgPlayer.icns").is_file())
            self.assertTrue((self.staged / "Contents/Resources/THIRD-PARTY-NOTICES.md").is_file())
            self.assertTrue((self.staged / "Contents/Resources/licenses/runtime/Qt-LGPL-3.0-only.txt").is_file())
            Path(args[-1]).write_bytes(b"test disk image")
        elif tool == "xcrun" and "notarytool" in args:
            output = json.dumps({"id": "fixture-submission", "status": "Accepted"})
        return subprocess.CompletedProcess(args, 0, stdout=output, stderr="")

    def package(self, **kwargs):
        with mock.patch.object(packaging.subprocess, "run", side_effect=self.fake_run), \
                mock.patch.object(packaging.sys, "platform", "darwin"), \
                mock.patch.object(Path, "symlink_to"):
            return packaging.package_app(self.app, self.qt, self.output, repo_root=self.repo, **kwargs)

    def test_development_package_uses_staging_and_never_uploads(self):
        original = self.main.read_bytes()
        result = self.package()
        self.assertIn("development", result["artifact"])
        self.assertEqual(result["signing"]["mode"], "ad-hoc")
        self.assertEqual(result["notarization"]["status"], "not-requested")
        self.assertFalse(result["functional_acceptance"]["complete"])
        self.assertEqual(self.main.read_bytes(), original)
        self.assertTrue(Path(result["app"]).is_dir())
        self.assertEqual(Path(result["app"]), self.output / "staging/AgPlayer.app")
        self.assertFalse((self.resources / "AgPlayer.icns").exists())
        self.assertFalse(any("notarytool" in args for args in self.commands))
        signing = [args for args in self.commands if "--sign" in args]
        self.assertTrue(signing)
        self.assertTrue(all("--deep" not in args for args in signing))
        self.assertEqual(Path(signing[-1][-1]).suffix, ".app")
        report = json.loads(Path(result["report"]).read_text(encoding="utf-8"))
        self.assertEqual(report["required_architectures"], ["arm64", "x86_64"])

    def test_invalid_bundle_is_not_signed_or_packaged(self):
        universal(self.worker, arches=("arm64",))
        with self.assertRaises(packaging.PackageError):
            self.package()
        self.assertFalse(any("--sign" in args or "hdiutil" in args for args in self.commands))
        self.assertFalse(list(self.output.glob("*.dmg")))

    def test_notarization_requires_developer_id_before_any_tool(self):
        with self.assertRaisesRegex(packaging.PackageError, "Developer ID"):
            self.package(notary_profile="profile")
        self.assertFalse(self.commands)

    def test_other_certificate_types_rejected(self):
        with self.assertRaisesRegex(packaging.PackageError, "Developer ID Application"):
            self.package(sign_identity="Apple Development: Example")
        self.assertFalse(self.commands)

    def test_notarized_path_requires_accepted_and_stapler_validation(self):
        result = self.package(sign_identity="Developer ID Application: Example (1234567890)", notary_profile="profile")
        self.assertEqual(result["notarization"]["status"], "Accepted")
        self.assertTrue(any("stapler" in args and "validate" in args for args in self.commands))
        self.assertTrue(all("--options" in args and "runtime" in args for args in self.commands
                            if "--sign" in args and not args[-1].endswith(".dmg")))

    def test_notary_rejection_does_not_publish_dmg(self):
        original_run = self.fake_run
        def rejected(argv, **kwargs):
            result = original_run(argv, **kwargs)
            if "notarytool" in argv:
                result.stdout = json.dumps({"id": "fixture-submission", "status": "Invalid"})
            return result
        self.fake_run = rejected
        with self.assertRaisesRegex(packaging.PackageError, "Invalid"):
            self.package(sign_identity="Developer ID Application: Example (1234567890)", notary_profile="profile")
        self.assertFalse(list(self.output.glob("*.dmg")))


if __name__ == "__main__":
    unittest.main()
