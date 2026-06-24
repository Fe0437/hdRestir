python := if os_family() == "windows" { "python" } else { "python3" }

default:
    @just --list

build:
    {{python}} workflow.py

debug:
    {{python}} workflow.py debug

test-smoke:
    ctest --test-dir build -V -L smoke

test-all:
    ctest --test-dir build -V

debug-smoke:
    {{python}} workflow.py debug
    ctest --test-dir build -V -L smoke

debug-test:
    {{python}} workflow.py debug
    ctest --test-dir build -V

launch scene="example_scenes/scene.usda" render_settings="":
    {{python}} workflow.py launch "{{scene}}" {{render_settings}}

capture scene="example_scenes/scene.usda" output="capture.png" render_settings="":
    {{python}} workflow.py capture "{{scene}}" "{{output}}" {{render_settings}}

capture-internal-reference output="reference/internal_scene_reference.hdr":
    {{python}} workflow.py capture "example_scenes/scene.usda" "{{output}}" \
        --render-setting targetSampleCount=2048 \
        --render-setting enableSplitScreen=0 \
        --render-setting primaryPipeline=PathTracer \
        --render-setting resolutionLevel=0 \
        --render-setting enableFireflyFilter=1

# Performance tests ─────────────────────────────────────────────────────────
# Baselines are stored in performance_reference/{scene}/{pipeline}.json.
# Run perf-store once per pipeline to save its numbers; later perf-test runs
# will load that baseline and skip the re-render entirely.
#
# Arguments (all optional):
#   pipeline  — pipeline(s) to act on           (default: "" = all pipelines)
#   scene     — path to a .usda, or "all"      (default: "" = example_scenes/scene.usda)
#   compare   — pipeline to compare against;   loaded from store if available, rendered live if not
#   variance  — target noise_rmse = RMSE(N, N/4) for convergence mode (omit = fixed-sample mode)
#   samples   — samples per render in fixed-sample mode (default: 64)
#
# Typical workflow:
#   just perf-store                              # store PathTracer + RIS fixed-sample baselines
#   just perf-store "" "" "" 0.005              # store convergence baselines (target rmse ≤ 0.005)
#   just perf-test "" "" PathTracer             # test all pipelines vs stored PathTracer baseline (no re-render)
#   just perf-test RIS "" PathTracer            # test RIS only vs stored PathTracer (no re-render)
#   just perf-test "" "" PathTracer 0.005       # convergence: how many samples does each pipeline need?
#   just perf-log                               # read-only inspection (never fails)

# Store a convergence baseline. Args: pipeline, scene, variance (e.g. 0.01), samples, runs, settings ("k=v k=v")
perf-store pipeline="" scene="" variance="" samples="64" runs="3" settings="":
    #!/usr/bin/env bash
    set -e
    ARGS=(--store --runs {{runs}})
    [ "{{pipeline}}" != "" ] && ARGS+=(--pipelines "{{pipeline}}")
    if [ "{{scene}}" = "all" ]; then ARGS+=(--all-scenes)
    elif [ "{{scene}}" != "" ]; then ARGS+=(--scene "{{scene}}"); fi
    if [ "{{variance}}" != "" ]; then ARGS+=(--target-variance {{variance}})
    else ARGS+=(--sample-count {{samples}}); fi
    for s in {{settings}}; do ARGS+=(--render-setting "$s"); done
    {{python}} tests/perf_test.py "${ARGS[@]}"

# Run perf test (strict). Args: pipeline, scene, compare-pipeline, variance, runs, settings ("k=v k=v")
perf-test pipeline="" scene="" compare="" variance="" runs="3" settings="":
    {{python}} tests/perf_test.py --strict --runs {{runs}} \
        {{ if pipeline != "" { "--pipelines " + pipeline } else { "" } }} \
        {{ if scene == "all" { "--all-scenes" } else if scene != "" { "--scene " + scene } else { "" } }} \
        {{ if compare != "" { "--compare-pipeline " + compare } else { "" } }} \
        {{ if variance != "" { "--target-variance " + variance } else { "" } }} \
        {{ if settings != "" { "--render-setting " + settings } else { "" } }}

