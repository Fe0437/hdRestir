#!/usr/bin/env python3

import argparse
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(
        description="Create an absolute HDR difference image between two HDR renders."
    )
    parser.add_argument("reference", help="Reference HDR image")
    parser.add_argument("current", help="Current HDR image")
    parser.add_argument("output", help="Output HDR image for the absolute difference")
    args = parser.parse_args()

    reference_path = Path(args.reference).resolve()
    current_path = Path(args.current).resolve()
    output_path = Path(args.output).resolve()

    if not reference_path.is_file():
        raise FileNotFoundError(f"Reference image not found: {reference_path}")
    if not current_path.is_file():
        raise FileNotFoundError(f"Current image not found: {current_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    # HDR formats encode positive radiance values, so this stores the absolute
    # per-pixel difference magnitude as an HDR image for inspection.
    result = subprocess.run(
        [
            "magick",
            str(current_path),
            str(reference_path),
            "-compose",
            "difference",
            "-composite",
            str(output_path),
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)

    print(f"Wrote absolute HDR difference image to {output_path}")


if __name__ == "__main__":
    main()