#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include <string>

namespace m8 {
namespace ui {
namespace instrument {

void RenderInstrumentScreen(Renderer& renderer, 
                            const engine::EngineState& engState, 
                            int currentInstIndex,
                            const std::string& active_cursor_id);

} // namespace instrument
} // namespace ui
} // namespace m8
