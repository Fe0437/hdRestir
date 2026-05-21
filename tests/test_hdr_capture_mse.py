#!/usr/bin/env python3

import argparse
import math
import re
import subprocess
import sys
from pathlib import Path


def run_command(command, check=True):
    result = subprocess.run(command, capture_output=True, text=True)
    if check and result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise subprocess.CalledProcessError(result.returncode, command, result.stdout, result.stderr)
    return result


def parse_normalized_rmse(compare_output):
    match = re.search(r"\(([0-9eE+\-.]+)\)%?", compare_output)
    if not match:
        raise RuntimeError(f"Could not parse RMSE output: {compare_output!r}")
    return float(match.group(1))


def compute_mse(reference_image, current_image):
    result = subprocess.run(
        ["magick", "compare", "-metric", "RMSE", str(reference_image), str(current_image), "null:"],
        capture_output=True,
        text=True,
    )
    if result.returncode not in {0, 1}:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise subprocess.CalledProcessError(
            result.returncode,
            result.args,
            result.stdout,
            result.stderr,
        )

    metric_output = (result.stderr or "") + (result.stdout or "")
    normalized_rmse = parse_normalized_rmse(metric_output)
    return normalized_rmse * normalized_rmse, normalized_rmse, metric_output.strip()


def main():
    parser = argparse.ArgumentParser(description="Capture a renderer frame and compare it against an HDR reference.")
    parser.add_argument("--workflow", required=True, help="Path to workflow.py")
    parser.add_argument("--scene", required=True, help="USD scene to capture")
    parser.add_argument("--reference", required=True, help="Reference HDR image")
    parser.add_argument("--output", required=True, help="Output HDR path for the captured frame")
    parser.add_argument("--threshold", required=True, type=float, help="Maximum allowed normalized MSE")
    parser.add_argument(
        "--render-setting",
        action="append",
        default=[],
        help="Additional render setting override in token=value form",
    )
    args = parser.parse_args()

    workflow_path = Path(args.workflow).resolve()
    scene_path = Path(args.scene).resolve()
    reference_path = Path(args.reference).resolve()
    output_path = Path(args.output).resolve()

    if not workflow_path.is_file():
        raise FileNotFoundError(f"workflow.py not found: {workflow_path}")
    if not scene_path.is_file():
        raise FileNotFoundError(f"Scene not found: {scene_path}")
    if not reference_path.is_file():
        raise FileNotFoundError(f"Reference image not found: {reference_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    capture_command = [sys.executable, str(workflow_path), "capture", str(scene_path), str(output_path)]
    for render_setting in args.render_setting:
        capture_command.extend(["--render-setting", render_setting])

    run_command(capture_command)
    mse, normalized_rmse, metric_output = compute_mse(reference_path, output_path)

    print(f"Reference: {reference_path}")
    print(f"Current:   {output_path}")
    print(f"Metric:    {metric_output}")
    print(f"RMSE:      {normalized_rmse:.8f}")
    print(f"MSE:       {mse:.8f}")
    print(f"Threshold: {args.threshold:.8f}")

    if math.isnan(mse) or mse > args.threshold:
        raise SystemExit(1)


if __name__ == "__main__":
    main()