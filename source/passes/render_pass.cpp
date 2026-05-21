#include "render_pass.h"

namespace Restir {

RenderPass::RenderPass(std::string name) noexcept
    : _name(std::move(name))
{
}

}  // namespace Restir
