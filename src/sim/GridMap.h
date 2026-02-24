#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// GridMap stores TWO map layers:
//   - Unseen: how tiles behave when NOT revealed (default gameplay physics)
//   - Seen  : how tiles behave when revealed (after a ping)
//
// LoadFromAscii uses a legend (char -> tile behavior). Extend it with
// SetLegend()/AddLegendEntry() before calling LoadFromAscii().
class GridMap {
public:
    enum class Layer : uint8_t { Unseen = 0, Seen = 1 };

    // Expand this as game grows.
    enum class Tile : uint8_t {
        Empty = 0,
        Wall,
        // "Ghost wall": solid only when revealed (seen layer), empty when unseen.
        GhostWall,
        Hazard,
        // "Door": solid in both layers unless opened by triggers (handled in gameplay code).
        Door,
        Exit,
        Plate,
        Button,
    };

    // Legend entry: how a glyph behaves in each layer.
    // If playerSpawn=true, the glyph also sets the player start position.
    struct CellDef {
        Tile unseen = Tile::Empty;
        Tile seen = Tile::Empty;
        bool playerSpawn = false;
    };

    GridMap();

    // Legend management (optional). has defaults otherwise
    void SetLegend(const std::unordered_map<char, CellDef>& legend);
    void AddLegendEntry(char glyph, const CellDef& def);
    void ResetLegendToDefault();

    // Authoring helpers
    bool IsGlyphKnown(char glyph) const;
    std::string KnownGlyphs() const;

    void LoadFromAscii(const std::vector<std::string>& rows);

    int Width() const { return m_width; }
    int Height() const { return m_height; }

    bool InBounds(int x, int y) const;

    // Layer-aware queries.
    Tile GetTile(int x, int y, Layer layer = Layer::Unseen) const;

    // Back-compat: current code only draws/queries walls.
    bool IsWall(int x, int y, Layer layer = Layer::Unseen) const;

    bool IsGhostWall(int x, int y, Layer layer = Layer::Unseen) const {
        return InBounds(x, y) && GetTile(x, y, layer) == Tile::GhostWall;
    }

    bool IsDoor(int x, int y, Layer layer = Layer::Unseen) const {
        return InBounds(x, y) && GetTile(x, y, layer) == Tile::Door;
    }

    bool IsBlocked(int x, int y, Layer layer = Layer::Unseen) const;

    // Hazards are *walkable* by default (not blocking), but can apply damage.
    bool IsHazard(int x, int y, Layer layer = Layer::Unseen) const {
        return InBounds(x, y) && GetTile(x, y, layer) == Tile::Hazard;
    }

    bool IsPlate(int x, int y, Layer layer = Layer::Unseen) const {
        return InBounds(x, y) && GetTile(x, y, layer) == Tile::Plate;
    }

    bool IsPlateAnyLayer(int x, int y) const {
        // Plates can exist in both layers (so they can be held even when unseen).
        return InBounds(x, y) &&
            (GetTile(x, y, Layer::Unseen) == Tile::Plate || GetTile(x, y, Layer::Seen) == Tile::Plate);
    }

    bool IsButton(int x, int y, Layer layer = Layer::Unseen) const {
        return InBounds(x, y) && GetTile(x, y, layer) == Tile::Button;
    }

    bool IsButtonAnyLayer(int x, int y) const {
        return InBounds(x, y) &&
            (GetTile(x, y, Layer::Unseen) == Tile::Button || GetTile(x, y, Layer::Seen) == Tile::Button);
    }

    // "Empty" for pathing / BFS / movement. Anything non-blocking counts as empty.
    bool IsEmpty(int x, int y, Layer layer = Layer::Unseen) const {
        return InBounds(x, y) && !IsBlocked(x, y, layer);
    }

    // Player start (set by 'P'/'p' by default).
    bool HasPlayerSpawn() const { return m_hasPlayerSpawn; }
    int PlayerSpawnCellX() const { return m_spawnX; }
    int PlayerSpawnCellY() const { return m_spawnY; }
    float PlayerSpawnWorldX() const { return m_spawnX + 0.5f; }
    float PlayerSpawnWorldY() const { return m_spawnY + 0.5f; }

private:
    int m_width = 0;
    int m_height = 0;

    // Per-cell tile type for each layer.
    std::vector<uint8_t> m_unseen; // Tile enum values
    std::vector<uint8_t> m_seen;   // Tile enum values

    // Optional: keep original glyph for debugging/authoring.
    std::vector<char> m_glyphs;

    // Glyph -> behavior mapping used by LoadFromAscii.
    std::unordered_map<char, CellDef> m_legend;

    // Spawn extracted from the ASCII map.
    bool m_hasPlayerSpawn = false;
    int  m_spawnX = 0;
    int  m_spawnY = 0;

    int Index(int x, int y) const { return y * m_width + x; }
    static bool IsBlocking(Tile t);
};
