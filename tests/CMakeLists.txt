find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(HDRESTIR_SLOW_TEST_TIMEOUT_SECONDS "5000" CACHE STRING "Timeout in seconds for slow render-dependent tests")

foreach(_test IN ITEMS test_camera test_frame_buffer test_rng test_split_screen test_scene_material_lookup test_renderer_aov_handoff test_mis_direct_light_integrator test_reservoir)
    add_executable(${_test} ${_test}.cpp ${SOURCE_CPP})
    target_include_directories(${_test} PRIVATE ${SOURCE_INCLUDE_DIRS})
    target_link_libraries(${_test} PRIVATE
        Microsoft.GSL::GSL
        tf gf vt work
        hd hdsi hio
        sdf sdr
        usdImaging
    )
    target_compile_definitions(${_test} PRIVATE
        HDRESTIR_DEBUG_SETTING=${HDRESTIR_DEBUG_SETTING_VALUE}
        HDRESTIR_METRICS_SETTING=${HDRESTIR_METRICS_SETTING_VALUE}
        $<$<CONFIG:Debug>:gsl_CONFIG_CONTRACT_VIOLATION_THROWS=1>
        $<$<BOOL:${METRICS_ENABLED}>:METRICS_ENABLED=1>
        $<$<NOT:$<BOOL:${METRICS_ENABLED}>>:METRICS_ENABLED=0>
        $<$<BOOL:${GPU_ENABLED}>:GPU_ENABLED=1>
        $<$<NOT:$<BOOL:${GPU_ENABLED}>>:GPU_ENABLED=0>
    )
    if(GPU_ENABLED)
        target_link_libraries(${_test} PRIVATE HdRestirGpu)
        # These exes live in build/tests/, so the backend DLL in the build root
        # is not on their search path. Without this, every test dies at startup
        # with STATUS_DLL_NOT_FOUND (0xc0000135) behind a modal Windows dialog.
        if(WIN32)
            add_custom_command(TARGET ${_test} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:LightRHIBackend>" "$<TARGET_FILE_DIR:${_test}>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:LightRHI>" "$<TARGET_FILE_DIR:${_test}>"
                VERBATIM)
        endif()
    endif()
    add_test(NAME ${_test} COMMAND ${_test})
    set_tests_properties(${_test} PROPERTIES LABELS "smoke")
endforeach()

set(HDRESTIR_INTERNAL_REFERENCE_SCENE "${PROJECT_SOURCE_DIR}/example_scenes/scene.usda")
set(HDRESTIR_INTERNAL_REFERENCE_IMAGE "${PROJECT_SOURCE_DIR}/images/reference/internal_scene_reference.hdr")
set(HDRESTIR_CAPTURE_MSE_THRESHOLD "5e-8" CACHE STRING "Maximum normalized MSE for same-pipeline HDR regression test" FORCE)
set(HDRESTIR_CROSS_PIPELINE_MSE_THRESHOLD "1e-4" CACHE STRING "Maximum normalized MSE for cross-pipeline convergence test")
set(HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT "2048" CACHE STRING "Target sample count used when capturing the internal HDR reference test scene")
set(HDRESTIR_REFERENCE_RESOLUTION_LEVEL "0" CACHE STRING "Resolution level used when capturing the internal HDR reference test scene")
if(HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT STREQUAL "256" OR HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT STREQUAL "1024")
    set(HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT "2048" CACHE STRING "Target sample count used when capturing the internal HDR reference test scene" FORCE)
endif()

# Common render settings shared by all pipeline reference tests.
# Each test appends --render-setting primaryPipeline=<X>.
set(_HDRESTIR_REF_COMMON_SETTINGS
    --render-setting targetSampleCount=${HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT}
    --render-setting enableSplitScreen=0
    --render-setting resolutionLevel=${HDRESTIR_REFERENCE_RESOLUTION_LEVEL}
    --render-setting enableFireflyFilter=1
)

# PathTracer vs its own reference — regression test, tight threshold.
add_test(
    NAME test_hdr_capture_mse
    COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/test_hdr_capture_mse.py
        --workflow ${PROJECT_SOURCE_DIR}/workflow.py
        --scene ${HDRESTIR_INTERNAL_REFERENCE_SCENE}
        --reference ${HDRESTIR_INTERNAL_REFERENCE_IMAGE}
        --output ${CMAKE_CURRENT_BINARY_DIR}/reference_captures/internal_scene_current.hdr
        --threshold ${HDRESTIR_CAPTURE_MSE_THRESHOLD}
        ${_HDRESTIR_REF_COMMON_SETTINGS}
        --render-setting primaryPipeline=PathTracer
)
set_tests_properties(test_hdr_capture_mse PROPERTIES
    LABELS "normal"
    TIMEOUT ${HDRESTIR_SLOW_TEST_TIMEOUT_SECONDS}
)

# RIS vs PathTracer reference — convergence test: both algorithms must produce the
# same image within the cross-pipeline threshold.
add_test(
    NAME test_hdr_capture_mse_ris
    COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/test_hdr_capture_mse.py
        --workflow ${PROJECT_SOURCE_DIR}/workflow.py
        --scene ${HDRESTIR_INTERNAL_REFERENCE_SCENE}
        --reference ${HDRESTIR_INTERNAL_REFERENCE_IMAGE}
        --output ${CMAKE_CURRENT_BINARY_DIR}/reference_captures/internal_scene_ris_current.hdr
        --threshold ${HDRESTIR_CROSS_PIPELINE_MSE_THRESHOLD}
        ${_HDRESTIR_REF_COMMON_SETTINGS}
        --render-setting primaryPipeline=RIS
)
set_tests_properties(test_hdr_capture_mse_ris PROPERTIES
    LABELS "normal"
    TIMEOUT ${HDRESTIR_SLOW_TEST_TIMEOUT_SECONDS}
)

