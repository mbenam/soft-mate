#pragma once
#include <string>

namespace m8 {
namespace ui {

struct UI_GridCell {
    std::string text;
    int col;
    int row;
    std::string normal_color;
    std::string selected_color;
    std::string role;
    bool has_cursor_box;
    int width = 0; // Used for proportional slider rendering
};

// Represents a node in the non-grid Cursor Navigation Graph. CursorId is a
// per-screen `enum class` whose NONE value marks "no neighbor in that direction".
template <typename CursorId>
struct NavNode {
    CursorId up;
    CursorId down;
    CursorId left;
    CursorId right;
};

} // namespace ui
} // namespace m8
