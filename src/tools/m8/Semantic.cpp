#include "Semantic.h"
#include "Primitives.h"   // gridCursorPosition
#include <sstream>

namespace m8 {
namespace dev {

static std::string escapeJson(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else o += c;
    }
    return o;
}

SemanticState semanticState(M8Device& dev) {
    SemanticState state;
    state.screen = identifyScreen(dev.grid());
    state.screenName = dev.grid().canon();
    state.isModal = m8::dev::isModal(dev.grid());
    state.isLiveMode = m8::dev::isLiveMode(dev.grid());
    state.settled = dev.lastRead().settled;

    auto cf = dev.cursorField();
    if (cf) {
        state.cursorField = cf->name;
        state.cursorRow = cf->row;
        // NOT valueOf(*cf): cf->col is the x of the FIRST accent cell on the row,
        // which is the field's LABEL, and valueAt() reads forward from whatever
        // column it is given -- so it returned the label ("TEMPO   120" reported
        // a value of "TEMPO"). cursorValueText strips the label using the
        // screen's own field map instead.
        state.cursorValue = cursorValueText(dev.grid());
    } else {
        state.cursorRow = dev.grid().cursorRowY();
    }

    // Grid coordinates, when this is a grid screen at all. gridCursorPosition
    // returns valid == false on form screens (no step-0 row label / no column
    // header), which leaves these at their -1 defaults.
    if (auto gc = gridCursorPosition(dev.grid()); gc.valid) {
        state.gridStep    = gc.step;
        state.gridCol     = gc.col;
        state.gridColumns = gc.columns;
    }

    state.rows = dev.listRows();
    return state;
}

std::string SemanticState::toJson() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"screen\": \"" << escapeJson(screenName) << "\",\n";
    ss << "  \"is_modal\": " << (isModal ? "true" : "false") << ",\n";
    ss << "  \"is_live_mode\": " << (isLiveMode ? "true" : "false") << ",\n";
    ss << "  \"settled\": " << (settled ? "true" : "false") << ",\n";
    ss << "  \"cursor_field\": \"" << escapeJson(cursorField) << "\",\n";
    ss << "  \"cursor_value\": \"" << escapeJson(cursorValue) << "\",\n";
    ss << "  \"cursor_row\": " << cursorRow << ",\n";
    ss << "  \"grid_step\": " << gridStep << ",\n";
    ss << "  \"grid_col\": " << gridCol << ",\n";
    ss << "  \"grid_columns\": " << gridColumns << ",\n";
    ss << "  \"rows\": [\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        ss << "    {\"y\": " << rows[i].first << ", \"text\": \"" << escapeJson(rows[i].second) << "\"}";
        if (i + 1 < rows.size()) ss << ",";
        ss << "\n";
    }
    ss << "  ]\n";
    ss << "}";
    return ss.str();
}

} // namespace dev
} // namespace m8
