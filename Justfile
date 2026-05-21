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

launch scene="scene.usda":
    {{python}} workflow.py launch "{{scene}}"

capture scene="scene.usda" output="capture.png":
    {{python}} workflow.py capture "{{scene}}" "{{output}}"

capture-internal-reference output="reference/internal_scene_reference.hdr":
    {{python}} workflow.py capture "scene.usda" "{{output}}" \
        --render-setting targetSampleCount=2048 \
        --render-setting enableSplitScreen=0 \
        --render-setting primaryPipeline=PathTracer \
        --render-setting resolutionLevel=0 \
        --render-setting enableFireflyFilter=1

diff-hdr reference current output:
    {{python}} exploratory_tests/image_comparison/create_hdr_difference.py "{{reference}}" "{{current}}" "{{output}}"

diff-internal-reference output="images/reference/internal_scene_difference.hdr":
    {{python}} exploratory_tests/image_comparison/create_hdr_difference.py \
        "images/reference/internal_scene_reference.hdr" \
        "build/tests/reference_captures/internal_scene_current.hdr" \
        "{{output}}"