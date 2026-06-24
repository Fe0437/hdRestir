#!/usr/bin/env python3
"""HdRestir pipeline performance tests.

Two test modes
--------------
Fixed-sample mode (default):
  Render each pipeline at a fixed sample count and compare time + convergence
  proxy.  The convergence proxy is RMSE(N_samples, N/2_samples): lower means
  less noise.  (Two renders of identical settings would be pixel-identical due
  to a fixed seed, so a simple dual-buffer approach would always give 0.)

Convergence-matching mode (--target-variance VALUE):
  Double sample count from a starting point until the convergence proxy drops
  below the target, or until a time/sample ceiling is hit.  Reports how many
  samples each pipeline needed and how long the winning render took.

References: performance_reference/{scene_stem}/{pipeline}.json
Each scene has independent baselines.

Examples
--------
  # Store baselines for every scene:
  python tests/perf_test.py --store --all-scenes

  # Compare all pipelines vs stored refs, all scenes:
  python tests/perf_test.py --all-scenes

  # Compare RIS vs PathTracer live, all scenes:
  python tests/perf_test.py --pipelines RIS --all-scenes --compare-pipeline PathTracer

  # Convergence-matching: how many samples does each pipeline need, all scenes:
  python tests/perf_test.py --all-scenes --target-variance 0.0005

  # Convergence-matching, RIS vs PathTracer, one scene:
  python tests/perf_test.py --compare-pipeline PathTracer --target-variance 0.0005
"""

import argparse
import json
import math
import re
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

# Extra render settings forwarded to every render call.  Populated from
# --render-setting KEY=VAL args in main(); do not set directly.
_extra_render_settings: dict = {}
PERF_REF_DIR = PROJECT_ROOT / "performance_reference"
EXAMPLE_SCENES_DIR = PROJECT_ROOT / "example_scenes"

PIPELINES = ["PathTracer", "RIS"]
DEFAULT_SAMPLE_COUNT = 64
DEFAULT_MIN_SAMPLES = 8
DEFAULT_MAX_SAMPLES = 32768
DEFAULT_MAX_TIME = 900  # seconds
DEFAULT_RUNS = 3        # renders per sample count for reliable timing
DEFAULT_SCENE = EXAMPLE_SCENES_DIR / "scene.usda"
DEFAULT_TMP_DIR = PROJECT_ROOT / "build" / "perf_test_captures"

# A timing delta is only reported as IMPROVED / REGRESSION if it clears both
# thresholds — the absolute one avoids flagging sub-second jitter on fast renders,
# the relative one avoids flagging large-but-proportionally-small deltas on slow ones.
TIMING_MIN_ABS_SECONDS = 1.5   # ignore if |delta| < this
TIMING_MIN_REL_PERCENT = 15.0  # ignore if |delta %| < this


# ── HDR pixel comparison (pure Python, no format delegates needed) ────────────

def _read_hdr_pixels(path: Path) -> "np.ndarray":
    """Parse a Radiance RGBE .hdr file into a float32 (H, W, 3) array.

    RGBE encodes each pixel as (R, G, B, E) bytes where the shared exponent E
    gives scale = 2^(E-136).  This reader handles both the new RLE scanline
    format (marker bytes 0x02 0x02) and the legacy uncompressed format.
    """
    import numpy as np

    with open(path, "rb") as f:
        buf = f.read()

    # Skip header: look for the resolution line that starts with '-' or '+'
    # The header ends with a blank line just before it.
    i = 0
    while i < len(buf) - 1:
        if buf[i] == ord('\n'):
            i += 1
            if buf[i] in (ord('-'), ord('+')):
                break  # found start of resolution line
        else:
            i += 1

    nl = buf.index(b'\n', i)
    parts = buf[i:nl].decode('ascii').split()
    H, W = int(parts[1]), int(parts[3])
    i = nl + 1

    raw = np.zeros((H, W, 4), dtype=np.uint8)

    for y in range(H):
        if i + 3 < len(buf) and buf[i] == 2 and buf[i + 1] == 2:
            # New RLE scanline format
            i += 4  # skip 0x02 0x02 width_hi width_lo
            for ch in range(4):
                x = 0
                while x < W:
                    code = buf[i]; i += 1
                    if code > 128:          # RLE run
                        n = code - 128
                        v = buf[i]; i += 1
                        raw[y, x:x + n, ch] = v
                        x += n
                    else:                   # literal run
                        raw[y, x:x + code, ch] = list(buf[i:i + code])
                        i += code
                        x += code
        else:
            # Uncompressed: 4 bytes per pixel
            row = np.frombuffer(buf[i:i + W * 4], dtype=np.uint8).reshape(W, 4)
            raw[y] = row
            i += W * 4

    # Decode RGBE → float32: channel_value = byte * 2^(E - 136)
    r, g, b, e = raw[..., 0], raw[..., 1], raw[..., 2], raw[..., 3]
    scale = np.where(e > 0, np.ldexp(np.float32(1.0), e.astype(np.int32) - 136), np.float32(0.0))
    return np.stack([r * scale, g * scale, b * scale], axis=-1)


def _rmse_hdr(path_a: Path, path_b: Path) -> float:
    """Compute normalised RMSE between two .hdr files using Python/numpy.

    Normalised by the maximum channel value across both images so the result
    is in [0, 1] — the same scale that `magick compare -metric RMSE` uses.
    No ImageMagick format delegate required.
    """
    import numpy as np

    a = _read_hdr_pixels(path_a).astype(np.float64)
    b = _read_hdr_pixels(path_b).astype(np.float64)
    peak = max(a.max(), b.max(), 1e-9)
    return float(np.sqrt(np.mean(((a - b) / peak) ** 2)))


# ── rendering ────────────────────────────────────────────────────────────────

