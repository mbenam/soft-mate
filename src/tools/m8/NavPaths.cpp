#include "NavPaths.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace m8 {
namespace dev {

NavPaths& getNavPaths() {
    static NavPaths p;
    return p;
}

uint8_t navKeyMask(const std::string& name) {
    if (name == "UP")    return Key::UP;
    if (name == "DOWN")  return Key::DOWN;
    if (name == "LEFT")  return Key::LEFT;
    if (name == "RIGHT") return Key::RIGHT;
    return 0;
}

std::string navPathFileFor(Screen s) {
    for (const auto& si : kScreenTable)
        if (si.id == s) return std::string("hw_crawl/") + si.canonHeader + ".json";
    return std::string();
}

bool NavPaths::loadScreen(Screen s, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Positional parsing of a file this project wrote itself, rather than
    // pulling a JSON library into m8_device for one artifact format. Each stop
    // is one object carrying grid_row, grid_col and a path array of key names.
    size_t q = all.find("stops");
    if (q == std::string::npos) return false;

    size_t added = 0;
    while (true) {
        size_t r = all.find("grid_row", q);
        if (r == std::string::npos) break;
        size_t c = all.find("grid_col", r);
        if (c == std::string::npos) break;
        size_t p = all.find("path", c);
        if (p == std::string::npos) break;

        const int gr = std::atoi(all.c_str() + all.find(':', r) + 1);
        const int gc = std::atoi(all.c_str() + all.find(':', c) + 1);

        size_t open = all.find('[', p);
        size_t close = all.find(']', open);
        if (open == std::string::npos || close == std::string::npos) break;

        NavRoute route;
        bool bad = false;
        size_t k = open;
        while (true) {
            size_t a = all.find('"', k);
            if (a == std::string::npos || a > close) break;
            size_t b = all.find('"', a + 1);
            if (b == std::string::npos || b > close) break;
            const std::string tok = all.substr(a + 1, b - a - 1);
            const uint8_t mask = navKeyMask(tok);
            if (mask == 0) { bad = true; break; }
            route.push_back(mask);
            k = b + 1;
        }

        // A route with an unrecognised key is dropped whole. Replaying part of
        // a route lands the cursor somewhere arbitrary, which is worse than
        // falling back to the walker.
        if (!bad) {
            m_routes[Key{ static_cast<int>(s), gr, gc }] = route;
            ++added;
        }
        q = close;
    }
    return added > 0;
}

bool NavPaths::ensureLoaded(Screen s) {
    const int key = static_cast<int>(s);
    auto it = m_tried.find(key);
    if (it != m_tried.end()) return it->second;
    const std::string path = navPathFileFor(s);
    const bool ok = !path.empty() && loadScreen(s, path);
    m_tried[key] = ok;
    return ok;
}

const NavRoute* NavPaths::route(Screen s, int gridRow, int gridCol) const {
    auto it = m_routes.find(Key{ static_cast<int>(s), gridRow, gridCol });
    return it == m_routes.end() ? nullptr : &it->second;
}

size_t NavPaths::routeCount(Screen s) const {
    size_t n = 0;
    for (const auto& kv : m_routes)
        if (kv.first.screen == static_cast<int>(s)) ++n;
    return n;
}

} // namespace dev
} // namespace m8
