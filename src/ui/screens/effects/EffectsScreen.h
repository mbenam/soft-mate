#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include <string>

namespace m8 {
namespace ui {
namespace effects {

void RenderEffectsScreen(Renderer& renderer, 
                         const engine::EngineState& engState, 
                         const std::string& active_cursor_id);

} // namespace effects
} // namespace ui
} // namespace m8
