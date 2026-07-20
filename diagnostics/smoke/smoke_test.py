#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


SUITE_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_SUITE = SUITE_DIRECTORY / "suite.json"


class SmokeTestError(RuntimeError):
    pass


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    pixels: bytes


@dataclass(frozen=True)
class Comparison:
    exact: bool
    changed_pixels: int
    total_pixels: int
    maximum_channel_delta: int
    mean_absolute_channel_delta: float
    baseline_sha256: str
    candidate_sha256: str


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SmokeTestError(f"Could not read JSON file '{path}': {error}") from error
    if not isinstance(value, dict):
        raise SmokeTestError(f"JSON file '{path}' must contain an object")
    return value


def load_suite(path: Path) -> list[dict[str, str]]:
    suite = load_json(path)
    if suite.get("version") != 1:
        raise SmokeTestError(f"Unsupported suite version in '{path}'")
    cases = suite.get("cases")
    if not isinstance(cases, list) or not cases:
        raise SmokeTestError(f"Suite '{path}' must contain a non-empty cases array")

    result: list[dict[str, str]] = []
    names: set[str] = set()
    for index, case in enumerate(cases):
        if not isinstance(case, dict) or set(case) != {"name", "target", "config"}:
            raise SmokeTestError(f"Case {index} in '{path}' must contain name, target, and config")
        if not all(isinstance(case[field], str) and case[field] for field in ("name", "target", "config")):
            raise SmokeTestError(f"Case {index} in '{path}' contains an invalid string")
        if Path(case["name"]).name != case["name"]:
            raise SmokeTestError(f"Case name '{case['name']}' is not a simple path component")
        if case["name"] in names:
            raise SmokeTestError(f"Duplicate case name '{case['name']}'")
        names.add(case["name"])
        result.append(case)
    return result


def suite_fingerprint(path: Path, cases: list[dict[str, str]]) -> str:
    suite_directory = path.parent
    configs = {
        case["config"]: load_json((suite_directory / case["config"]).resolve())
        for case in cases
    }
    canonical = json.dumps(
        {"suite": load_json(path), "configs": configs},
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def default_project_directory() -> Path:
    repositories = os.environ.get("YAE_CLONED_REPOSITORIES_DIR")
    if not repositories:
        raise SmokeTestError(
            "--project-dir is required when YAE_CLONED_REPOSITORIES_DIR is not set"
        )
    return Path(repositories) / "Sunday111" / "verlet" / "main"


def prepare_config(source: Path, destination: Path, capture_directory: Path) -> list[str]:
    config = load_json(source)
    captures = config.get("captures")
    if not isinstance(captures, list) or not captures:
        raise SmokeTestError(f"Diagnostic config '{source}' must contain captures")

    filenames: list[str] = []
    for index, capture in enumerate(captures):
        if not isinstance(capture, dict) or not isinstance(capture.get("path"), str):
            raise SmokeTestError(f"Capture {index} in '{source}' has no string path")
        filename = Path(capture["path"]).name
        if not filename or filename in filenames:
            raise SmokeTestError(f"Capture path '{capture['path']}' in '{source}' is invalid or duplicated")
        filenames.append(filename)
        capture["path"] = str((capture_directory / filename).resolve())

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    return filenames


def git_output(repository: Path, arguments: list[str]) -> str:
    process = subprocess.run(
        ["git", *arguments],
        cwd=repository,
        check=False,
        capture_output=True,
        text=True,
    )
    return process.stdout.strip() if process.returncode == 0 else "unknown"


def capture(args: argparse.Namespace) -> None:
    suite_path = args.suite.resolve()
    suite_directory = suite_path.parent
    cases = load_suite(suite_path)
    project_directory = (args.project_dir or default_project_directory()).resolve()
    if not (project_directory / "yae_project.json").is_file():
        raise SmokeTestError(f"Consumer project '{project_directory}' has no yae_project.json")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "manifest.json").unlink(missing_ok=True)
    generated_configs = output / ".configs"
    manifest_cases: list[dict[str, Any]] = []

    for case in cases:
        case_output = output / case["name"]
        case_output.mkdir(parents=True, exist_ok=True)
        source_config = (suite_directory / case["config"]).resolve()
        generated_config = generated_configs / f"{case['name']}.json"
        filenames = prepare_config(source_config, generated_config, case_output)
        for filename in filenames:
            (case_output / filename).unlink(missing_ok=True)

        print(f"\n=== {case['name']} ({case['target']}) ===", flush=True)
        subprocess.run(
            [
                args.yae,
                "run",
                case["target"],
                "--",
                "--klvk-diagnostics",
                str(generated_config),
            ],
            cwd=project_directory,
            check=True,
        )
        missing = [filename for filename in filenames if not (case_output / filename).is_file()]
        if missing:
            raise SmokeTestError(f"Case '{case['name']}' did not produce: {', '.join(missing)}")
        manifest_cases.append({**case, "captures": filenames})

    repository = SUITE_DIRECTORY.parent.parent
    manifest = {
        "version": 1,
        "klvk_revision": git_output(repository, ["rev-parse", "HEAD"]),
        "klvk_dirty": bool(git_output(repository, ["status", "--porcelain"])),
        "suite": str(suite_path),
        "suite_fingerprint": suite_fingerprint(suite_path, cases),
        "cases": manifest_cases,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"\nCaptured {len(cases)} cases in {output}")


