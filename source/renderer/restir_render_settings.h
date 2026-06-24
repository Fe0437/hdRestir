#pragma once

#include "pxr/base/tf/staticTokens.h"
#include "pxr/pxr.h"

PXR_NAMESPACE_USING_DIRECTIVE

// Each token uses the tuple form ((cppIdentifier, "restir:namespace:name")) so the
// C++ side stays clean while the USD wire-format uses a structured namespace hierarchy.
// Adding a new pipeline only requires new tokens in the matching namespace block.
#define HD_RESTIR_RENDER_SETTINGS_TOKENS                                                                                   \
    /* --- Pipeline selection ---------------------------------------------------- */                                      \
    ((pipelineIndex, "restir:pipelineIndex"))(                                                                             \
        (splitScreenRightPipelineIndex, "restir:splitScreen:rightPipelineIndex"))(                                         \
        (primaryPipeline, "restir:primaryPipeline"))((splitScreenRightPipeline, "restir:splitScreen:rightPipeline"))(      \
        (enableSplitScreen, "restir:splitScreen:enable"))(                                                                 \
        (splitScreenTargetSampleCount, "restir:splitScreen:targetSampleCount"))(                                           \
        (targetSampleCount, "restir:targetSampleCount"))(                                                                  \
        (resolutionLevel,                                                                                                  \
         "restir:resolutionLevel")) /* --- Path tracing ---------------------------------------------------------- */      \
        ((enableSubsurface, "restir:path:enableSubsurface"))(                                                              \
            (maxReflectionBounces, "restir:path:maxReflectionBounces"))(                                                   \
            (maxRefractionBounces, "restir:path:maxRefractionBounces"))((                                                  \
            renderIblBackground,                                                                                           \
            "restir:path:renderIblBackground")) /* --- RIS                                                                 \
                                                   -------------------------------------------------------------------     \
                                                 */                                                                        \
        ((risCandidateCount,                                                                                               \
          "restir:ris:candidateCount")) /* --- Denoiser --------------------------------------------------------------     \
                                         */                                                                                \
        ((enableDenoiser, "restir:denoiser:enable"))((enableFireflyFilter, "restir:denoiser:fireflyFilter"))(              \
            (enableChromaticityBlur,                                                                                       \
             "restir:denoiser:chromaticityBlur")) /* --- Post-process                                                      \
                                                     ---------------------------------------------------------- */         \
        ((enableLensFlare, "restir:postProcess:lensFlare"))((lensDistortion, "restir:postProcess:lensDistortion"))((       \
            chromaticAberration,                                                                                           \
            "restir:postProcess:chromaticAberration")) /* --- Camera                                                       \
                                                          ---------------------------------------------------------------- \
                                                        */                                                                 \
        ((enableDoF, "restir:camera:depthOfField"))((focalLength, "restir:camera:focalLength"))(                           \
            (fStop, "restir:camera:fStop"))((focusDistance, "restir:camera:focusDistance"))(                               \
            (bokehBlades, "restir:camera:bokehBlades"))((enablePhysicalCamera, "restir:camera:physicalExposure"))(         \
            (iso, "restir:camera:iso"))(                                                                                   \
            (shutterSpeed,                                                                                                 \
             "restir:camera:shutterSpeed")) /* --- Sky                                                                     \
                                               ------------------------------------------------------------------- */      \
        ((physicalSkyEnable, "restir:sky:enable"))((                                                                       \
            physicalSkyTime,                                                                                               \
            "restir:sky:timeOfDay")) /* --- Debug ----------------------------------------------------------------- */     \
        ((debugOverlay, "restir:debug:overlay"))

TF_DECLARE_PUBLIC_TOKENS(HdRestirRenderSettingsTokens, HD_RESTIR_RENDER_SETTINGS_TOKENS);

