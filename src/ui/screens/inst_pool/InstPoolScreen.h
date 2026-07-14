#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include <vector>

namespace m8 {
namespace ui {
namespace inst_pool {

void RenderInstPoolScreen(Renderer& renderer, 
                          const engine::EngineState& engState,
                          int cursor_x, int cursor_y);

} // namespace inst_pool
} // namespace ui
} // namespace m8
