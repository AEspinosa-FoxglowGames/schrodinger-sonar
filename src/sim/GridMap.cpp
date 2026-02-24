#include "GridMap.h"

#include <algorithm>
#include <cassert>
#include <sstream>

namespace {
    // Helper: trim trailing whitespace (lets you indent ASCII maps nicely).
    static std::string rtrim_copy(const std::string& s) {
        size_t end = s.size();
        while (end > 0) {
            char c = s[end - 1];
            if (c == ' ' || c == '\t' || c == '\r') end--;
            else break;
        }
        return s.substr(0, end);
    }
}

GridMap::GridMap() {
    ResetLegendToDefault();
}

bool GridMap::IsGlyphKnown(char glyph) const {
    return m_legend.find(glyph) != m_legend.end();
}

std::string GridMap::KnownGlyphs() const {
    // For error messages: return glyphs in a stable, readable order.
    std::vector<char> keys;
    keys.reserve(m_legend.size());
    for (const auto& kv : m_legend) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());

    std::ostringstream oss;
    for (char c : keys) {
        if (c == '\n' || c == '\r' || c == '\t') continue;
        if (c == ' ') oss << "[space] ";
        else oss << "'" << c << "' ";
    }
    return oss.str();
}

void GridMap::SetLegend(const std::unordered_map<char, CellDef>& legend) {
    m_legend = legend;
}

void GridMap::AddLegendEntry(char glyph, const CellDef& def) {
    m_legend[glyph] = def;
}

void GridMap::ResetLegendToDefault() {
    m_legend.clear();

    // Basics
    m_legend['.'] = { Tile::Empty,  Tile::Empty,  false };
    m_legend[' '] = { Tile::Empty,  Tile::Empty,  false };
    m_legend['0'] = { Tile::Empty,  Tile::Empty,  false };

    // Walls (present in both layers)
    m_legend['#'] = { Tile::Wall,   Tile::Wall,   false };
    m_legend['1'] = { Tile::Wall,   Tile::Wall,   false };

    // Player start: treated as empty tiles, but records spawn.
    m_legend['P'] = { Tile::Empty,  Tile::Empty,  true };
    m_legend['p'] = { Tile::Empty,  Tile::Empty,  true };

    // Hazard: hurts only when UNSEEN (seen layer is empty).
    // Hazards are WALKABLE by default (non-blocking); damage is handled by gameplay code.
    m_legend['H'] = { Tile::Hazard, Tile::Empty,  false };

    // Ghost wall (formerly 'D'): blocks ONLY in the SEEN layer.
    m_legend['G'] = { Tile::Empty,  Tile::GhostWall, false };

    // Door: blocks in BOTH layers; gameplay code can override collision when opened.
    m_legend['D'] = { Tile::Door,   Tile::Door,   false };

    // Exit: exists in both layers (non-blocking), gameplay checks for win.
    m_legend['E'] = { Tile::Exit, Tile::Exit, false };

    // Pressure plate / button:
    //  - Plates must be pressable even when unseen (so you can leave a crate on one and walk away).
    //  - Buttons can remain "observation-gated" if desired, but we keep them present in both layers
    //    for consistent parsing & collision (they are non-blocking either way).
    m_legend['T'] = { Tile::Plate,  Tile::Plate,  false };
    m_legend['t'] = m_legend['T'];
    m_legend['B'] = { Tile::Button, Tile::Button, false };
    m_legend['b'] = m_legend['B'];
}

bool GridMap::IsBlocking(Tile t) {
    switch (t) {
    case Tile::Wall:
    case Tile::GhostWall:
    case Tile::Door:
        return true;
    default:
        return false;
    }
}

void GridMap::LoadFromAscii(const std::vector<std::string>& inputRows) {
    assert(!inputRows.empty());

    // 1) Trim trailing whitespace so you can indent levels in code.
    std::vector<std::string> rows;
    rows.reserve(inputRows.size());
    for (const auto& r : inputRows) {
        rows.push_back(rtrim_copy(r));
    }

    // 2) Determine width as the maximum row length (pads short rows with empty).
    m_height = (int)rows.size();
    m_width = 0;
    for (const auto& r : rows) {
        m_width = std::max(m_width, (int)r.size());
    }
    assert(m_width > 0);

    const int count = m_width * m_height;
    m_unseen.assign(count, (uint8_t)Tile::Empty);
    m_seen.assign(count, (uint8_t)Tile::Empty);
    m_glyphs.assign(count, '.');

    m_hasPlayerSpawn = false;
    m_spawnX = m_spawnY = 0;

    // 3) Fill cells.
    // NOTE: ASCII maps are authored top-to-bottom, but the game world's Y grows upward.
    // So row 0 (top line) should land at y = (height - 1).
    for (int srcY = 0; srcY < m_height; ++srcY) {
        const int y = (m_height - 1) - srcY;
        const std::string& row = rows[srcY];
        for (int x = 0; x < m_width; ++x) {
            const char glyph = (x < (int)row.size()) ? row[x] : '.'; // pad with empty
            const int idx = Index(x, y);
            m_glyphs[idx] = glyph;

            auto it = m_legend.find(glyph);
            if (it == m_legend.end()) {
                // Unknown glyph => treat as empty, but keep the glyph for debugging.
                // (You can assert here if you prefer strict authoring.)
                m_unseen[idx] = (uint8_t)Tile::Empty;
                m_seen[idx] = (uint8_t)Tile::Empty;
                continue;
            }

            const CellDef& def = it->second;
            m_unseen[idx] = (uint8_t)def.unseen;
            m_seen[idx] = (uint8_t)def.seen;

            if (def.playerSpawn) {
                m_hasPlayerSpawn = true;
                m_spawnX = x;
                m_spawnY = y;
            }
        }
    }
}

bool GridMap::InBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < m_width && y < m_height;
}

GridMap::Tile GridMap::GetTile(int x, int y, Layer layer) const {
    if (!InBounds(x, y)) return Tile::Wall; // treat outside as solid
    const int idx = Index(x, y);
    const uint8_t v = (layer == Layer::Seen) ? m_seen[idx] : m_unseen[idx];
    return (Tile)v;
}

bool GridMap::IsWall(int x, int y, Layer layer) const {
    return GetTile(x, y, layer) == Tile::Wall;
}

bool GridMap::IsBlocked(int x, int y, Layer layer) const {
    return IsBlocking(GetTile(x, y, layer));
}
