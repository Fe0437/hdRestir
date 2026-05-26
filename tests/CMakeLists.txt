find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(HDRESTIR_SLOW_TEST_TIMEOUT_SECONDS "5000" CACHE STRING "Timeout in seconds for slow render-dependent tests")

foreach(_test IN ITEMS test_camera test_frame_buffer test_rng test_split_screen test_scene_material_lookup test_renderer_aov_handoff test_mis_direct_light_integrator)
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
    )
    add_test(NAME ${_test} COMMAND ${_test})
    set_tests_properties(${_test} PROPERTIES LABELS "smoke")
endforeach()

set(HDRESTIR_INTERNAL_REFERENCE_SCENE "${PROJECT_SOURCE_DIR}/example_scenes/scene.usda")
set(HDRESTIR_INTERNAL_REFERENCE_IMAGE "${PROJECT_SOURCE_DIR}/images/reference/internal_scene_reference.hdr")
set(HDRESTIR_CAPTURE_MSE_THRESHOLD "5e-8" CACHE STRING "Maximum normalized MSE allowed for HDR capture regression" FORCE)
set(HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT "2048" CACHE STRING "Target sample count used when capturing the internal HDR reference test scene")
set(HDRESTIR_REFERENCE_RESOLUTION_LEVEL "0" CACHE STRING "Resolution level used when capturing the internal HDR reference test scene")
if(HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT STREQUAL "256" OR HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT STREQUAL "1024")
    set(HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT "2048" CACHE STRING "Target sample count used when capturing the internal HDR reference test scene" FORCE)
endif()

if(EXISTS "${HDRESTIR_INTERNAL_REFERENCE_SCENE}" AND EXISTS "${HDRESTIR_INTERNAL_REFERENCE_IMAGE}")
    add_test(
        NAME test_hdr_capture_mse
        COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/test_hdr_capture_mse.py
            --workflow ${PROJECT_SOURCE_DIR}/workflow.py
            --scene ${HDRESTIR_INTERNAL_REFERENCE_SCENE}
            --reference ${HDRESTIR_INTERNAL_REFERENCE_IMAGE}
            --output ${CMAKE_CURRENT_BINARY_DIR}/reference_captures/internal_scene_current.hdr
            --threshold ${HDRESTIR_CAPTURE_MSE_THRESHOLD}
            --render-setting targetSampleCount=${HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT}
            --render-setting enableSplitScreen=0
            --render-setting resolutionLevel=${HDRESTIR_REFERENCE_RESOLUTION_LEVEL}
            --render-setting enableFireflyFilter=1
    )
    set_tests_properties(test_hdr_capture_mse PROPERTIES
        LABELS "normal"
        TIMEOUT ${HDRESTIR_SLOW_TEST_TIMEOUT_SECONDS}
    )
endif()

set(HDRESTIR_MIS_AREA_LIGHTS_SCENE "${PROJECT_SOURCE_DIR}/example_scenes/mis_area_lights_scene.usda")
set(HDRESTIR_MIS_AREA_LIGHTS_REFERENCE "${PROJECT_SOURCE_DIR}/images/reference/mis_area_lights_reference.hdr")

if(EXISTS "${HDRESTIR_MIS_AREA_LIGHTS_SCENE}" AND EXISTS "${HDRESTIR_MIS_AREA_LIGHTS_REFERENCE}")
    add_test(
        NAME test_hdr_capture_mse_mis_area_lights
        COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/test_hdr_capture_mse.py
            --workflow ${PROJECT_SOURCE_DIR}/workflow.py
            --scene ${HDRESTIR_MIS_AREA_LIGHTS_SCENE}
            --reference ${HDRESTIR_MIS_AREA_LIGHTS_REFERENCE}
            --output ${CMAKE_CURRENT_BINARY_DIR}/reference_captures/mis_area_lights_current.hdr
            --threshold ${HDRESTIR_CAPTURE_MSE_THRESHOLD}
            --render-setting targetSampleCount=${HDRESTIR_REFERENCE_TARGET_SAMPLE_COUNT}
            --render-setting enableSplitScreen=0
            --render-setting resolutionLevel=${HDRESTIR_REFERENCE_RESOLUTION_LEVEL}
            --render-setting enableFireflyFilter=1
    )
    set_tests_properties(test_hdr_capture_mse_mis_area_lights PROPERTIES
        LABELS "normal"
        TIMEOUT ${HDRESTIR_SLOW_TEST_TIMEOUT_SECONDS}
    )
endif()
