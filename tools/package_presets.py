"""Package the validated WideSmearNT16 bank with the audited ARM plug-in."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validation", type=Path,
                        default=ROOT / "build/preset-validation/validation.json")
    parser.add_argument("--release-manifest", type=Path,
                        default=ROOT / "build/audit-2026-10-15/release-manifest.json")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "archive/2026-10-15/WideSmearNT16-NT-2026-10-15.zip")
    args = parser.parse_args()
    report_bytes = args.validation.read_bytes()
    report = json.loads(report_bytes)
    require(report.get("pass") is True and report.get("count") == 16,
            "All 16 presets must pass native validation before packaging")
    require(report.get("hardware_loader_executed") is False,
            "Validation must accurately disclose the hardware-loader boundary")
    contract = report["factory_contract"]
    require(contract["api_version"] == 13 and contract["guid"] == "SMNT"
            and contract["num_parameters"] == 56 and contract["common_parameter_offset"] == 1,
            "Validated native factory does not match the audited WideSmearNT API v13 plug-in")
    rows = report["presets"]
    require(len(rows) == 16, "Expected exactly 16 validation results")

    # The native factory and the included ARM object must refer to the same
    # unchanged sources/API as the completed maintenance audit.
    audited = json.loads(args.release_manifest.read_bytes())
    require(audited["checks_exit_code"] == 0 and audited["api_version"] == 13,
            "Expected successful API v13 maintenance audit")
    checked_sources = []
    audited_object_hash = None
    for row in audited["files"]:
        path = row["path"]
        if path.startswith(("src/", "include/")) or path == "build/WideSmearNT.o":
            require(sha256((ROOT / path).read_bytes()) == row["sha256"],
                    f"Audited source/object has changed: {path}")
            if path == "build/WideSmearNT.o":
                audited_object_hash = row["sha256"]
            else:
                checked_sources.append(path)
    require(audited_object_hash and checked_sources,
            "Missing audited plug-in object or source fingerprints")

    payload = {}
    bank_files = set((ROOT / "presets/WideSmearNT16").glob("*.json"))
    seen_files = set()
    for row in rows:
        relative = Path(row["path"])
        require(not relative.is_absolute() and ".." not in relative.parts,
                "Unsafe preset path in validation report")
        path = ROOT / relative
        require(path in bank_files and path not in seen_files,
                f"Unexpected or repeated validated preset: {relative}")
        data = path.read_bytes()
        require(sha256(data) == row["sha256"], f"Preset changed after validation: {relative}")
        require(row["native_result"]["pass"] is True, f"Native preset test failed: {relative}")
        payload[relative.as_posix()] = data
        seen_files.add(path)
    require(seen_files == bank_files and len(bank_files) == 16,
            "Preset directory differs from the validated bank")

    payload["programs/plug-ins/WideSmearNT.o"] = (ROOT / "build/WideSmearNT.o").read_bytes()
    payload["WideSmearNT16-README.md"] = (ROOT / "presets/README.md").read_bytes()
    payload["WideSmearNT-modulation-README.md"] = (ROOT / "README.md").read_bytes()
    payload["validation/validation.json"] = report_bytes
    manifest = {
        "bank": "WideSmearNT16", "date": "2026-10-15", "native_preset_version": 2,
        "preset_count": 16, "plugin_guid": "SMNT", "plugin_api_version": 13,
        "minimum_firmware": "1.15.0", "target_firmware": "1.18.0",
        "hardware_loader_executed": False,
        "audited_object_sha256": audited_object_hash,
        "audited_source_files_unchanged": checked_sources,
        "files": [{"path": path, "bytes": len(data), "sha256": sha256(data)}
                  for path, data in sorted(payload.items())],
    }
    payload["validation/manifest.json"] = (
        json.dumps(manifest, ensure_ascii=True, indent=2) + "\n").encode("utf-8")
    payload["SHA256SUMS.txt"] = "".join(
        f"{sha256(data)}  {path}\n" for path, data in sorted(payload.items())
    ).encode("ascii")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(".zip.tmp")
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path, data in sorted(payload.items()):
                entry = zipfile.ZipInfo(path, date_time=(2026, 10, 15, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, data)
        with zipfile.ZipFile(temporary) as archive:
            require(archive.testzip() is None, "ZIP integrity check failed")
            require(set(archive.namelist()) == set(payload), "ZIP file list mismatch")
            for path, expected in payload.items():
                require(archive.read(path) == expected, f"ZIP content mismatch: {path}")
            native_files = [path for path in archive.namelist()
                            if path.startswith("presets/WideSmearNT16/") and path.endswith(".json")]
            require(len(native_files) == 16, "Expected exactly 16 native presets in the ZIP")
            for path in native_files:
                require(json.loads(archive.read(path))["kind"] == "disting NT preset",
                        f"Non-native preset in ZIP: {path}")
        temporary.replace(args.output)
    finally:
        if temporary.exists():
            temporary.unlink()
    digest = sha256(args.output.read_bytes())
    checksum = args.output.with_suffix(".zip.sha256")
    checksum.write_text(f"{digest}  {args.output.name}\n", encoding="ascii")
    print(f"Packaged and verified 16 native presets: {args.output}")
    print(f"SHA256 {digest}")


if __name__ == "__main__":
    main()