set(HDRESTIR_PERF_SAMPLE_COUNT "64" CACHE STRING "Sample count per render used in performance tests")
set(HDRESTIR_PERF_SCENE "${PROJECT_SOURCE_DIR}/example_scenes/scene.usda")

add_test(
    NAME perf_test_all_pipelines
    COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/perf_test.py
        --workflow ${PROJECT_SOURCE_DIR}/workflow.py
        --scene ${HDRESTIR_PERF_SCENE}
        --sample-count ${HDRESTIR_PERF_SAMPLE_COUNT}
        --tmp-dir ${CMAKE_CURRENT_BINARY_DIR}/perf_test_captures
)
set_tests_properties(perf_test_all_pipelines PROPERTIES
    LABELS "performance"
    TIMEOUT ${HDRESTIR_SLOW_TEST_TIMEOUT_SECONDS}
)

set(HDRESTIR_MIS_AREA_LIGHTS_SCENE "${PROJECT_SOURCE_DIR}/example_scenes/mis_area_lights_scene.usda")
set(HDRESTIR_MIS_AREA_LIGHTS_REFERENCE "${PROJECT_SOURCE_DIR}/images/reference/mis_area_lights_reference.hdr")

add_test(
    NAME test_hdr_capture_mse_mis_area_lights
    COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/test_hdr_capture_mse.py
        --workflow ${PROJECT_SOURCE_DIR}/workflow.py
        --scene ${HDRESTIR_MIS_AREA_LIGHTS_SCENE}
        --reference ${HDRESTIR_MIS_AREA_LIGHTS_REFERENCE}
        --output ${CMAKE_CURRENT_BINARY_DIR}/reference_captures/mis_area_lights_current.hdr
        --threshold ${HDRESTIR_CAPTURE_MSE_THRESHOLD}
        ${_HDRESTIR_REF_COMMON_SETTINGS}
)
set_tests_properties(test_hdr_capture_mse_mis_area_lights PROPERTIES
    LABELS "normal"
    TIMEOUT ${HDRESTIR_SLOW_TEST_TIMEOUT_SECONDS}
)

set(HDRESTIR_MATERIALX_SCENE "${PROJECT_SOURCE_DIR}/example_scenes/materialx_scene.usda")
set(HDRESTIR_MATERIALX_REFERENCE "${PROJECT_SOURCE_DIR}/images/reference/materialx_scene_reference.hdr")

add_test(
    NAME test_hdr_capture_mse_materialx
    COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/test_hdr_capture_mse.py
        --workflow ${PROJECT_SOURCE_DIR}/workflow.py
        --scene ${HDRESTIR_MATERIALX_SCENE}
        --reference ${HDRESTIR_MATERIALX_REFERENCE}
        --output ${CMAKE_CURRENT_BINARY_DIR}/reference_captures/materialx_scene_current.hdr
        --threshold ${HDRESTIR_CAPTURE_MSE_THRESHOLD}
        ${_HDRESTIR_REF_COMMON_SETTINGS}
)
set_tests_properties(test_hdr_capture_mse_materialx PROPERTIES
    LABELS "normal"
    TIMEOUT ${HDRESTIR_SLOW_TEST_TIMEOUT_SECONDS}
)
# Point the test loader at the USD runtime libraries. USD is not necessarily
# local: it may be the external/usd-install build, a system-wide install, or a
# conda environment. Use whatever find_package(pxr) actually resolved (via
# pxr_DIR), falling back to the local build, so the tests load the same USD the
# targets linked against — matching the plugin RPATH logic in the root CMake.
if(pxr_DIR AND EXISTS "${pxr_DIR}/lib")
    set(_usd_prefix "${pxr_DIR}")
elseif(EXISTS "${_usd_local}/lib")
    set(_usd_prefix "${_usd_local}")
elseif(DEFINED ENV{CONDA_PREFIX})
    set(_usd_prefix "$ENV{CONDA_PREFIX}")
else()
    set(_usd_prefix "")
endif()

if(_usd_prefix)
    if(WIN32)
        set(_usd_runtime_dirs "${_usd_prefix}/lib" "${_usd_prefix}/bin")
        set(_runtime_path_var "PATH")
    elseif(APPLE)
        set(_usd_runtime_dirs "${_usd_prefix}/lib")
        set(_runtime_path_var "DYLD_LIBRARY_PATH")
    else()
        set(_usd_runtime_dirs "${_usd_prefix}/lib")
        set(_runtime_path_var "LD_LIBRARY_PATH")
    endif()
    set(_env_mods "")
    foreach(_d IN LISTS _usd_runtime_dirs)
        list(APPEND _env_mods "${_runtime_path_var}=path_list_prepend:${_d}")
    endforeach()
    get_property(_hdrestir_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${_hdrestir_tests} PROPERTIES ENVIRONMENT_MODIFICATION "${_env_mods}")
endif()
