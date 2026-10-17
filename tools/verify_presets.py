"""Check the SmearNT bank against its native envelope and real adapter (stdlib).

These are project compatibility checks, not a manufacturer-certified JSON schema.
No disting NT firmware loader or physical hardware is executed by this script.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PARAMETER_NAMES = [
    "In L", "In R", "Out L", "Out L mode", "Out R", "Out R mode",
    "Frequency", "Resonance", "Stages", "Distribution", "Feedback",
    "Stereo spread", "LR motion", "Stereo crossover",
    "Clock source", "Tempo", "Clock CV", "Reset CV", "Clock PPQN", "Beats per bar",
    "Mod 1 shape", "Mod 1 cycle", "Mod 1 amount", "Mod 1 offset", "Mod 1 phase",
    "Mod 2 shape", "Mod 2 cycle", "Mod 2 amount", "Mod 2 offset", "Mod 2 phase",
    "CV 1 input", "CV 2 input",
]
PARAMETER_NAMES += [f"Route {route} {field}"
                    for route in range(1, 9) for field in ("source", "target", "amount")]
PARAMETER_DEFAULTS = [
    1, 2, 13, 0, 14, 0, 50, 50, 50, 50, 20,
    0, 0, 250, 0, 120, 0, 0, 0, 4,
    0, 15, 100, 0, 0, 0, 17, 100, 0, 90, 0, 0,
] + [0] * 24
LEGACY_PARAMETER_COUNT = 11
FILENAME = re.compile(r"^(0[1-9]|1[0-6])_[A-Za-z0-9_]+\.json$")


class ValidationError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ValidationError(message)


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValidationError(f"non-finite JSON constant: {value}")


def decode_json(raw, label):
    require(not raw.startswith(b"\xef\xbb\xbf"), f"{label}: UTF-8 BOM is forbidden")
    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=no_duplicates,
                          parse_constant=reject_constant)
    except (UnicodeError, json.JSONDecodeError, ValidationError) as error:
        raise ValidationError(f"{label}: {error}") from error


def keys(value, expected, label):
    require(type(value) is dict, f"{label}: expected an object")
    require(set(value) == set(expected), f"{label}: unexpected or missing keys")


def integer(value, label, minimum=None, maximum=None):
    require(type(value) is int, f"{label}: expected an integer, not bool or float")
    if minimum is not None:
        require(value >= minimum, f"{label}: below minimum {minimum}")
    if maximum is not None:
        require(value <= maximum, f"{label}: above maximum {maximum}")


def integer_equal(value, expected, label):
    integer(value, label)
    require(value == expected, f"{label}: expected {expected}")


def zeros(value, count, label):
    require(type(value) is list and len(value) == count,
            f"{label}: expected {count} integer zeroes")
    for index, item in enumerate(value):
        integer_equal(item, 0, f"{label}[{index}]")


def ascii_name(value, label):
    require(type(value) is str and 1 <= len(value) <= 31,
            f"{label}: expected a name of 1..31 characters")
    require(all(32 <= ord(char) <= 126 for char in value),
            f"{label}: expected printable ASCII")


def execute(inspector, arguments):
    try:
        result = subprocess.run([str(inspector)] + [str(value) for value in arguments],
                                capture_output=True, check=False, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValidationError(f"native inspector could not run: {error}") from error
    payload = decode_json(result.stdout, "native inspector output")
    require(type(payload) is dict, "native inspector must emit a JSON object")
    if result.returncode != 0:
        message = payload.get("error", result.stderr.decode("utf-8", errors="replace").strip())
        raise ValidationError(f"native inspector failed ({result.returncode}): {message}")
    return payload


def validate_contract(contract):
    keys(contract, ["guid", "guid_numeric", "api_version", "factory_name",
                    "num_specifications", "num_parameters", "sram_bytes_host",
                    "common_parameter_offset", "parameters"], "factory contract")
    require(contract["guid"] == "SMNT", "factory GUID must be SMNT")
    integer_equal(contract["guid_numeric"], int.from_bytes(b"SMNT", "little"), "factory GUID number")
    integer_equal(contract["api_version"], 13, "factory API version")
    require(contract["factory_name"] == "SmearNT", "factory name must be SmearNT")
    integer_equal(contract["num_specifications"], 0, "factory specification count")
    integer_equal(contract["num_parameters"], len(PARAMETER_NAMES), "factory parameter count")
    integer_equal(contract["common_parameter_offset"], 1, "native common parameter offset")
    integer(contract["sram_bytes_host"], "host SRAM bytes", 1)
    parameters = contract["parameters"]
    require(type(parameters) is list and len(parameters) == len(PARAMETER_NAMES),
            "factory contract must contain 56 append-only parameters")
    for index, parameter in enumerate(parameters):
        label = f"factory parameter {index}"
        keys(parameter, ["plugin_index", "native_index", "name", "min", "max",
                         "default", "unit", "scaling"], label)
        integer_equal(parameter["plugin_index"], index, f"{label} plugin index")
        integer_equal(parameter["native_index"], index + 1, f"{label} native index")
        require(parameter["name"] == PARAMETER_NAMES[index], f"{label}: unexpected name/order")
        integer(parameter["min"], f"{label} minimum", -32768, 32767)
        integer(parameter["max"], f"{label} maximum", parameter["min"], 32767)
        integer(parameter["default"], f"{label} default", parameter["min"], parameter["max"])
        integer_equal(parameter["default"], PARAMETER_DEFAULTS[index], f"{label} factory default")
        integer(parameter["unit"], f"{label} unit", 0, 255)
        integer(parameter["scaling"], f"{label} scaling", 0, 255)
    return parameters


def validate_preset(document, index, factory_parameters):
    keys(document, ["kind", "author", "engine", "version", "name", "slots", "ui"], "preset")
    require(document["kind"] == "disting NT preset", "preset kind is not native NT")
    require(document["author"] == "SmearNT", "preset author must be SmearNT")
    keys(document["engine"], ["sampleRate", "blockSize"], "engine")
    integer_equal(document["engine"]["sampleRate"], 48000, "engine sampleRate")
    integer_equal(document["engine"]["blockSize"], 24, "engine blockSize")
    integer_equal(document["version"], 2, "preset version")
    ascii_name(document["name"], "preset name")
    require(document["name"].startswith(f"SMNT {index:02d} "), "preset name/index mismatch")
    require(type(document["slots"]) is list and len(document["slots"]) == 1,
            "preset must contain exactly one slot")
    slot = document["slots"][0]
    keys(slot, ["guid", "specs", "name", "parameters", "ui"], "slot")
    require(slot["guid"] == "SMNT", "slot GUID must be SMNT")
    ascii_name(slot["name"], "slot name")
    require(slot["name"] == "SmearNT", "slot name must match factory")
    zeros(slot["specs"], 3, "slot specs")
    parameters = slot["parameters"]
    require(type(parameters) is list and
            len(parameters) in (LEGACY_PARAMETER_COUNT + 1, len(factory_parameters) + 1),
            "slot parameters: expected Bypass plus 11 legacy or all 56 factory parameters")
    integer_equal(parameters[0], 0, "Bypass")
    for parameter_index, parameter in enumerate(factory_parameters[:len(parameters) - 1]):
        integer(parameters[parameter_index + 1], parameter["name"], parameter["min"], parameter["max"])
    require(parameters[1:7] == [1, 2, 13, 1, 14, 1],
            "routing must be input 1/2 to output busses 13/14 in Replace mode")
    keys(slot["ui"], ["page", "items", "display"], "slot UI")
    integer_equal(slot["ui"]["page"], 0, "slot UI page")
    zeros(slot["ui"]["items"], 16, "slot UI items")
    require(slot["ui"]["display"] == "params", "slot UI display must be params")
    keys(document["ui"], ["currentSlot", "displayMode"], "preset UI")
    integer_equal(document["ui"]["currentSlot"], 0, "preset UI currentSlot")
    require(document["ui"]["displayMode"] == "algorithm", "preset UI displayMode must be algorithm")
    return parameters


def validate_stats(stats):
    for flag in ["pass", "finite", "no_clip", "inputs_preserved", "other_busses_preserved",
                 "routing_replace", "nonzero"]:
        require(stats.get(flag) is True, f"native audio check failed: {flag}")
    integer_equal(stats.get("sample_rate"), 48000, "native sample rate")
    integer_equal(stats.get("frames_per_block"), 24, "native block size")
    for label in ["peak_left", "peak_right", "rms_left", "rms_right"]:
        value = stats.get(label)
        require(type(value) in (int, float) and math.isfinite(value) and 0 <= value <= 1,
                f"native {label} is invalid or exceeds PCM full scale")
    for label in ["nonzero_left", "nonzero_right"]:
        integer(stats.get(label), f"native {label}", 1)


def validate_bank(bank, inspector):
    contract = execute(inspector, [])
    factory_parameters = validate_contract(contract)
    require(bank.is_dir(), f"preset bank not found: {bank}")
    files = sorted(path for path in bank.iterdir() if path.is_file() and path.suffix.lower() == ".json")
    require(len(files) == 16, f"expected exactly 16 JSON presets, found {len(files)}")
    names = set()
    controls = set()
    entries = []
    for index, path in enumerate(files, 1):
        match = FILENAME.fullmatch(path.name)
        require(match is not None and int(match.group(1)) == index,
                f"preset filename must use unique sequential 01..16 indices: {path.name}")
        raw = path.read_bytes()
        document = decode_json(raw, path.name)
        try:
            parameters = validate_preset(document, index, factory_parameters)
            require(document["name"] not in names, "preset names must be unique")
            tuple_controls = tuple(parameters[7:])
            require(tuple_controls not in controls, "control tuples must be distinct")
            names.add(document["name"])
            controls.add(tuple_controls)
            stats = execute(inspector, parameters)
            validate_stats(stats)
        except ValidationError as error:
            raise ValidationError(f"{path.name}: {error}") from error
        try:
            relative_path = path.relative_to(ROOT).as_posix()
        except ValueError:
            relative_path = str(path)
        entries.append({"path": relative_path, "file": path.name, "name": document["name"],
                        "sha256": hashlib.sha256(raw).hexdigest(),
                        "parameters": parameters,
                        "effective_parameters": parameters + [parameter["default"] for parameter
                            in factory_parameters[len(parameters) - 1:]],
                        "native_result": stats})
    return {"pass": True, "count": len(entries),
            "validation_scope": "project native envelope plus real adapter host execution",
            "manufacturer_schema_certification": False, "hardware_loader_executed": False,
            "bank": str(bank), "inspector": str(inspector),
            "inspector_sha256": hashlib.sha256(inspector.read_bytes()).hexdigest(),
            "factory_contract": contract, "presets": entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspector", required=True, type=Path,
                        help="native PresetInspector executable compiled from the real adapter")
    parser.add_argument("--bank", type=Path, default=ROOT / "presets" / "SmearNT16")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "preset-validation" / "validation.json")
    options = parser.parse_args()
    output = options.output.resolve()
    try:
        report = validate_bank(options.bank.resolve(), options.inspector.resolve())
    except (ValidationError, OSError) as error:
        report = {"pass": False, "hardware_loader_executed": False, "error": str(error)}
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: 16/16 native NT presets; adapter audio checks passed. Report: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
