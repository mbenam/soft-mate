#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include <string>

namespace m8 {
namespace ui {
namespace mods {

void RenderModScreen(Renderer& renderer, 
                     const engine::EngineState& engState, 
                     int currentInstIndex,
                     const std::string& active_cursor_id);

} // namespace mods
} // namespace ui
} // namespace m8