def read_ppm(path: Path) -> Image:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise SmokeTestError(f"Could not read image '{path}': {error}") from error

    header_end = data.find(b"\n255\n")
    if not data.startswith(b"P6\n") or header_end < 0:
        raise SmokeTestError(f"Image '{path}' is not a supported binary RGB PPM")
    dimensions = data[3:header_end].split()
    if len(dimensions) != 2:
        raise SmokeTestError(f"Image '{path}' has an invalid PPM header")
    try:
        width, height = (int(value) for value in dimensions)
    except ValueError as error:
        raise SmokeTestError(f"Image '{path}' has invalid dimensions") from error
    pixels = data[header_end + len(b"\n255\n") :]
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise SmokeTestError(f"Image '{path}' has an invalid pixel payload")
    return Image(width=width, height=height, pixels=pixels)


def compare_images(baseline_path: Path, candidate_path: Path, diff_path: Path) -> Comparison:
    baseline = read_ppm(baseline_path)
    candidate = read_ppm(candidate_path)
    if (baseline.width, baseline.height) != (candidate.width, candidate.height):
        raise SmokeTestError(
            f"Image dimensions differ for '{baseline_path.name}': "
            f"{baseline.width}x{baseline.height} versus {candidate.width}x{candidate.height}"
        )

    changed_pixels = 0
    maximum_delta = 0
    total_delta = 0
    diff = bytearray(len(baseline.pixels))
    for offset in range(0, len(diff), 3):
        pixel_changed = False
        for channel in range(3):
            delta = abs(baseline.pixels[offset + channel] - candidate.pixels[offset + channel])
            diff[offset + channel] = delta
            total_delta += delta
            maximum_delta = max(maximum_delta, delta)
            pixel_changed |= delta != 0
        changed_pixels += int(pixel_changed)

    diff_path.parent.mkdir(parents=True, exist_ok=True)
    diff_path.write_bytes(
        f"P6\n{baseline.width} {baseline.height}\n255\n".encode("ascii") + diff
    )
    return Comparison(
        exact=changed_pixels == 0,
        changed_pixels=changed_pixels,
        total_pixels=baseline.width * baseline.height,
        maximum_channel_delta=maximum_delta,
        mean_absolute_channel_delta=total_delta / len(diff),
        baseline_sha256=hashlib.sha256(baseline.pixels).hexdigest(),
        candidate_sha256=hashlib.sha256(candidate.pixels).hexdigest(),
    )


