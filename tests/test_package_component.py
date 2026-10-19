"""Offline checks for the installable component and debug-symbol archive layouts."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / ".github/scripts/package_component.py"
SPEC = importlib.util.spec_from_file_location("package_component", SCRIPT)
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)


class ComponentPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.inputs = root / "release inputs"
        self.outputs = root / "packaged outputs"
        self.contents = {}
        for arch in ("x86", "x64"):
            (self.inputs / arch).mkdir(parents=True)
            for suffix in ("dll", "pdb"):
                path = self.inputs / arch / f"foo_openlyrics.{suffix}"
                data = f"fixture {arch} {suffix}".encode()
                path.write_bytes(data)
                self.contents[arch, suffix] = data

    def package(self, revision="0123456789abcdef0123456789abcdef01234567"):
        return PACKAGER.package_component(self.inputs, self.outputs, revision)

    def test_installer_contains_only_the_two_correctly_located_dlls(self):
        component, _ = self.package()
        self.assertEqual(component.name, "foo_openlyrics-0123456789ab.fb2k-component")
        with zipfile.ZipFile(component) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(archive.namelist()), {"foo_openlyrics.dll", "x64/foo_openlyrics.dll"})
            self.assertEqual(archive.read("foo_openlyrics.dll"), self.contents["x86", "dll"])
            self.assertEqual(archive.read("x64/foo_openlyrics.dll"), self.contents["x64", "dll"])

    def test_symbols_keep_each_pdb_with_its_matching_dll(self):
        _, symbols = self.package()
        self.assertEqual(symbols.name, "foo_openlyrics-0123456789ab-with_debug_symbols.zip")
        with zipfile.ZipFile(symbols) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(
                set(archive.namelist()),
                {f"{arch}/foo_openlyrics.{suffix}" for arch, suffix in self.contents},
            )
            for (arch, suffix), data in self.contents.items():
                self.assertEqual(archive.read(f"{arch}/foo_openlyrics.{suffix}"), data)

    def test_missing_outputs_fail_before_creating_archives(self):
        for arch, suffix in self.contents:
            with self.subTest(arch=arch, suffix=suffix):
                path = self.inputs / arch / f"foo_openlyrics.{suffix}"
                data = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(ValueError, "Missing or empty"):
                    self.package()
                self.assertFalse(self.outputs.exists())
                path.write_bytes(data)

    def test_empty_outputs_fail_before_creating_archives(self):
        for arch, suffix in self.contents:
            with self.subTest(arch=arch, suffix=suffix):
                path = self.inputs / arch / f"foo_openlyrics.{suffix}"
                data = path.read_bytes()
                path.write_bytes(b"")
                with self.assertRaisesRegex(ValueError, "Missing or empty"):
                    self.package()
                self.assertFalse(self.outputs.exists())
                path.write_bytes(data)

    def test_invalid_revision_cannot_be_used_as_a_filename(self):
        for revision in ("", "main", "../bad-path", "abc123", "a" * 65):
            with self.subTest(revision=revision):
                with self.assertRaisesRegex(ValueError, "Git commit hash"):
                    self.package(revision)
                self.assertFalse(self.outputs.exists())

    def test_short_commit_hash_is_supported(self):
        component, _ = self.package("ABC1234")
        self.assertEqual(component.name, "foo_openlyrics-abc1234.fb2k-component")


if __name__ == "__main__":
    unittest.main()