# Like perf-test but read-only (never fails). Args: pipeline, scene, compare, variance, runs, settings
perf-log pipeline="" scene="" compare="" variance="" runs="3" settings="":
    {{python}} tests/perf_test.py --runs {{runs}} \
        {{ if pipeline != "" { "--pipelines " + pipeline } else { "" } }} \
        {{ if scene == "all" { "--all-scenes" } else if scene != "" { "--scene " + scene } else { "" } }} \
        {{ if compare != "" { "--compare-pipeline " + compare } else { "" } }} \
        {{ if variance != "" { "--target-variance " + variance } else { "" } }} \
        {{ if settings != "" { "--render-setting " + settings } else { "" } }}

# Generate convergence graph (PNG in graphs/). Args: pipeline, scene, compare, variance, runs, settings, output
perf-test-graph pipeline="" scene="" compare="" variance="0.01" runs="1" settings="" output="":
    #!/usr/bin/env bash
    set -e
    ARGS=(--runs {{runs}} --graph-output "{{output}}" --target-variance {{variance}})
    [ "{{pipeline}}" != "" ] && ARGS+=(--pipelines "{{pipeline}}")
    if [ "{{scene}}" = "all" ]; then ARGS+=(--all-scenes)
    elif [ "{{scene}}" != "" ]; then ARGS+=(--scene "{{scene}}"); fi
    [ "{{compare}}" != "" ] && ARGS+=(--compare-pipeline "{{compare}}")
    for s in {{settings}}; do ARGS+=(--render-setting "$s"); done
    {{python}} tests/perf_test.py "${ARGS[@]}"

# Open the stored performance report in the browser
perf-report:
    {{python}} tests/perf_report.py --open

# test-and-perf: one command to build, test, and verify performance for a pipeline.
#
#   What it does (in order):
#     1. Build the plugin                           (just build)
#     2. All tests — smoke + normal                 (exits immediately on any failure)
#     3. Ensure performance baselines exist         (runs convergence + stores, no-op if already stored)
#     4. Self-regression check (strict)             (pipeline vs its own stored baseline → ✅/❌)
#        + Cross-pipeline comparison (informational) (pipeline vs PathTracer or RIS)
#
#   Exit code: non-zero if any test fails OR a performance regression is detected.
#
#   Arguments:
#     pipeline  — pipeline to validate           (default: RIS)
#     variance  — noise_rmse convergence target  (default: 0.005)
#
#   Examples:
#     just test-and-perf RIS
#     just test-and-perf PathTracer 0.01

# Build, run all tests, ensure baselines, then check for regressions. Args: pipeline, variance
test-and-perf pipeline="RIS" variance="0.005":
    #!/usr/bin/env bash
    set -e
    PIPELINE="{{pipeline}}"
    VARIANCE="{{variance}}"
    if [ "$PIPELINE" = "PathTracer" ]; then
        REFERENCE="RIS"
    else
        REFERENCE="PathTracer"
    fi
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo "  test-and-perf │ $PIPELINE  (convergence target ≤ $VARIANCE)"
    echo "════════════════════════════════════════════════════════════"
    echo ""
    echo "▶ 1/4  Building..."
    {{python}} workflow.py
    echo ""
    echo "▶ 2/4  Running all tests (smoke + normal)..."
    ctest --test-dir build -V
    echo ""
    echo "▶ 3/4  Ensuring performance baselines exist..."
    {{python}} tests/perf_test.py --ensure-store --pipelines "$REFERENCE" --target-variance "$VARIANCE"
    {{python}} tests/perf_test.py --ensure-store --pipelines "$PIPELINE" --target-variance "$VARIANCE"
    echo ""
    echo "▶ 4/4  Performance: $PIPELINE vs stored $PIPELINE (regression) + vs $REFERENCE (quality)..."
    {{python}} tests/perf_test.py --strict \
        --pipelines "$PIPELINE" \
        --compare-pipeline "$REFERENCE" \
        --target-variance "$VARIANCE"
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo "  ✅  test-and-perf: $PIPELINE — all checks passed"
    echo "════════════════════════════════════════════════════════════"
    echo ""

# ─────────────────────────────────────────────────────────────────────────────

diff-hdr reference current output:
    {{python}} exploratory_tests/image_comparison/create_hdr_difference.py "{{reference}}" "{{current}}" "{{output}}"

diff-internal-reference output="images/reference/internal_scene_difference.hdr":
    {{python}} exploratory_tests/image_comparison/create_hdr_difference.py \
        "images/reference/internal_scene_reference.hdr" \
        "build/tests/reference_captures/internal_scene_current.hdr" \
        "{{output}}"