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

// Represents a node in the non-grid Cursor Navigation Graph
struct NavNode {
    std::string up;
    std::string down;
    std::string left;
    std::string right;
};

} // namespace ui
} // namespace m8
