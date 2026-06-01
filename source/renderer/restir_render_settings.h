#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

#define HD_RESTIR_RENDER_SETTINGS_TOKENS \
    (enableSplitScreen)                  \
    (enableRis)                          \
    (primaryPipeline)                    \
    (splitScreenRightPipeline)           \
    (enableDenoiser)                     \
    (enableFireflyFilter)                \
    (enableChromaticityBlur)             \
    (targetSampleCount)                  \
    (risCandidateCount)                  \
    (maxReflectionBounces)               \
    (maxRefractionBounces)               \
    (resolutionLevel)                    \
    (enableDoF)                          \
    (focalLength)                        \
    (fStop)                              \
    (focusDistance)                      \
    (bokehBlades)                        \
    (enablePhysicalCamera)               \
    (iso)                                \
    (shutterSpeed)                       \
    (enableLensFlare)                    \
    (renderIblBackground)                \
    (lensDistortion)                     \
    (chromaticAberration)                \
    (enableSubsurface)                   \
    (physicalSkyEnable)                  \
    (physicalSkyTime)

TF_DECLARE_PUBLIC_TOKENS(HdRestirRenderSettingsTokens, HD_RESTIR_RENDER_SETTINGS_TOKENS);