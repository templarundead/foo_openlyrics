"""Package previously tested x86/x64 Release outputs without publishing a release."""

import argparse
from pathlib import Path
import re
import zipfile


def package_component(input_dir: Path, output_dir: Path, revision: str) -> tuple[Path, Path]:
    if not re.fullmatch(r"[0-9a-fA-F]{7,64}", revision):
        raise ValueError("revision must be a Git commit hash")

    # Check everything before creating either archive: a missing PDB or DLL is a
    # build failure, not a reason to silently upload an incomplete component.
    files = {}
    for arch in ("x86", "x64"):
        for suffix in ("dll", "pdb"):
            path = input_dir / arch / f"foo_openlyrics.{suffix}"
            if not path.is_file() or path.stat().st_size == 0:
                raise ValueError(f"Missing or empty Release output: {path}")
            files[arch, suffix] = path

    output_dir.mkdir(parents=True, exist_ok=True)
    name = f"foo_openlyrics-{revision[:12].lower()}"
    component = output_dir / f"{name}.fb2k-component"
    symbols = output_dir / f"{name}-with_debug_symbols.zip"

    # foobar2000 expects the x86 DLL at the root, and the x64 DLL under x64/.
    # This is the same layout as the existing tagged-release workflow.
    with zipfile.ZipFile(component, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.write(files["x86", "dll"], "foo_openlyrics.dll")
        archive.write(files["x64", "dll"], "x64/foo_openlyrics.dll")

    with zipfile.ZipFile(symbols, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for arch in ("x86", "x64"):
            for suffix in ("dll", "pdb"):
                archive.write(files[arch, suffix], f"{arch}/foo_openlyrics.{suffix}")

    return component, symbols


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--revision", required=True)
    args = parser.parse_args()
    for path in package_component(args.input_dir, args.output_dir, args.revision):
        print(path)


if __name__ == "__main__":
    main()
