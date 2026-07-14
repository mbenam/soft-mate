#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"

namespace m8 {
namespace ui {
namespace groove {

void RenderGrooveScreen(Renderer& renderer, 
                        const engine::EngineState& engState,
                        const engine::Groove& grooveData,
                        int currentGrooveIndex,
                        int cursor_y);

} // namespace groove
} // namespace ui
} // namespace m8