def _capture(workflow_path: Path, scene_path: Path, output_path: Path, render_settings: dict) -> str:
    cmd = [sys.executable, str(workflow_path), "capture", str(scene_path), str(output_path)]
    for key, val in render_settings.items():
        cmd += ["--render-setting", f"{key}={val}"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise subprocess.CalledProcessError(result.returncode, cmd, result.stdout, result.stderr)
    return (result.stdout or "") + (result.stderr or "")


# Final AccumulationPass metrics line emitted by the renderer when built with
# METRICS_ENABLED (workflow.py now configures all builds with it).  Gives a
# float64, format-independent noise measurement — unlike the .hdr image diff,
# which is floored at ~0.004 by RGBE's 8-bit mantissa precision.
_METRICS_LINE_RE = re.compile(
    r"AccumulationPass: samples=(\d+)"
    r" mean estimator variance=([0-9.eE+\-]+)"
    r" max estimator variance=([0-9.eE+\-]+)"
    r"(?: mean luminance=([0-9.eE+\-]+))?")


def _parse_logged_noise(capture_output: str) -> "dict | None":
    """Parse the LAST AccumulationPass metrics line from a capture's output.

    Returns a dict with mean/max estimator variance and, when the renderer
    also logs mean luminance, `rel_noise` = sqrt(mean variance) / mean
    luminance — a scale-free relative noise level usable as a convergence
    target with arbitrary precision.  Returns None when the renderer was
    built without METRICS_ENABLED.
    """
    matches = _METRICS_LINE_RE.findall(capture_output or "")
    if not matches:
        return None
    samples, mean_var, max_var, mean_lum = matches[-1]
    info = {
        "samples": int(samples),
        "mean_estimator_variance": float(mean_var),
        "max_estimator_variance": float(max_var),
    }
    if mean_lum:
        lum = float(mean_lum)
        info["mean_luminance"] = lum
        if lum > 0.0:
            info["rel_noise"] = math.sqrt(info["mean_estimator_variance"]) / lum
    return info


def _compute_rmse(image_a: Path, image_b: Path) -> float:
    """ImageMagick RMSE — used for reference image comparisons (HDR only)."""
    result = subprocess.run(
        ["magick", "compare", "-metric", "RMSE", str(image_a), str(image_b), "null:"],
        capture_output=True, text=True,
    )
    if result.returncode not in {0, 1}:
        sys.stderr.write(result.stderr)
        raise subprocess.CalledProcessError(result.returncode, result.args)
    output = (result.stderr or "") + (result.stdout or "")
    match = re.search(r"\(([0-9eE+\-.]+)\)%?", output)
    if not match:
        raise RuntimeError(f"Could not parse RMSE output: {output!r}")
    return float(match.group(1))


def _base_settings(pipeline: str, sample_count: int, resolution_level: int = 1) -> dict:
    return {
        "targetSampleCount": str(sample_count),
        "enableSplitScreen": "0",
        "resolutionLevel": str(resolution_level),
        "primaryPipeline": pipeline,
        "enableFireflyFilter": "1",
        **_extra_render_settings,
    }


def _render_timed(workflow_path, scene_path, pipeline, sample_count, output_path,
                  resolution_level: int = 1, runs: int = 1):
    """Render `runs` times; return (median wall-clock seconds, last capture output).

    The output file is overwritten on each run (deterministic renders produce
    identical pixels so the last write is fine).  Median is more robust than
    mean against occasional OS scheduling spikes.
    """
    times = []
    last_output = ""
    for _ in range(runs):
        t0 = time.perf_counter()
        last_output = _capture(workflow_path, scene_path, output_path,
                               _base_settings(pipeline, sample_count, resolution_level))
        times.append(time.perf_counter() - t0)
    return statistics.median(times), last_output


# Comparison factor: compare N samples vs N//NOISE_DIVISOR samples.
# 4 gives more signal than 2 while staying fast (one extra render at 1/4 cost).
_NOISE_DIVISOR = 4


def _noise_rmse(workflow_path, scene_path, pipeline, sample_count, tmp_dir,
                timed_output=None, resolution_level: int = 1):
    """Return (noise_rmse, render_time_or_None).

    Computes RMSE between renders at `sample_count` and `sample_count // 4`
    using a pure-Python RGBE reader (no ImageMagick format delegate required).

    A larger divisor (4 vs 2) makes the sample counts more different, giving
    a stronger signal that survives the RGBE format's ~0.4% relative precision.
    Lower result = image barely changes when quadrupling samples = more converged.
    """
    slug = f"{scene_path.stem}_{pipeline}"
    out_full = timed_output or (tmp_dir / f"{slug}_full.hdr")
    out_low = tmp_dir / f"{slug}_low.hdr"

    if timed_output is None:
        render_time, _ = _render_timed(
            workflow_path, scene_path, pipeline, sample_count, out_full, resolution_level
        )
    else:
        render_time = None

    low_samples = max(1, sample_count // _NOISE_DIVISOR)
    _capture(workflow_path, scene_path, out_low,
             _base_settings(pipeline, low_samples, resolution_level))

    rmse = _rmse_hdr(out_full, out_low)
    return rmse, render_time


def _measure_pipeline(workflow_path, scene_path, pipeline, sample_count, tmp_dir,
                      resolution_level: int = 1, runs: int = DEFAULT_RUNS) -> dict:
    """Fixed-sample measurement: timed full render (median over `runs`) + noise RMSE."""
    slug = f"{scene_path.stem}_{pipeline}"
    out_full = tmp_dir / f"{slug}_full.hdr"

    render_time, capture_out = _render_timed(
        workflow_path, scene_path, pipeline, sample_count, out_full, resolution_level,
        runs=runs,
    )
    noise, _ = _noise_rmse(
        workflow_path, scene_path, pipeline, sample_count, tmp_dir,
        timed_output=out_full, resolution_level=resolution_level,
    )

    logged = _parse_logged_noise(capture_out)

    result = {
        "scene": scene_path.name,
        "pipeline": pipeline,
        "sample_count": sample_count,
        "resolution_level": resolution_level,
        "render_time_seconds": round(render_time, 3),
        "render_time_runs": runs,
        "noise_rmse": round(noise, 8),
    }
    if logged is not None:
        result["mean_estimator_variance"] = logged["mean_estimator_variance"]
        result["max_estimator_variance"] = logged["max_estimator_variance"]
        if "rel_noise" in logged:
            result["noise_rel_log"] = round(logged["rel_noise"], 10)
    return result


# ── convergence-matching mode ─────────────────────────────────────────────────

def _find_convergence(
    workflow_path, scene_path, pipeline, target_noise_rmse,
    min_samples, max_samples, max_time_seconds, tmp_dir,
    resolution_level: int = 1,
    reference_image: "Path | None" = None,
    runs: int = DEFAULT_RUNS,
) -> dict:
    """Find the minimum sample count for this pipeline to reach target_noise_rmse.

    Tracks two independent metrics at every step:

    noise — convergence TARGET, from one of two sources:
        [log]   sqrt(mean estimator variance) / mean luminance, parsed from the
                renderer's AccumulationPass metrics log.  float64, format-
                independent, arbitrary precision.  Preferred when available.
                NOTE: assumes iid samples — temporal-reuse correlation
                (reservoir) makes it optimistic; cross-check rmse_vs_ref.
        [image] RMSE(N, N/4 samples) on the .hdr outputs.  Fallback when the
                renderer lacks METRICS_ENABLED.  Floored at ~0.004 by RGBE
                8-bit mantissa precision — targets below that are unreachable.

    rmse_vs_ref = RMSE(N, reference image)  — quality vs ground truth.
        Measures how close the current render is to the stored PathTracer reference
        (2048 samples, full resolution).  For PathTracer this trends toward 0.  For
        other algorithms it converges to a non-zero floor that represents the
        algorithmic difference — this is expected and fine.
        NOT used for the convergence target, but printed at every step so you can
        see which pipeline produces a better image at the same sample count.

    Renders are cached by sample count so the N/4 comparison render is never
    produced twice (from N=32 onward it was already rendered in an earlier step).
    """
    slug = f"{scene_path.stem}_{pipeline}"
    has_ref = reference_image is not None and reference_image.is_file()

    samples = min_samples
    total_elapsed = 0.0
    last_render_time = None
    last_noise = None
    last_rmse_ref = None
    noise_source = None
    render_cache: dict[int, Path] = {}
    curve = []

    def get_render(n: int) -> Path:
        if n not in render_cache:
            out = tmp_dir / f"{slug}_conv{n}.hdr"
            _capture(workflow_path, scene_path, out,
                     _base_settings(pipeline, n, resolution_level))
            render_cache[n] = out
        return render_cache[n]

    while samples <= max_samples:
        # Exploration render — single pass.  Only the winner gets re-timed with
        # the full `runs` count so the search itself stays fast.
        out_full = tmp_dir / f"{slug}_conv{samples}.hdr"
        t0 = time.perf_counter()
        capture_out = _capture(workflow_path, scene_path, out_full,
                               _base_settings(pipeline, samples, resolution_level))
        render_time = time.perf_counter() - t0
        render_cache[samples] = out_full

        total_elapsed += render_time
        last_render_time = render_time

        # Prefer the renderer's logged estimator variance (float64, no file
        # format quantisation floor).  Fall back to the image-diff proxy when
        # the renderer was built without METRICS_ENABLED — note that proxy is
        # floored at ~0.004 by RGBE precision, so small targets are
        # unreachable with it.
        logged = _parse_logged_noise(capture_out)
        if logged is not None and "rel_noise" in logged:
            noise = logged["rel_noise"]
            noise_source = "log"
        else:
            low_n = max(1, samples // _NOISE_DIVISOR)
            noise = _rmse_hdr(out_full, get_render(low_n))
            noise_source = "image"
        last_noise = noise

        rmse_ref = _rmse_hdr(out_full, reference_image) if has_ref else None
        last_rmse_ref = rmse_ref

        point = {
            "samples": samples,
            "noise_rmse": round(noise, 10),
            "noise_source": noise_source,
            "render_time_seconds": round(render_time, 3),
            "cumulative_time_seconds": round(total_elapsed, 3),
        }
        if logged is not None:
            point["mean_estimator_variance"] = logged["mean_estimator_variance"]
        if rmse_ref is not None:
            point["rmse_vs_ref"] = round(rmse_ref, 8)
        curve.append(point)

        ref_str = f"  rmse_vs_ref={rmse_ref:.6f}" if rmse_ref is not None else ""
        print(f"      samples={samples:5d}  noise[{noise_source}]={noise:.8f}{ref_str}  render={render_time:.2f}s")

        if noise <= target_noise_rmse:
            median_time = _retimed(workflow_path, scene_path, out_full,
                                   pipeline, samples, resolution_level, render_time, runs)
            if runs > 1:
                print(f"      → timing confirmed over {runs} runs: median={median_time:.2f}s")
            result = {
                "samples_needed": samples,
                "render_time_seconds": round(median_time, 3),
                "render_time_runs": runs,
                "final_noise_rmse": round(noise, 10),
                "noise_source": noise_source,
                "exceeded": False,
                "curve": curve,
            }
            if rmse_ref is not None:
                result["final_rmse_vs_ref"] = round(rmse_ref, 8)
            return result

        if total_elapsed >= max_time_seconds:
            result = {
                "samples_needed": None,
                "render_time_seconds": round(last_render_time, 3),
                "render_time_runs": 1,
                "final_noise_rmse": round(last_noise, 10),
                "noise_source": noise_source,
                "exceeded": True,
                "exceeded_reason": f"time budget ({max_time_seconds}s) — best noise[{noise_source}]: {last_noise:.8f} at {samples} samples",
                "curve": curve,
            }
            if last_rmse_ref is not None:
                result["final_rmse_vs_ref"] = round(last_rmse_ref, 8)
            return result

        samples *= 2

    result = {
        "samples_needed": None,
        "render_time_seconds": round(last_render_time, 3) if last_render_time else None,
        "render_time_runs": 1,
        "final_noise_rmse": round(last_noise, 10) if last_noise is not None else None,
        "noise_source": noise_source,
        "exceeded": True,
        "exceeded_reason": f"sample ceiling ({max_samples}) — best noise[{noise_source}]: {last_noise:.8f} at {samples // 2} samples",
        "curve": curve,
    }
    if last_rmse_ref is not None:
        result["final_rmse_vs_ref"] = round(last_rmse_ref, 8)
    return result


def _retimed(workflow_path, scene_path, output_path, pipeline, samples,
             resolution_level, first_time, runs):
    """Re-render `runs-1` more times and return the median across all `runs` timings."""
    if runs <= 1:
        return first_time
    extra = []
    for _ in range(runs - 1):
        t0 = time.perf_counter()
        _capture(workflow_path, scene_path, output_path,
                 _base_settings(pipeline, samples, resolution_level))
        extra.append(time.perf_counter() - t0)
    return statistics.median([first_time] + extra)


# ── reference store ──────────────────────────────────────────────────────────

def _ref_path(scene_path: Path, pipeline: str) -> Path:
    return PERF_REF_DIR / scene_path.stem / f"{pipeline}.json"


def _load_reference(scene_path: Path, pipeline: str) -> "dict | None":
    ref_file = _ref_path(scene_path, pipeline)
    if not ref_file.exists():
        return None
    with open(ref_file) as f:
        return json.load(f)


def _load_for_compare(scene_path: Path, pipeline: str,
                      convergence_mode: bool, args) -> "dict | None":
    """Load stored baseline for pipeline if it is compatible with the current run.

    Returns the stored dict (tagged with from_store=True) or None if no
    compatible baseline exists.  Compatibility means: same mode, same resolution
    level, and — for fixed-sample — same sample count, for convergence — same
    target variance.
    """
    ref = _load_reference(scene_path, pipeline)
    if ref is None:
        return None
    if convergence_mode:
        if ref.get("mode") != "convergence":
            return None
        if ref.get("target_variance") != args.target_variance:
            return None
        if ref.get("resolution_level") != args.resolution_level:
            return None
    else:
        if ref.get("mode", "fixed") != "fixed":
            return None
        if ref.get("sample_count") != args.sample_count:
            return None
        if ref.get("resolution_level") != args.resolution_level:
            return None
    return {**ref, "from_store": True}


def _store_reference(scene_path: Path, pipeline: str, result: dict):
    ref_file = _ref_path(scene_path, pipeline)
    ref_file.parent.mkdir(parents=True, exist_ok=True)
    data = {**result, "stored_at": datetime.now(timezone.utc).isoformat()}
    with open(ref_file, "w") as f:
        json.dump(data, f, indent=2)
    print(f"  Stored → {ref_file.relative_to(PROJECT_ROOT)}")


def _discover_stored_scenes(pipelines: list) -> list:
    found = set()
    for pipeline in pipelines:
        for ref_file in sorted(PERF_REF_DIR.glob(f"*/{pipeline}.json")):
            candidates = list(EXAMPLE_SCENES_DIR.glob(f"{ref_file.parent.name}.usda"))
            if candidates:
                found.add(candidates[0])
    return sorted(found, key=lambda p: p.name)


# ── reporting ─────────────────────────────────────────────────────────────────

def _pct_change(current: float, reference: float) -> str:
    if reference == 0:
        return "∞% REGRESSED" if current > 0 else "→ UNCHANGED"
    pct = (current - reference) / reference * 100
    arrow = "↓" if pct < 0 else ("↑" if pct > 0 else "→")
    label = "IMPROVED" if pct < 0 else ("REGRESSED" if pct > 0 else "UNCHANGED")
    return f"{arrow}{abs(pct):.1f}% {label}"


_NOISE_RMSE_LEGEND = """
  📊 How to read the metrics
  ──────────────────────────────────────────────────────────────────────────────
  noise_rmse = RMSE( N samples,  N/4 samples )           ← convergence speed

      How much the image still changes when you quadruple the sample count.
      Approaches 0 as the pipeline converges — used as the convergence TARGET.
      Because every pipeline is compared against its own previous render, this
      is a fair apples-to-apples speed comparison across different algorithms.

  rmse_vs_ref = RMSE( pipeline@N,  PathTracer@2048 [fixed] )  ← image quality

      The reference is a single fixed image — PathTracer rendered at 2048 samples,
      stored on disk.  It does not change during the loop.

      At every step N both pipelines are measured against that same fixed image.
      This lets you answer: "at 64 samples, is RIS or PathTracer closer to the
      ground truth?"  The one with the lower value is producing a more accurate
      image at that sample cost — regardless of what noise_rmse says.

  Timing is the median of {runs} renders.  A Δ is only flagged as IMPROVED or
  REGRESSION when it clears both thresholds: ≥{abs_s}s absolute and ≥{rel}%
  relative — smaller deltas are reported as "within measurement noise".
  ──────────────────────────────────────────────────────────────────────────────"""


def _noise_rmse_label(value: float) -> str:
    s = f"{value:.6f}"
    if value == 0.0:
        s += "  (converged — adding samples produces no measurable change)"
    return s


def _report_vs_reference(scene_path, pipeline, current, reference) -> bool:
    time_regressed = current["render_time_seconds"] > reference["render_time_seconds"]
    noise_regressed = current["noise_rmse"] > reference.get("noise_rmse", 0)

    print(f"\n  Scene / Pipeline : {scene_path.name} / {pipeline}")
    print(f"    Render time  : {current['render_time_seconds']:.3f}s"
          f"  (ref {reference['render_time_seconds']:.3f}s"
          f"  {_pct_change(current['render_time_seconds'], reference['render_time_seconds'])})")
    print(f"    Noise RMSE   : {_noise_rmse_label(current['noise_rmse'])}"
          f"\n                   (ref {reference.get('noise_rmse', 0):.6f}"
          f"  {_pct_change(current['noise_rmse'], reference.get('noise_rmse', 0))})")
    print(f"    Samples      : {current['sample_count']}"
          + (f"  |  ref stored: {reference['stored_at'][:10]}" if reference.get("stored_at") else ""))
    return not (time_regressed or noise_regressed)


def _report_no_reference(scene_path, pipeline, current):
    print(f"\n  Scene / Pipeline : {scene_path.name} / {pipeline}  [no reference — run with --store]")
    print(f"    Render time  : {current['render_time_seconds']:.3f}s")
    print(f"    Noise RMSE   : {_noise_rmse_label(current['noise_rmse'])}")
    print(f"    Samples      : {current['sample_count']}")


def _report_cross_pipeline(scene_path, pipeline_a, pipeline_b, result_a, result_b):
    print(f"\n  Scene : {scene_path.name}  |  {pipeline_a} vs {pipeline_b}")
    print(f"    Render time  : {pipeline_a} {result_a['render_time_seconds']:.3f}s"
          f"  vs  {pipeline_b} {result_b['render_time_seconds']:.3f}s"
          f"  →  {_pct_change(result_a['render_time_seconds'], result_b['render_time_seconds'])}")
    print(f"    Noise RMSE   : {pipeline_a} {result_a['noise_rmse']:.6f}"
          f"  vs  {pipeline_b} {result_b['noise_rmse']:.6f}"
          f"  →  {_pct_change(result_a['noise_rmse'], result_b['noise_rmse'])}")
    print(f"    Samples      : {result_a['sample_count']}")
    if result_a["noise_rmse"] == 0.0 and result_b["noise_rmse"] == 0.0:
        print(f"    Note: both pipelines are fully converged at these settings — "
              f"render time is the only differentiator.")


def _report_convergence_cross(scene_path, pipeline_a, pipeline_b, result_a, result_b, target):
    def fmt(r):
        src = " 📦 stored" if r.get("from_store") else ""
        noise_label = f"noise[{r.get('noise_source', 'image')}]"
        ref_str = (f"  rmse_vs_ref={r['final_rmse_vs_ref']:.6f}"
                   if r.get("final_rmse_vs_ref") is not None else "")
        if r["exceeded"]:
            reason = r.get("exceeded_reason", "limit reached")
            noise = r["final_noise_rmse"]
            return f"⚠️  did not converge — {reason}  {noise_label}={noise:.8f}{ref_str}{src}"
        return (f"✓  converged at {r['samples_needed']} samples  ({r['render_time_seconds']:.1f}s)"
                f"  {noise_label}={r['final_noise_rmse']:.8f}{ref_str}{src}")

    print(f"\n  Scene : {scene_path.name}  |  {pipeline_a} vs {pipeline_b}"
          f"  (target noise ≤ {target})")
    if result_a.get("noise_source") != result_b.get("noise_source"):
        print(f"    ⚠  noise metrics differ ({result_a.get('noise_source')} vs "
              f"{result_b.get('noise_source')}) — values are NOT comparable; "
              f"rebuild with metrics or re-store the baseline")
    print(f"    {pipeline_a:<24}: {fmt(result_a)}")
    print(f"    {pipeline_b:<24}: {fmt(result_b)}")

    converged_a = not result_a["exceeded"]
    converged_b = not result_b["exceeded"]

    if converged_a and converged_b:
        sample_ratio = result_b["samples_needed"] / result_a["samples_needed"]
        time_ratio = result_b["render_time_seconds"] / result_a["render_time_seconds"]
        if sample_ratio > 1:
            print(f"    🏆 {pipeline_a} converges {sample_ratio:.1f}× faster in samples"
                  f"  ({time_ratio:.1f}× faster wall time)")
        elif sample_ratio < 1:
            print(f"    🏆 {pipeline_b} converges {1/sample_ratio:.1f}× faster in samples"
                  f"  ({1/time_ratio:.1f}× faster wall time)")
        else:
            print(f"    → Both pipelines converge at the same sample count")

        ref_a = result_a.get("final_rmse_vs_ref")
        ref_b = result_b.get("final_rmse_vs_ref")
        if ref_a is not None and ref_b is not None:
            if ref_a < ref_b:
                print(f"    📐 At convergence, {pipeline_a} is closer to reference"
                      f"  ({ref_a:.6f} vs {ref_b:.6f})")
            elif ref_b < ref_a:
                print(f"    📐 At convergence, {pipeline_b} is closer to reference"
                      f"  ({ref_b:.6f} vs {ref_a:.6f})")
            else:
                print(f"    📐 Both pipelines are equally close to reference at convergence")


def _report_convergence_vs_reference(scene_path, pipeline, current, reference) -> bool:
    """Compare today's convergence run against the stored baseline for the same pipeline.

    This is a regression test: run perf-store to save a baseline, then run
    perf-test after making changes to see if the algorithm got faster or slower.
    """
    def fmt(r, settings=None):
        noise_label = f"noise[{r.get('noise_source', 'image')}]"
        ref_str = (f"  rmse_vs_ref={r['final_rmse_vs_ref']:.6f}"
                   if r.get("final_rmse_vs_ref") is not None else "")
        date_str = (f"  [{r['stored_at'][:10]}]" if r.get("stored_at") else "")
        settings_str = ""
        if settings:
            settings_str = "  [" + " ".join(f"{k}={v}" for k, v in settings.items()) + "]"
        if r.get("exceeded"):
            reason = r.get("exceeded_reason", "limit reached")
            noise = r.get("final_noise_rmse")
            noise_str = f"  {noise_label}={noise:.8f}" if noise is not None else ""
            return f"⚠  did not converge ({reason}){noise_str}{ref_str}{date_str}{settings_str}"
        return (f"✓  converged at {r['samples_needed']:>6} samples  ({r['render_time_seconds']:5.1f}s)"
                f"  {noise_label}={r['final_noise_rmse']:.8f}{ref_str}{date_str}{settings_str}")

    cur_converged = not current.get("exceeded", True)
    ref_converged = not reference.get("exceeded", True)
    cur_samples   = current.get("samples_needed")
    ref_samples   = reference.get("samples_needed")
    cur_src = current.get("noise_source", "image")
    ref_src = reference.get("noise_source", "image")

    print(f"\n  ┌─ {scene_path.name} / {pipeline}")
    print(f"  │  Today    : {fmt(current, settings=_extra_render_settings or None)}")
    print(f"  │  Baseline : {fmt(reference, settings=reference.get('render_settings') or None)}")
    if cur_src != ref_src:
        print(f"  │  ⚠  noise metric changed ({ref_src} → {cur_src}); noise values "
              f"are not comparable — run 'just perf-store {pipeline}' to refresh "
              f"the baseline")
    print(f"  │")

    # ── verdict ──────────────────────────────────────────────────────────────
    passed = True

    if cur_converged and not ref_converged:
        # Old code never converged; new code does → clear improvement
        print(f"  │  ✅  IMPROVED — now converges (baseline did not)")
        print(f"  └─    Run 'just perf-store {pipeline}' to update the baseline.")

    elif not cur_converged and ref_converged:
        # Old code converged; new code doesn't → regression
        print(f"  │  ❌  REGRESSION — no longer converges (baseline did)")
        passed = False

    elif not cur_converged and not ref_converged:
        # Neither converged — compare best noise reached (only meaningful when
        # both runs used the same noise metric)
        if cur_src != ref_src:
            print(f"  └─  →  SKIPPED — noise metric changed; store a new baseline")
            return passed
        cur_noise = current.get("final_noise_rmse") or float("inf")
        ref_noise = reference.get("final_noise_rmse") or float("inf")
        delta = (cur_noise - ref_noise) / ref_noise * 100 if ref_noise else 0
        if cur_noise < ref_noise * 0.95:
            print(f"  │  ✅  IMPROVED — lower noise at limit  (Δ{delta:+.1f}%)")
            print(f"  └─    Run 'just perf-store {pipeline}' to update the baseline.")
        elif cur_noise > ref_noise * 1.05:
            print(f"  │  ❌  REGRESSION — higher noise at limit  (Δ{delta:+.1f}%)")
            passed = False
        else:
            print(f"  └─  →  UNCHANGED — both hit the limit; noise within 5%")

    else:
        # Both converged — compare sample count (primary) and wall time (secondary)
        if cur_samples < ref_samples:
            sx = ref_samples / cur_samples
            tx = reference["render_time_seconds"] / current["render_time_seconds"]
            print(f"  │  ✅  IMPROVED — {sx:.1f}× fewer samples needed  ({tx:.1f}× faster wall time)")
            print(f"  └─    Run 'just perf-store {pipeline}' to update the baseline.")
        elif cur_samples > ref_samples:
            sx = cur_samples / ref_samples
            tx = current["render_time_seconds"] / reference["render_time_seconds"]
            print(f"  └─  ❌  REGRESSION — {sx:.1f}× more samples needed  ({tx:.1f}× slower wall time)")
            passed = False
        else:
            # Same sample count — look at wall time, but only call it meaningful
            # if the delta clears both the absolute and relative noise thresholds.
            time_diff = current["render_time_seconds"] - reference["render_time_seconds"]
            time_pct  = time_diff / reference["render_time_seconds"] * 100
            meaningful = abs(time_diff) >= TIMING_MIN_ABS_SECONDS \
                         and abs(time_pct) >= TIMING_MIN_REL_PERCENT
            arrow = "↑" if time_pct > 0 else "↓"
            runs_note = (f"  (median of {current.get('render_time_runs', 1)} runs)"
                         if current.get("render_time_runs", 1) > 1 else "")
            if not meaningful:
                print(f"  └─  →  UNCHANGED — same convergence point;"
                      f" timing Δ={arrow}{abs(time_pct):.1f}% ({abs(time_diff):.2f}s)"
                      f" is within measurement noise{runs_note}")
                passed = True
            elif time_diff > 0:
                print(f"  └─  ❌  REGRESSION — same convergence point but"
                      f" {arrow}{abs(time_pct):.1f}% slower ({abs(time_diff):.2f}s){runs_note}")
                passed = False
            else:
                print(f"  │  ✅  IMPROVED — same convergence point;"
                      f" {arrow}{abs(time_pct):.1f}% faster ({abs(time_diff):.2f}s){runs_note}")
                print(f"  └─    Run 'just perf-store {pipeline}' to update the baseline.")
                passed = True

    return passed
    if not current.get("exceeded") and ref_samples:
        ratio = cur_samples / ref_samples
        arrow = "↓" if ratio < 1 else ("↑" if ratio > 1 else "→")
        label = "IMPROVED" if ratio < 1 else ("REGRESSED" if ratio > 1 else "UNCHANGED")
        print(f"    Δ samples: {arrow}{abs(1 - ratio) * 100:.1f}% {label}")

    return not has_regression


# ── convergence graph ─────────────────────────────────────────────────────────

def _plot_convergence_graph(series: list, target_noise: "float | None", output_path: Path):
    """Save a convergence-speed PNG: x = cumulative render time, y = noise."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("  ⚠️  matplotlib not installed — run: pip install matplotlib")
        return

    fig, ax = plt.subplots(figsize=(11, 6))
    colors = plt.rcParams["axes.prop_cycle"].by_key()["color"]

    for idx, s in enumerate(series):
        curve = s.get("curve", [])
        if not curve:
            continue
        x, y = [], []
        acc = 0.0
        for pt in curve:
            if "cumulative_time_seconds" in pt:
                acc = pt["cumulative_time_seconds"]
            else:
                acc += pt["render_time_seconds"]
            x.append(acc)
            y.append(pt["noise_rmse"])

        color = colors[idx % len(colors)]
        linestyle = "--" if s.get("from_store") else "-"
        ax.plot(x, y, linestyle=linestyle, marker="o", markersize=5,
                color=color, label=s["label"], linewidth=1.8)

    if target_noise is not None:
        ax.axhline(y=target_noise, color="gray", linestyle=":", linewidth=1.2,
                   label=f"target  ({target_noise})")

    # Subtitle: for each parameter that differs across series, show "key: A vs B vs ..."
    all_settings = [s.get("settings_raw", {}) for s in series]
    all_keys = {k for d in all_settings for k in d}
    differing = sorted(k for k in all_keys
                       if len({d.get(k) for d in all_settings}) > 1)
    if differing:
        subtitle = "  |  ".join(
            f"{k}: {' vs '.join(str(d.get(k, '?')) for d in all_settings)}"
            for k in differing
        )
    else:
        subtitle = ""

    ax.set_xlabel("Cumulative render time  (s)", fontsize=11)
    ax.set_ylabel("Noise  (lower = more converged)", fontsize=11)
    ax.set_yscale("log")
    if subtitle:
        ax.set_title(f"Convergence Speed\n{subtitle}", fontsize=11, fontweight="bold")
    else:
        ax.set_title("Convergence Speed", fontsize=13, fontweight="bold")
    ax.legend(fontsize=8, framealpha=0.8, loc="upper right")
    ax.grid(True, which="both", alpha=0.25)
    fig.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=150)
    print(f"\n  Graph saved → {output_path}")
    plt.close(fig)


# ── scene resolution ──────────────────────────────────────────────────────────

def _resolve_scenes(args) -> list:
    if args.all_scenes:
        if args.store or args.ensure_store or args.compare_pipeline:
            scenes = sorted(EXAMPLE_SCENES_DIR.glob("*.usda"))
            if not scenes:
                sys.exit(f"No .usda files found in {EXAMPLE_SCENES_DIR}")
            return scenes
        pipelines_to_check = list(args.pipelines)
        scenes = _discover_stored_scenes(pipelines_to_check)
        if not scenes:
            sys.exit("No stored references found for the requested pipelines. Run with --store first.")
        return scenes
    return [Path(args.scene).resolve()]


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="HdRestir pipeline performance tests",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--workflow", default=str(PROJECT_ROOT / "workflow.py"))
    parser.add_argument("--scene", default=str(DEFAULT_SCENE),
                        help="USD scene to render (ignored with --all-scenes)")
    parser.add_argument("--all-scenes", action="store_true",
                        help="Run for every .usda in example_scenes/")
    parser.add_argument("--sample-count", type=int, default=DEFAULT_SAMPLE_COUNT,
                        help="Fixed sample count (default: %(default)s, ignored in convergence mode)")
    parser.add_argument("--pipelines", nargs="+", default=PIPELINES, choices=PIPELINES,
                        help="Pipeline(s) to test (default: all)")
    parser.add_argument("--compare-pipeline", choices=PIPELINES, metavar="PIPELINE",
                        help="Compare --pipelines against this pipeline live. Choices: " + ", ".join(PIPELINES))
    parser.add_argument("--resolution-level", type=int, default=1,
                        help="Render resolution level: 0=full, 1=half, 2=quarter (default: %(default)s). "
                             "Lower = more pixels = more noise signal but slower renders.")
    parser.add_argument("--target-variance", type=float, metavar="VALUE",
                        help="Convergence mode: find the minimum sample count where "
                             "RMSE(N, N/4 samples) ≤ VALUE.")
    parser.add_argument("--reference-image",
                        default=str(PROJECT_ROOT / "images" / "reference" / "internal_scene_reference.hdr"),
                        help="Ground-truth reference image for the rmse_vs_ref quality metric "
                             "(default: stored PathTracer 2048-sample reference).  Optional — "
                             "if the file is missing, rmse_vs_ref is skipped and only noise_rmse "
                             "is reported.")
    parser.add_argument("--min-samples", type=int, default=DEFAULT_MIN_SAMPLES,
                        help="Starting sample count for convergence mode (default: %(default)s)")
    parser.add_argument("--max-samples", type=int, default=DEFAULT_MAX_SAMPLES,
                        help="Sample ceiling for convergence search (default: %(default)s)")
    parser.add_argument("--max-time", type=float, default=DEFAULT_MAX_TIME,
                        help="Time ceiling in seconds for convergence mode (default: %(default)s)")
    parser.add_argument("--runs", type=int, default=DEFAULT_RUNS,
                        help=f"Number of renders for timing (median is reported). "
                             f"In convergence mode only the winning sample count is re-timed; "
                             f"exploration renders use a single pass.  (default: %(default)s)")
    parser.add_argument("--store", action="store_true",
                        help="Store results as new reference (fixed-sample mode only)")
    parser.add_argument("--ensure-store", dest="ensure_store", action="store_true",
                        help="For each pipeline, if no compatible stored baseline exists "
                             "run convergence and store it. No-op when a baseline already "
                             "exists. Requires --target-variance.")
    parser.add_argument("--strict", action="store_true",
                        help="Exit 1 if any regression detected")
    parser.add_argument("--render-setting", action="append", metavar="KEY=VAL",
                        dest="render_setting", default=[],
                        help="Extra render setting forwarded to every render (repeatable). "
                             "Example: --render-setting risUseReservoir=0")
    parser.add_argument("--graph-output", metavar="PATH", nargs="?", const="",
                        help="Save a convergence graph image (PNG/SVG). "
                             "Omit PATH to auto-name from the differing settings. "
                             "Requires matplotlib.  Only meaningful with --target-variance.")
    parser.add_argument("--tmp-dir", default=str(DEFAULT_TMP_DIR))
    args = parser.parse_args()

    global _extra_render_settings
    _extra_render_settings = dict(s.split("=", 1) for s in args.render_setting)

    if args.store and args.compare_pipeline:
        sys.exit("--store and --compare-pipeline are mutually exclusive.")
    if args.ensure_store and (args.store or args.compare_pipeline):
        sys.exit("--ensure-store is mutually exclusive with --store and --compare-pipeline.")
    if args.ensure_store and not args.target_variance:
        sys.exit("--ensure-store requires --target-variance.")

    reference_image = Path(args.reference_image).resolve() if args.reference_image else None
    if reference_image and not reference_image.is_file():
        print(f"  ℹ️  Reference image not found: {reference_image.name}")
        print(f"     rmse_vs_ref will not be computed.  Run 'just capture-internal-reference' to generate it.")
        reference_image = None

    workflow_path = Path(args.workflow).resolve()
    tmp_dir = Path(args.tmp_dir)
    tmp_dir.mkdir(parents=True, exist_ok=True)

    if not workflow_path.is_file():
        sys.exit(f"workflow.py not found: {workflow_path}")

    scenes = _resolve_scenes(args)
    convergence_mode = args.target_variance is not None

    res_label = ["full", "half (1/2)", "quarter (1/4)", "1/8", "1/16"][min(args.resolution_level, 4)]
    print(f"\n=== HdRestir Performance Tests ===")
    print(f"Scenes     : {', '.join(s.name for s in scenes)}")
    print(f"Resolution : level {args.resolution_level} ({res_label})")

    if args.store:
        print("Mode       : 📦 store — saving current timings as the new baseline")
    elif args.ensure_store:
        tested = ', '.join(args.pipelines)
        print(f"Mode       : 📦 ensure-store — [{tested}] | create baseline only if missing")
        print(f"             target noise_rmse ≤ {args.target_variance}")
    elif convergence_mode and args.compare_pipeline:
        tested = ', '.join(args.pipelines)
        ref_note = reference_image.name if reference_image else "not available — rmse_vs_ref skipped"
        print(f"Mode       : 🔬 convergence — doubling samples until noise ≤ {args.target_variance}")
        print(f"             Testing [{tested}]; comparing vs stored {args.compare_pipeline} baseline")
        print(f"Metrics    : noise[log]=sqrt(est.variance)/mean lum (fallback noise[image]=RMSE(N,N/4), floor ~0.004)")
        print(f"             + rmse_vs_ref=RMSE(N,reference) [quality/bias check]")
        print(f"Reference  : {ref_note}")
        print(f"Samples    : {args.min_samples}–{args.max_samples}, doubling each step (time cap: {args.max_time}s, ~{args.max_time//60}min)")
    elif convergence_mode:
        tested = ', '.join(args.pipelines)
        ref_note = reference_image.name if reference_image else "not available — rmse_vs_ref skipped"
        print(f"Mode       : 🔬 convergence — doubling samples until noise ≤ {args.target_variance}")
        print(f"             Testing [{tested}]")
        print(f"Metrics    : noise[log]=sqrt(est.variance)/mean lum (fallback noise[image]=RMSE(N,N/4), floor ~0.004)")
        print(f"             + rmse_vs_ref=RMSE(N,reference) [quality/bias check]")
        print(f"Reference  : {ref_note}")
        print(f"Samples    : {args.min_samples}–{args.max_samples}, doubling each step (time cap: {args.max_time}s, ~{args.max_time//60}min)")
    elif args.compare_pipeline:
        tested = ', '.join(args.pipelines)
        print(f"Mode       : ⚖️  live compare — rendering [{tested}] and {args.compare_pipeline} at")
        print(f"             {args.sample_count} samples each, then comparing time and noise side-by-side")
        print(f"Samples    : {args.sample_count} (fixed)")
    else:
        strict_note = " — ❌ exits with error on regression" if args.strict else " — 📋 read-only, never fails"
        print(f"Mode       : 📊 fixed-sample compare vs stored baseline{strict_note}")
        print(f"Samples    : {args.sample_count} (fixed)")

    print(_NOISE_RMSE_LEGEND.format(
        runs=args.runs,
        abs_s=TIMING_MIN_ABS_SECONDS,
        rel=int(TIMING_MIN_REL_PERCENT),
    ))

    any_regression = False
    graph_series: list = []

    for scene_path in scenes:
        if not scene_path.is_file():
            print(f"\n[SKIP] Scene not found: {scene_path}")
            continue

        if args.ensure_store:
            print(f"\n[{scene_path.stem}] Checking baselines...")
            for pipeline in args.pipelines:
                existing = _load_for_compare(scene_path, pipeline, convergence_mode=True, args=args)
                if existing is not None:
                    date = existing.get("stored_at", "?")[:10]
                    print(f"  ✓ {pipeline}: compatible baseline exists (stored {date}) — skipping")
                else:
                    stale = _load_reference(scene_path, pipeline)
                    if stale is not None:
                        print(f"  ⚠️  {pipeline}: incompatible baseline found — creating new one...")
                    else:
                        print(f"  📦 {pipeline}: no baseline found — running convergence...")
                    result = _find_convergence(
                        workflow_path, scene_path, pipeline, args.target_variance,
                        args.min_samples, args.max_samples, args.max_time, tmp_dir,
                        resolution_level=args.resolution_level,
                        reference_image=reference_image,
                        runs=args.runs,
                    )
                    to_store = {**result,
                                "mode": "convergence",
                                "target_variance": args.target_variance,
                                "resolution_level": args.resolution_level,
                                "render_settings": _extra_render_settings,
                                "reference_image": reference_image.name if reference_image else None}
                    _store_reference(scene_path, pipeline, to_store)
                    print(f"  ✓ {pipeline}: baseline stored")
            continue

        pipelines_to_render = list(args.pipelines)
        measured: dict = {}

        if args.compare_pipeline and args.compare_pipeline not in pipelines_to_render:
            stored = _load_for_compare(scene_path, args.compare_pipeline, convergence_mode, args)
            if stored is not None:
                measured[args.compare_pipeline] = stored
                date = stored.get("stored_at", "?")[:10]
                print(f"\n  📦 {args.compare_pipeline} baseline loaded from store (saved {date}) — skipping re-render")
            else:
                pipelines_to_render.append(args.compare_pipeline)
                print(f"\n  ⚠️  No compatible stored baseline for '{args.compare_pipeline}' — rendering live.")
                print(f"     Run 'just perf-store' to save it and skip this render in the future.")

        for pipeline in pipelines_to_render:
            if convergence_mode:
                print(f"\n[{scene_path.stem} / {pipeline}] Searching for convergence...")
                measured[pipeline] = _find_convergence(
                    workflow_path, scene_path, pipeline, args.target_variance,
                    args.min_samples, args.max_samples, args.max_time, tmp_dir,
                    resolution_level=args.resolution_level,
                    reference_image=reference_image,
                    runs=args.runs,
                )
            else:
                print(f"\n[{scene_path.stem} / {pipeline}] Measuring...")
                measured[pipeline] = _measure_pipeline(
                    workflow_path, scene_path, pipeline, args.sample_count, tmp_dir,
                    resolution_level=args.resolution_level,
                    runs=args.runs,
                )

        # ── reporting ────────────────────────────────────────────────────────
        if args.store:
            for pipeline in args.pipelines:
                result = measured[pipeline]
                if convergence_mode:
                    result = {**result,
                              "mode": "convergence",
                              "target_variance": args.target_variance,
                              "resolution_level": args.resolution_level,
                              "render_settings": _extra_render_settings,
                              "reference_image": reference_image.name if reference_image else None}
                else:
                    result = {**result, "mode": "fixed", "render_settings": _extra_render_settings}
                _store_reference(scene_path, pipeline, result)

        elif convergence_mode and args.compare_pipeline:
            # Self-regression: each tested pipeline vs its own stored baseline.
            # When the tested pipeline IS the compare pipeline (e.g.
            # `--pipelines RIS --compare-pipeline RIS` with different
            # --render-setting values), this is the only meaningful comparison,
            # so it must not be skipped — previously this case printed nothing.
            for pipeline in args.pipelines:
                baseline = _load_for_compare(scene_path, pipeline, convergence_mode=True, args=args)
                if baseline is not None:
                    passed = _report_convergence_vs_reference(scene_path, pipeline, measured[pipeline], baseline)
                    if not passed:
                        any_regression = True
                else:
                    r = measured[pipeline]
                    print(f"\n  ┌─ {scene_path.name} / {pipeline} vs stored baseline")
                    if r.get("exceeded"):
                        print(f"  │  Today : ⚠  did not converge ({r.get('exceeded_reason', '?')})"
                              f"  noise_rmse={r.get('final_noise_rmse', 0.0):.6f}")
                    else:
                        print(f"  │  Today : ✓  converged at {r['samples_needed']} samples"
                              f"  ({r['render_time_seconds']:.1f}s)"
                              f"  noise_rmse={r['final_noise_rmse']:.6f}")
                    print(f"  └─  ℹ️  No compatible baseline — run"
                          f" 'just perf-store {pipeline} \"\" {args.target_variance}' to save one.")

            # Cross-pipeline quality comparison
            ref_result = measured[args.compare_pipeline]
            for pipeline in args.pipelines:
                if pipeline != args.compare_pipeline:
                    _report_convergence_cross(
                        scene_path, pipeline, args.compare_pipeline,
                        measured[pipeline], ref_result, args.target_variance,
                    )

        elif convergence_mode:
            for pipeline in args.pipelines:
                baseline = _load_for_compare(scene_path, pipeline, convergence_mode=True, args=args)
                if baseline is None:
                    # No compatible baseline — show current result and hint how to store one.
                    stale = _load_reference(scene_path, pipeline)
                    r = measured[pipeline]
                    print(f"\n  ┌─ {scene_path.name} / {pipeline}")
                    if r.get("exceeded"):
                        print(f"  │  Today : ⚠  did not converge ({r.get('exceeded_reason', '?')})"
                              f"  noise_rmse={r.get('final_noise_rmse', '?'):.6f}")
                    else:
                        print(f"  │  Today : ✓  converged at {r['samples_needed']} samples"
                              f"  ({r['render_time_seconds']:.1f}s)"
                              f"  noise_rmse={r['final_noise_rmse']:.6f}")
                    if stale is not None:
                        print(f"  │  ℹ️  A stored baseline exists but has incompatible settings")
                        print(f"  │     (stored target={stale.get('target_variance')}, level={stale.get('resolution_level')};"
                              f" current target={args.target_variance}, level={args.resolution_level})")
                    print(f"  └─  ℹ️  No compatible baseline — run 'just perf-store {pipeline} \"\" {args.target_variance}'"
                          f" to save one for future comparisons.")
                else:
                    passed = _report_convergence_vs_reference(
                        scene_path, pipeline, measured[pipeline], baseline
                    )
                    if not passed:
                        any_regression = True

        elif args.compare_pipeline:
            ref_result = measured[args.compare_pipeline]
            for pipeline in args.pipelines:
                if pipeline != args.compare_pipeline:
                    _report_cross_pipeline(scene_path, pipeline, args.compare_pipeline,
                                           measured[pipeline], ref_result)

        else:
            for pipeline in args.pipelines:
                reference = _load_reference(scene_path, pipeline)
                if reference is None:
                    _report_no_reference(scene_path, pipeline, measured[pipeline])
                else:
                    passed = _report_vs_reference(scene_path, pipeline, measured[pipeline], reference)
                    if not passed:
                        any_regression = True

        # ── graph series collection ──────────────────────────────────────────
        if convergence_mode and args.graph_output is not None:
            for pipeline in args.pipelines:
                if pipeline not in measured:
                    continue
                s_str = " ".join(f"{k}={v}" for k, v in _extra_render_settings.items())
                label = pipeline + (f"  [{s_str}]" if s_str else "")
                graph_series.append({
                    "label": label,
                    "curve": measured[pipeline].get("curve", []),
                    "from_store": False,
                    "settings_raw": dict(_extra_render_settings),
                })
            if args.compare_pipeline:
                compare = measured.get(args.compare_pipeline)
                if compare is not None and args.compare_pipeline not in args.pipelines:
                    if compare.get("from_store"):
                        b_settings = compare.get("render_settings", {})
                        b_str = " ".join(f"{k}={v}" for k, v in b_settings.items())
                        b_date = compare.get("stored_at", "?")[:10]
                        label = args.compare_pipeline + f" (stored {b_date})" + (f"  [{b_str}]" if b_str else "")
                    else:
                        b_settings = dict(_extra_render_settings)
                        b_str = " ".join(f"{k}={v}" for k, v in b_settings.items())
                        label = args.compare_pipeline + (f"  [{b_str}]" if b_str else "")
                    graph_series.append({
                        "label": label,
                        "curve": compare.get("curve", []),
                        "from_store": compare.get("from_store", False),
                        "settings_raw": b_settings,
                    })

    print()
    if args.graph_output is not None and graph_series:
        if args.graph_output:
            out_path = Path(args.graph_output)
        else:
            tested = "_".join(args.pipelines) if args.pipelines else "all"
            compare = args.compare_pipeline or ""
            compare_label = f"_vs_{compare}" if (compare and compare not in (args.pipelines or [])) else ""
            pipeline_label = f"{tested}{compare_label}"

            all_settings = [s.get("settings_raw", {}) for s in graph_series]
            all_keys = {k for d in all_settings for k in d}
            differing = sorted(k for k in all_keys
                               if len({d.get(k) for d in all_settings}) > 1)
            if differing:
                parts = "_".join(
                    f"{k}_{'vs'.join(str(d.get(k, '?')) for d in all_settings)}"
                    for k in differing
                )
                out_path = Path(f"graphs/convergence_{pipeline_label}_{parts}.png")
            else:
                out_path = Path(f"graphs/convergence_{pipeline_label}.png")
        _plot_convergence_graph(graph_series, args.target_variance, out_path)

    if args.store or args.ensure_store:
        print("=== References stored in performance_reference/ ===\n")
    elif args.compare_pipeline and not convergence_mode:
        print("=== Cross-pipeline comparison complete ===\n")
    elif convergence_mode:
        print("=== Convergence comparison complete ===\n")
    elif any_regression:
        print("=== REGRESSIONS DETECTED ===\n")
        if args.strict:
            sys.exit(1)
    else:
        print("=== All pipelines within reference ===\n")


if __name__ == "__main__":
    main()
