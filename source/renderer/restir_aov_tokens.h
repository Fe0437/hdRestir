#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

#define HD_RESTIR_AOV_TOKENS \
    (albedo)                 \
    (normal)

TF_DECLARE_PUBLIC_TOKENS(HdRestirAovTokens, HD_RESTIR_AOV_TOKENS);