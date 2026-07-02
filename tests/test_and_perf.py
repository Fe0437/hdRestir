#!/usr/bin/env python3
"""Build, run all tests, ensure perf baselines, then check for regressions.

This is the orchestration behind the `test-and-perf` just recipe. Written in
Python so it runs identically under Windows (cmd.exe), macOS, and Linux with no
shell dependency. Output uses plain ASCII so it never trips a non-UTF-8 console.

Usage: test_and_perf.py [PIPELINE] [VARIANCE]
  PIPELINE  pipeline to validate          (default: RIS)
  VARIANCE  noise_rmse convergence target (default: 0.005)

Steps (stops immediately on the first failure):
  1. Build the plugin
  2. All tests (smoke + normal)
  3. Ensure performance baselines exist (no-op if already stored)
  4. Strict self-regression check + cross-pipeline comparison
"""
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = PROJECT_ROOT / "workflow.py"
PERF_TEST = PROJECT_ROOT / "tests" / "perf_test.py"
PY = sys.executable


def run(cmd):
    print(f"\n>> {' '.join(str(c) for c in cmd)}\n", flush=True)
    subprocess.run(cmd, cwd=PROJECT_ROOT, check=True)


def banner(line):
    bar = "=" * 60
    print(f"\n{bar}\n  {line}\n{bar}\n", flush=True)


def main():
    pipeline = sys.argv[1] if len(sys.argv) > 1 else "RIS"
    variance = sys.argv[2] if len(sys.argv) > 2 else "0.005"
    reference = "RIS" if pipeline == "PathTracer" else "PathTracer"

    banner(f"test-and-perf | {pipeline}  (convergence target <= {variance})")

    print("> 1/4  Building...", flush=True)
    run([PY, str(WORKFLOW)])

    print("\n> 2/4  Running all tests (smoke + normal)...", flush=True)
    run(["ctest", "--test-dir", "build", "-V"])

    print("\n> 3/4  Ensuring performance baselines exist...", flush=True)
    run([PY, str(PERF_TEST), "--ensure-store",
         "--pipelines", reference, "--target-variance", variance])
    run([PY, str(PERF_TEST), "--ensure-store",
         "--pipelines", pipeline, "--target-variance", variance])

    print(f"\n> 4/4  Performance: {pipeline} vs stored {pipeline} (regression) "
          f"+ vs {reference} (quality)...", flush=True)
    run([PY, str(PERF_TEST), "--strict",
         "--pipelines", pipeline,
         "--compare-pipeline", reference,
         "--target-variance", variance])

    banner(f"OK  test-and-perf: {pipeline} - all checks passed")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(exc.returncode)
