#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include <string>

namespace m8 {
namespace ui {
namespace scale {

void RenderScaleScreen(Renderer& renderer, 
                       const engine::EngineState& engState, 
                       int currentScaleIndex,
                       const std::string& active_cursor_id);

} // namespace scale
} // namespace ui
} // namespace m8
