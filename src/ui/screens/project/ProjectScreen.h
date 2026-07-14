#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include <string>

namespace m8 {
namespace ui {
namespace project {

void RenderProjectScreen(Renderer& renderer, 
                         const engine::EngineState& engState, 
                         const std::string& active_cursor_id);

} // namespace project
} // namespace ui
} // namespace m8