// Expands to a comma-separated list of SpecT{Label, Token, DefaultValue, Hidden}
// initializers. SpecT is the caller's concrete spec struct so this header stays
// independent of Renderer internals.
#define HD_RESTIR_RENDER_SETTINGS_SPECS(SpecT)                                                                         \
    SpecT{"Pipeline (0=PathTracer 1=PostProcess 2=RIS)", HdRestirRenderSettingsTokens->pipelineIndex, VtValue(0),      \
          true},                                                                                                       \
        SpecT{"Split Screen Right Pipeline (0/1/2)", HdRestirRenderSettingsTokens->splitScreenRightPipelineIndex,      \
              VtValue(1), true},                                                                                       \
        SpecT{"Primary Pipeline", HdRestirRenderSettingsTokens->primaryPipeline,                                       \
              VtValue(Restir::GetPathTracerPipelineToken()), true},                                                    \
        SpecT{"Enable Split Screen", HdRestirRenderSettingsTokens->enableSplitScreen, VtValue(false), false},          \
        SpecT{"Split Screen Right Pipeline", HdRestirRenderSettingsTokens->splitScreenRightPipeline,                   \
              VtValue(Restir::GetPathTracerPostProcessPipelineToken()), true},                                         \
        SpecT{"Target Sample Count (Left)", HdRestirRenderSettingsTokens->targetSampleCount, VtValue(32), false},      \
        SpecT{"Target Sample Count (Right)", HdRestirRenderSettingsTokens->splitScreenTargetSampleCount, VtValue(32),  \
              false},                                                                                                  \
        SpecT{"Resolution Level", HdRestirRenderSettingsTokens->resolutionLevel, VtValue(2), false},                   \
        SpecT{"Enable Subsurface Scattering", HdRestirRenderSettingsTokens->enableSubsurface, VtValue(true), true},    \
        SpecT{"Max Reflection Bounces", HdRestirRenderSettingsTokens->maxReflectionBounces, VtValue(8), true},         \
        SpecT{"Max Refraction Bounces", HdRestirRenderSettingsTokens->maxRefractionBounces, VtValue(8), true},         \
        SpecT{"Render IBL Background", HdRestirRenderSettingsTokens->renderIblBackground, VtValue(true), true},        \
        SpecT{"RIS Candidate Count", HdRestirRenderSettingsTokens->risCandidateCount, VtValue(16), true},              \
        SpecT{"Enable OIDN Denoiser", HdRestirRenderSettingsTokens->enableDenoiser, VtValue(true), true},              \
        SpecT{"Enable Pre-Pass: Firefly Filter", HdRestirRenderSettingsTokens->enableFireflyFilter, VtValue(true),     \
              true},                                                                                                   \
        SpecT{"Enable Pre-Pass: Chromaticity Blur", HdRestirRenderSettingsTokens->enableChromaticityBlur,              \
              VtValue(true), true},                                                                                    \
        SpecT{"Enable Lens Flare", HdRestirRenderSettingsTokens->enableLensFlare, VtValue(false), true},               \
        SpecT{"Lens Distortion", HdRestirRenderSettingsTokens->lensDistortion, VtValue(0.0f), true},                   \
        SpecT{"Chromatic Aberration", HdRestirRenderSettingsTokens->chromaticAberration, VtValue(0.0f), true},         \
        SpecT{"Enable Depth of Field", HdRestirRenderSettingsTokens->enableDoF, VtValue(false), false},                \
        SpecT{"Focal Length (mm)", HdRestirRenderSettingsTokens->focalLength, VtValue(50.0f), false},                  \
        SpecT{"F-Stop (Aperture)", HdRestirRenderSettingsTokens->fStop, VtValue(5.6f), false},                         \
        SpecT{"Focus Distance", HdRestirRenderSettingsTokens->focusDistance, VtValue(10.0f), false},                   \
        SpecT{"Bokeh Blades", HdRestirRenderSettingsTokens->bokehBlades, VtValue(0), false},                           \
        SpecT{"Enable Physical Camera Exposure", HdRestirRenderSettingsTokens->enablePhysicalCamera, VtValue(false),   \
              false},                                                                                                  \
        SpecT{"ISO", HdRestirRenderSettingsTokens->iso, VtValue(100.0f), false},                                       \
        SpecT{"Shutter Speed", HdRestirRenderSettingsTokens->shutterSpeed, VtValue(0.02f), false},                     \
        SpecT{"Enable Physical Sky", HdRestirRenderSettingsTokens->physicalSkyEnable, VtValue(false), false},          \
        SpecT{"Physical Sky Time of Day", HdRestirRenderSettingsTokens->physicalSkyTime, VtValue(12.0f), false}, SpecT \
    {                                                                                                                  \
        "Debug Overlay (only working in Debug Mode build)", HdRestirRenderSettingsTokens->debugOverlay,                \
            VtValue(false), false                                                                                      \
    }
