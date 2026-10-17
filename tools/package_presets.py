"""Package the current verified WideSmearNT plug-in and 16 presets for MicroSD."""
import argparse
from datetime import date
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import zipfile

import verify_presets

ROOT = Path(__file__).resolve().parents[1]
BANK = ROOT / "presets/WideSmearNT16"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def project_file(relative):
    """Accept only normal, relative paths to files inside this project."""
    require(isinstance(relative, str) and "\\" not in relative,
            "Unsafe file path in validation report")
    parts = PurePosixPath(relative)
    require(not parts.is_absolute() and ".." not in parts.parts and ":" not in relative,
            "Unsafe file path in validation report")
    path = (ROOT / relative).resolve()
    require(path.is_relative_to(ROOT) and path.is_file(),
            f"Missing or unsafe project file: {relative}")
    return path


def require_fresh(artifact, sources):
    require(artifact.is_file(), f"Missing artifact: {artifact}")
    stale = [path.relative_to(ROOT).as_posix() for path in sources
             if path.stat().st_mtime_ns > artifact.stat().st_mtime_ns]
    require(not stale, f"Rebuild {artifact.name}; newer sources: {', '.join(stale)}")


def verify_object(object_path):
    host = shutil.which("pwsh") or shutil.which("powershell")
    require(host, "PowerShell is required to verify the ARM plug-in")
    subprocess.run([host, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    str(ROOT / "verify.ps1"), "-ObjectPath", str(object_path)],
                   cwd=ROOT, check=True, timeout=60)


def version_date(version):
    match = re.fullmatch(r"v(\d{4})\.(\d{2})\.(\d{2})(?:[.-][A-Za-z0-9]+)*", version)
    require(match is not None, "Version must start with vYYYY.MM.DD")
    release_date = date(*(int(value) for value in match.groups()))
    require(1980 <= release_date.year <= 2107, "Version date is outside the ZIP date range")
    return (release_date.year, release_date.month, release_date.day, 0, 0, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validation", type=Path,
                        default=ROOT / "build/preset-validation/validation.json")
    parser.add_argument("--object", type=Path, default=ROOT / "build/WideSmearNT.o")
    parser.add_argument("--version", default="v2026.10.17.1")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    timestamp = version_date(args.version)
    output = (args.output or ROOT / "build/releases" /
              f"WideSmearNT16-NT-{args.version}.zip").resolve()
    object_path = args.object.resolve()
    report = json.loads(args.validation.read_bytes())
    require(report.get("pass") is True and report.get("count") == 16,
            "All 16 presets must pass native validation before packaging")
    contract = report["factory_contract"]
    verify_presets.validate_contract(contract)
    inspector = Path(report["inspector"])
    if not inspector.is_absolute():
        inspector = ROOT / inspector
    inspector = inspector.resolve()
    require(inspector.is_file() and
            sha256(inspector.read_bytes()) == report["inspector_sha256"],
            "Native inspector is missing or changed since validation")

    core = ROOT / "src/widesmearnt_core"
    arm_sources = ([ROOT / "src/WideSmearNTAdapter.cpp"] +
                   list((ROOT / "src").glob("*.h")) +
                   list(core.rglob("*.h")) + list(core.rglob("*.cpp")) +
                   list((ROOT / "include").rglob("*.h")))
    require_fresh(object_path, arm_sources)
    require_fresh(inspector, arm_sources + [ROOT / "src/NTHostShim.cpp",
                                          ROOT / "tests/PresetInspector.cpp"])
    verify_object(object_path)

    # Recheck the current inspector and bank instead of trusting an old audit.
    current = verify_presets.validate_bank(BANK, inspector)
    require(current["factory_contract"] == contract,
            "Factory contract changed since validation")
    rows = report["presets"]
    require(len(rows) == 16, "Expected exactly 16 validation results")
    bank_files = {path.resolve() for path in BANK.iterdir()
                  if path.is_file() and path.suffix.lower() == ".json"}
    seen_files = set()
    payload = {}
    for row in rows:
        path = project_file(row["path"])
        require(path in bank_files and path not in seen_files,
                f"Unexpected or repeated validated preset: {row['path']}")
        data = path.read_bytes()
        require(sha256(data) == row["sha256"],
                f"Preset changed after validation: {row['path']}")
        verify_presets.validate_stats(row["native_result"])
        payload[f"presets/WideSmearNT16/{path.name}"] = data
        seen_files.add(path)
    require(seen_files == bank_files and len(bank_files) == 16,
            "Preset directory differs from the validated bank")

    payload["programs/plug-ins/WideSmearNT.o"] = object_path.read_bytes()
    payload["README.md"] = (ROOT / "README.md").read_bytes()
    payload["PRESETS.md"] = (ROOT / "presets/README.md").read_bytes()
    manifest = {
        "name": "WideSmearNT", "version": args.version,
        "description": "A stereo phase effect for disting NT, with 16 presets.",
        "files": [{"path": path, "sha256": sha256(data)}
                  for path, data in sorted(payload.items())],
    }
    payload["manifest.json"] = (
        json.dumps(manifest, ensure_ascii=True, indent=2) + "\n").encode("utf-8")
    payload["SHA256SUMS.txt"] = "".join(
        f"{sha256(data)}  {path}\n" for path, data in sorted(payload.items())
    ).encode("ascii")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".zip.tmp")
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path, data in sorted(payload.items()):
                parts = PurePosixPath(path)
                require(not parts.is_absolute() and ".." not in parts.parts,
                        f"Unsafe ZIP path: {path}")
                entry = zipfile.ZipInfo(path, date_time=timestamp)
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, data)
        with zipfile.ZipFile(temporary) as archive:
            require(archive.testzip() is None, "ZIP integrity check failed")
            require(len(archive.namelist()) == len(payload) and
                    set(archive.namelist()) == set(payload), "ZIP file list mismatch")
            for path, expected in payload.items():
                require(archive.read(path) == expected, f"ZIP content mismatch: {path}")
            native_files = [path for path in archive.namelist()
                            if path.startswith("presets/WideSmearNT16/") and path.endswith(".json")]
            require(len(native_files) == 16, "Expected exactly 16 native presets in the ZIP")
        temporary.replace(output)
    finally:
        if temporary.exists():
            temporary.unlink()
    digest = sha256(output.read_bytes())
    checksum = output.with_suffix(".zip.sha256")
    checksum.write_text(f"{digest}  {output.name}\n", encoding="ascii")
    print(f"Packaged and verified 16 native presets: {output}")
    print(f"SHA256 {digest}")


if __name__ == "__main__":
    main()
