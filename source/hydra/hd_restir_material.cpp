#include "hd_restir_material.h"
#include "hd_restir_render_delegate.h"
#include "hd_restir_render_param.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/materialNetwork2Interface.h"
#include "pxr/usd/sdr/registry.h"
#include "pxr/usd/sdr/shaderNode.h"
#include "pxr/base/vt/value.h"
#include <map>
#include <set>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

HdRestirMaterial::HdRestirMaterial(SdfPath const& id)
    : HdMaterial(id)
{
}

HdRestirMaterial::~HdRestirMaterial() = default;

// Helper to walk upstream and process nodes
static void _ProcessNodeUpstream(
    const HdMaterialNetwork2& network,
    const SdfPath& nodePath,
    std::set<SdfPath>& visited,
    HdRestirMaterial* material,
    TfToken targetInput = TfToken())
{
    if (visited.count(nodePath)) return;
    visited.insert(nodePath);

    auto itNode = network.nodes.find(nodePath);
    if (itNode == network.nodes.end()) return;

    const HdMaterialNode2& node = itNode->second;
    SdrRegistry &sdrRegistry = SdrRegistry::GetInstance();
    
    // Resolve shader with MaterialX context preference
    SdrShaderNodeConstPtr sdrEntry = sdrRegistry.GetShaderNodeByIdentifier(node.nodeTypeId, {TfToken("mtlx")});
    if (!sdrEntry) sdrEntry = sdrRegistry.GetShaderNodeByIdentifier(node.nodeTypeId);
    
    TfToken shaderId = sdrEntry ? sdrEntry->GetIdentifier() : node.nodeTypeId;
    
    HdRestir_LOG << "[Restir]     Node: " << nodePath.GetText() << " | Resolved ID: " << shaderId.GetText() << " | Type: " << node.nodeTypeId.GetText() << " | Target: " << targetInput.GetText() << std::endl;

    // Parse parameters based on shader type
    if (shaderId == TfToken("UsdPreviewSurface")) {
        for (auto const& param : node.parameters) {
            if (param.first == TfToken("diffuseColor") && param.second.IsHolding<GfVec3f>()) {
                material->SetDiffuseColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("metallic") && param.second.IsHolding<float>()) {
                material->SetMetallic(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("roughness") && param.second.IsHolding<float>()) {
                material->SetRoughness(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("specularColor") && param.second.IsHolding<GfVec3f>()) {
                material->SetSpecularColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("opacity") && param.second.IsHolding<float>()) {
                material->SetOpacity(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("ior") && param.second.IsHolding<float>()) {
                material->SetIor(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("emissiveColor") && param.second.IsHolding<GfVec3f>()) {
                material->SetEmissionColor(param.second.UncheckedGet<GfVec3f>());
                material->SetEmission(1.0f);
            }
        }
    } else if (shaderId == TfToken("ND_standard_surface_surfaceshader") ||
               shaderId == TfToken("standard_surface") ||
               shaderId == TfToken("StandardSurface")) {
        float base = 1.0f;
        GfVec3f baseColor(0.8f);
        for (auto const& param : node.parameters) {
            if ((param.first == TfToken("base") || param.first == TfToken("base_weight")) && param.second.IsHolding<float>()) {
                base = param.second.UncheckedGet<float>();
            } else if (param.first == TfToken("base_color") && param.second.IsHolding<GfVec3f>()) {
                baseColor = param.second.UncheckedGet<GfVec3f>();
            } else if (param.first == TfToken("metalness") && param.second.IsHolding<float>()) {
                material->SetMetallic(param.second.UncheckedGet<float>());
            } else if ((param.first == TfToken("specular") || param.first == TfToken("specular_weight")) && param.second.IsHolding<float>()) {
                material->SetSpecular(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("specular_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetSpecularColor(param.second.UncheckedGet<GfVec3f>());
            } else if ((param.first == TfToken("specular_roughness") || param.first == TfToken("roughness")) && param.second.IsHolding<float>()) {
                material->SetRoughness(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("opacity") && param.second.IsHolding<float>()) {
                material->SetOpacity(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("ior") && param.second.IsHolding<float>()) {
                material->SetIor(param.second.UncheckedGet<float>());
            } else if ((param.first == TfToken("transmission") || param.first == TfToken("transmission_weight")) && param.second.IsHolding<float>()) {
                material->SetTransmission(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("transmission_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetTransmissionColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("transmission_depth") && param.second.IsHolding<float>()) {
                material->SetTransmissionDepth(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("transmission_scatter") && param.second.IsHolding<GfVec3f>()) {
                material->SetTransmissionScatter(param.second.UncheckedGet<GfVec3f>());
            } else if ((param.first == TfToken("subsurface") || param.first == TfToken("subsurface_weight")) && param.second.IsHolding<float>()) {
                material->SetSubsurface(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("subsurface_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetSubsurfaceColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("subsurface_radius") && param.second.IsHolding<GfVec3f>()) {
                material->SetSubsurfaceRadius(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("subsurface_scale") && param.second.IsHolding<float>()) {
                material->SetSubsurfaceScale(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("subsurface_anisotropy") && param.second.IsHolding<float>()) {
                material->SetSubsurfaceAnisotropy(param.second.UncheckedGet<float>());
            } else if ((param.first == TfToken("sheen") || param.first == TfToken("sheen_weight")) && param.second.IsHolding<float>()) {
                material->SetSheen(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("sheen_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetSheenColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("sheen_roughness") && param.second.IsHolding<float>()) {
                material->SetSheenRoughness(param.second.UncheckedGet<float>());
            } else if ((param.first == TfToken("coat") || param.first == TfToken("coat_weight")) && param.second.IsHolding<float>()) {
                material->SetCoat(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("coat_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetCoatColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("coat_roughness") && param.second.IsHolding<float>()) {
                material->SetCoatRoughness(param.second.UncheckedGet<float>());
            } else if ((param.first == TfToken("coat_IOR") || param.first == TfToken("coat_ior")) && param.second.IsHolding<float>()) {
                material->SetCoatIor(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("thin_walled") && param.second.IsHolding<bool>()) {
                material->SetThinWalled(param.second.UncheckedGet<bool>());
            } else if (param.first == TfToken("diffuse_roughness") && param.second.IsHolding<float>()) {
                material->SetDiffuseRoughness(param.second.UncheckedGet<float>());
            } else if ((param.first == TfToken("emission") || param.first == TfToken("emission_weight")) && param.second.IsHolding<float>()) {
                material->SetEmission(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("emission_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetEmissionColor(param.second.UncheckedGet<GfVec3f>());
            }
        }
        material->SetDiffuseColor(baseColor * base);
    } else if (shaderId == TfToken("ND_open_pbr_surface_surfaceshader") ||
               shaderId == TfToken("open_pbr_surface")) {
        float base = 1.0f;
        GfVec3f baseColor(0.8f);
        for (auto const& param : node.parameters) {
            if (param.first == TfToken("base_weight") && param.second.IsHolding<float>()) {
                base = param.second.UncheckedGet<float>();
            } else if (param.first == TfToken("base_color") && param.second.IsHolding<GfVec3f>()) {
                baseColor = param.second.UncheckedGet<GfVec3f>();
            } else if (param.first == TfToken("base_metalness") && param.second.IsHolding<float>()) {
                material->SetMetallic(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("specular_weight") && param.second.IsHolding<float>()) {
                material->SetSpecular(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("specular_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetSpecularColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("specular_roughness") && param.second.IsHolding<float>()) {
                material->SetRoughness(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("geometry_opacity") && param.second.IsHolding<float>()) {
                material->SetOpacity(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("specular_ior") && param.second.IsHolding<float>()) {
                material->SetIor(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("transmission_weight") && param.second.IsHolding<float>()) {
                material->SetTransmission(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("transmission_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetTransmissionColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("transmission_depth") && param.second.IsHolding<float>()) {
                material->SetTransmissionDepth(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("transmission_scatter") && param.second.IsHolding<GfVec3f>()) {
                material->SetTransmissionScatter(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("subsurface_weight") && param.second.IsHolding<float>()) {
                material->SetSubsurface(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("subsurface_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetSubsurfaceColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("subsurface_radius") && param.second.IsHolding<GfVec3f>()) {
                material->SetSubsurfaceRadius(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("subsurface_radius_scale") && param.second.IsHolding<float>()) {
                material->SetSubsurfaceScale(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("subsurface_anisotropy") && param.second.IsHolding<float>()) {
                material->SetSubsurfaceAnisotropy(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("fuzz_weight") && param.second.IsHolding<float>()) {
                material->SetSheen(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("fuzz_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetSheenColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("fuzz_roughness") && param.second.IsHolding<float>()) {
                material->SetSheenRoughness(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("coat_weight") && param.second.IsHolding<float>()) {
                material->SetCoat(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("coat_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetCoatColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("coat_roughness") && param.second.IsHolding<float>()) {
                material->SetCoatRoughness(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("coat_ior") && param.second.IsHolding<float>()) {
                material->SetCoatIor(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("geometry_thin_walled") && param.second.IsHolding<bool>()) {
                material->SetThinWalled(param.second.UncheckedGet<bool>());
            } else if (param.first == TfToken("emission_luminance") && param.second.IsHolding<float>()) {
                material->SetEmission(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("emission_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetEmissionColor(param.second.UncheckedGet<GfVec3f>());
            }
        }
        material->SetDiffuseColor(baseColor * base);
    } else if (shaderId == TfToken("UsdUVTexture") ||
               shaderId == TfToken("ND_image_color3") ||
               shaderId == TfToken("ND_image") ||
               shaderId == TfToken("ND_image_float") ||
               shaderId == TfToken("ND_image_vector2") ||
               shaderId == TfToken("ND_image_vector3") ||
               shaderId == TfToken("ND_image_vector4") ||
               shaderId == TfToken("ND_image_color4")) {
        
        // Only map to diffuse texture if we are on a path leading to diffuse color
        if (targetInput == TfToken("diffuseColor") || targetInput == TfToken("base_color") || targetInput == TfToken("base")) {
            for (auto const& param : node.parameters) {
                if ((param.first == TfToken("file") || param.first == TfToken("texcoord")) && 
                    param.second.IsHolding<SdfAssetPath>()) {
                    material->SetDiffuseTexture(param.second.UncheckedGet<SdfAssetPath>());
                    HdRestir_LOG << "[Restir]       Mapped diffuse texture: " << material->GetDiffuseTexture().GetAssetPath() << std::endl;
                }
            }
        } else if (targetInput == TfToken("metalness") || targetInput == TfToken("base_metalness")) {
            for (auto const& param : node.parameters) {
                if ((param.first == TfToken("file") || param.first == TfToken("texcoord")) && 
                    param.second.IsHolding<SdfAssetPath>()) {
                    material->SetMetallicTexture(param.second.UncheckedGet<SdfAssetPath>());
                    HdRestir_LOG << "[Restir]       Mapped metallic texture: " << material->GetMetallicTexture().GetAssetPath() << std::endl;
                }
            }
        } else if (targetInput == TfToken("specular_roughness") || targetInput == TfToken("roughness")) {
            for (auto const& param : node.parameters) {
                if ((param.first == TfToken("file") || param.first == TfToken("texcoord")) && 
                    param.second.IsHolding<SdfAssetPath>()) {
                    material->SetRoughnessTexture(param.second.UncheckedGet<SdfAssetPath>());
                    HdRestir_LOG << "[Restir]       Mapped roughness texture: " << material->GetRoughnessTexture().GetAssetPath() << std::endl;
                }
            }
        } else if (targetInput == TfToken("emissiveColor") || targetInput == TfToken("emission_color") || targetInput == TfToken("emission") || targetInput == TfToken("emission_luminance")) {
             for (auto const& param : node.parameters) {
                if ((param.first == TfToken("file") || param.first == TfToken("texcoord")) && 
                    param.second.IsHolding<SdfAssetPath>()) {
                    HdRestir_LOG << "[Restir]       Mapped emissive texture: " << param.second.UncheckedGet<SdfAssetPath>().GetAssetPath() << std::endl;
                    // For now, if diffuse is empty, use emissive as a placeholder so it shows up
                    if (material->GetDiffuseTexture().GetAssetPath().empty()) {
                        material->SetDiffuseTexture(param.second.UncheckedGet<SdfAssetPath>());
                    }
                }
            }
        } else if (targetInput == TfToken("normal")) {
             for (auto const& param : node.parameters) {
                if ((param.first == TfToken("file") || param.first == TfToken("texcoord")) && 
                    param.second.IsHolding<SdfAssetPath>()) {
                    material->SetNormalTexture(param.second.UncheckedGet<SdfAssetPath>());
                    HdRestir_LOG << "[Restir]       Mapped normal texture: " << material->GetNormalTexture().GetAssetPath() << std::endl;
                }
            }
        }
    }

    // Walk upstream recursively
    for (auto const& connPair : node.inputConnections) {
        TfToken inputName = connPair.first;
        // Propagate targetInput. If it's empty, we use inputName as the start of a new chain.
        TfToken nextTarget = targetInput.IsEmpty() ? inputName : targetInput;

        for (auto const& conn : connPair.second) {
            _ProcessNodeUpstream(network, conn.upstreamNode, visited, material, nextTarget);
        }
    }
}

void
HdRestirMaterial::Sync(HdSceneDelegate *sceneDelegate,
                       HdRenderParam   *renderParam,
                       HdDirtyBits     *dirtyBits)
{
    bool materialChanged{false};

    if (*dirtyBits & HdMaterial::DirtyResource) {
        VtValue materialResource = sceneDelegate->GetMaterialResource(GetId());
        if (materialResource.IsHolding<HdMaterialNetworkMap>()) {
            HdMaterialNetworkMap const& map = materialResource.UncheckedGet<HdMaterialNetworkMap>();
            
            // Convert to HdMaterialNetwork2
            HdMaterialNetwork2 network = HdConvertToHdMaterialNetwork2(map);

            HdRestir_LOG << "[Restir] Syncing material " << GetId().GetText() << ":" << std::endl;
            
            std::set<SdfPath> visited;
            
            HdRestir_LOG << "[Restir]   Terminals found:";
            for (auto const& t : network.terminals) HdRestir_LOG << " " << t.first.GetText();
            HdRestir_LOG << std::endl;

            // Find the best terminal
            TfToken selectedTerminal;
            TfToken terminalPriorities[] = { 
                TfToken("mtlx:surface"), 
                HdMaterialTerminalTokens->surface,
                TfToken("outputs:surface") 
            };

            for (const auto& t : terminalPriorities) {
                if (network.terminals.count(t)) {
                    selectedTerminal = t;
                    break;
                }
            }

            // Fallback to first if none of the above found
            if (selectedTerminal.IsEmpty() && !network.terminals.empty()) {
                selectedTerminal = network.terminals.begin()->first;
            }

            if (!selectedTerminal.IsEmpty()) {
                const SdfPath& terminalPath = network.terminals.at(selectedTerminal).upstreamNode;
                HdRestir_LOG << "[Restir]   Selected terminal: " << selectedTerminal.GetText() << " -> " << terminalPath.GetText() << std::endl;
                _ProcessNodeUpstream(network, terminalPath, visited, this);
                
                HdRestir_LOG << "[Restir]   Final Params: Diffuse=" << _params.DiffuseColor << " | Emission=" << (_params.EmissionColor * _params.Emission)
                          << " (Color=" << _params.EmissionColor << ", Weight=" << _params.Emission << ") | Opacity=" << _params.Opacity
                          << " | Transmission=" << _params.Transmission << " (Color=" << _params.TransmissionColor << ")" << std::endl;
            } else {
                HdRestir_LOG << "[Restir]   No terminals found in network!" << std::endl;
            }

            materialChanged = true;
        }
    }

    if (materialChanged) {
        auto* restirRenderParam{static_cast<HdRestirRenderParam*>(renderParam)};
        restirRenderParam->EditScene([&](Restir::Scene& scene) {
            scene.SetMaterialParams(GetId(), GetParams());
        });
    }

    *dirtyBits = HdMaterial::Clean;
}

HdDirtyBits
HdRestirMaterial::GetInitialDirtyBitsMask() const
{
    return HdMaterial::AllDirty;
}