def validate_capture_manifest(
    directory: Path,
    cases: list[dict[str, str]],
    suite_path: Path,
) -> None:
    manifest_path = directory / "manifest.json"
    manifest = load_json(manifest_path)
    if manifest.get("version") != 1 or not isinstance(manifest.get("cases"), list):
        raise SmokeTestError(f"Capture manifest '{manifest_path}' is invalid")
    if manifest.get("suite_fingerprint") != suite_fingerprint(suite_path, cases):
        raise SmokeTestError(f"Capture manifest '{manifest_path}' was produced from a different suite definition")

    suite_directory = suite_path.parent
    expected: list[tuple[str, str, list[str]]] = []
    for case in cases:
        config = load_json((suite_directory / case["config"]).resolve())
        captures = config.get("captures")
        if not isinstance(captures, list):
            raise SmokeTestError(f"Diagnostic config for '{case['name']}' has no captures")
        expected.append(
            (
                case["name"],
                case["target"],
                [Path(capture["path"]).name for capture in captures],
            )
        )
    actual = [
        (case.get("name"), case.get("target"), case.get("captures"))
        for case in manifest["cases"]
        if isinstance(case, dict)
    ]
    if actual != expected:
        raise SmokeTestError(f"Capture manifest '{manifest_path}' does not match the selected suite")


def compare(args: argparse.Namespace) -> None:
    cases = load_suite(args.suite.resolve())
    suite_path = args.suite.resolve()
    suite_directory = suite_path.parent
    baseline = args.baseline.resolve()
    candidate = args.candidate.resolve()
    diff_output = args.diff_output.resolve()
    validate_capture_manifest(baseline, cases, suite_path)
    validate_capture_manifest(candidate, cases, suite_path)
    report_cases: list[dict[str, Any]] = []
    changed_images = 0

    print("case                     capture           pixels changed      max Δ    mean |Δ|")
    print("-----------------------  ----------------  ------------------  -------  ----------")
    for case in cases:
        config = load_json((suite_directory / case["config"]).resolve())
        captures = config.get("captures")
        if not isinstance(captures, list):
            raise SmokeTestError(f"Diagnostic config for '{case['name']}' has no captures")
        for capture_config in captures:
            filename = Path(capture_config["path"]).name
            comparison = compare_images(
                baseline / case["name"] / filename,
                candidate / case["name"] / filename,
                diff_output / case["name"] / filename,
            )
            changed_images += int(not comparison.exact)
            print(
                f"{case['name']:<23}  {filename:<16}  "
                f"{comparison.changed_pixels:>8}/{comparison.total_pixels:<9}  "
                f"{comparison.maximum_channel_delta:>7}  "
                f"{comparison.mean_absolute_channel_delta:>10.4f}"
            )
            report_cases.append(
                {
                    "case": case["name"],
                    "capture": filename,
                    **asdict(comparison),
                }
            )

    report = {
        "version": 1,
        "baseline": str(baseline),
        "candidate": str(candidate),
        "changed_images": changed_images,
        "comparisons": report_cases,
    }
    diff_output.mkdir(parents=True, exist_ok=True)
    (diff_output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"\n{len(report_cases) - changed_images}/{len(report_cases)} images match exactly.")
    print(f"Absolute RGB difference images and report: {diff_output}")
    if changed_images:
        raise SmokeTestError(f"{changed_images} smoke-test image(s) differ")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Capture and compare klvk diagnostic smoke-test images")
    parser.add_argument("--suite", type=Path, default=DEFAULT_SUITE, help="suite manifest (default: %(default)s)")
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture_parser = subparsers.add_parser("capture", help="build examples and capture this checkout")
    capture_parser.add_argument("--project-dir", type=Path, help="consumer yae project (default: verlet/main)")
    capture_parser.add_argument("--output", type=Path, required=True, help="capture output directory")
    capture_parser.add_argument("--yae", default="yae", help="yae executable (default: %(default)s)")
    capture_parser.set_defaults(function=capture)

    compare_parser = subparsers.add_parser("compare", help="compare two capture directories")
    compare_parser.add_argument("--baseline", type=Path, required=True, help="main-branch captures")
    compare_parser.add_argument("--candidate", type=Path, required=True, help="candidate-branch captures")
    compare_parser.add_argument("--diff-output", type=Path, required=True, help="difference image/report directory")
    compare_parser.set_defaults(function=compare)
    return parser


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    try:
        args.function(args)
    except (SmokeTestError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
