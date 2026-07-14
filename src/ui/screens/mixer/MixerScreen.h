#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include <string>

namespace m8 {
namespace ui {
namespace mixer {

void RenderMixerScreen(Renderer& renderer, 
                       const engine::EngineState& engState, 
                       const std::string& active_cursor_id);

} // namespace mixer
} // namespace ui
} // namespace m8
