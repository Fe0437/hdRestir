#pragma once

#include "buffer_user.h"
#include "direct_light_integrator_interface.h"
#include "not_null_unique_ptr.h"
#include "scene_interface.h"

namespace Restir
{

    class IDirectLightIntegratorFactory
    {
      public:
        virtual ~IDirectLightIntegratorFactory() = default;

        [[nodiscard]] virtual NotNullUniquePtr<IDirectLightIntegrator> Create(const IScene    &scene,
                                                                              IBufferProvider &provider) const = 0;

        [[nodiscard]] virtual IBufferStager *GetBufferStager()
        {
            return nullptr;
        }
    };

} // namespace Restir
