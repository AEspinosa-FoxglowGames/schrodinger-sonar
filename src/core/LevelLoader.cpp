#include "app/app.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <cmath>

// This project may be built with C++14; avoid std::clamp.
static int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float Hash12(float x, float y) {
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return h - std::floor(h);
}

static float EaseInOut(float t) {
    // smoothstep
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

static void AddRectPts(std::vector<glm::vec2>& pts, float x0, float y0, float x1, float y1, float spacing) {
    if (spacing <= 0.0f) spacing = 8.0f;
    for (float y = y0; y <= y1; y += spacing) {
        for (float x = x0; x <= x1; x += spacing) {
            pts.emplace_back(x, y);
        }
    }
}

// Level loading + validation split from app.cpp.

// -----------------------------
// Levels
// -----------------------------

static bool IsBlankLine(const std::string& s) {
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\r') return false;
    }
    return true;
}

bool App::LoadLevelsFile(const char* path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[levels] Could not open: " << path << "\n";
        return false;
    }

    std::vector<std::vector<std::string>> levels;
    std::vector<std::string> cur;
    std::string line;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == ';') continue; // comment

        if (IsBlankLine(line)) {
            if (!cur.empty()) {
                levels.push_back(cur);
                cur.clear();
            }
            continue;
        }

        cur.push_back(line);
    }
    if (!cur.empty()) levels.push_back(cur);

    // Validate each level, keep only valid.
    std::vector<std::vector<std::string>> valid;
    valid.reserve(levels.size());

    auto okGlyph = [](char c) {
        switch (c) {
        case '#': case '.': case ' ': case '0': case '1':
        case 'P': case 'p': case 'H': case 'D': case 'E':
        case 'T': case 'B': case 'C':
            return true;
        default:
            return false;
        }
        };

    for (size_t li = 0; li < levels.size(); ++li) {
        const auto& rows = levels[li];
        int h = (int)rows.size();
        int w = (int)rows[0].size();
        bool bad = false;
        int spawnCount = 0;
        int exitCount = 0;

        for (int y = 0; y < h; ++y) {
            if ((int)rows[y].size() != w) {
                std::cerr << "[levels] Level " << (li + 1) << ": ragged row at y=" << y << "\n";
                bad = true; break;
            }
            for (int x = 0; x < w; ++x) {
                char c = rows[y][x];
                if (!okGlyph(c)) {
                    std::cerr << "[levels] Level " << (li + 1) << ": unknown glyph '" << c << "' at (" << x << "," << y << ")\n";
                    bad = true; break;
                }
                if (c == 'P' || c == 'p') spawnCount++;
                if (c == 'E') exitCount++;
            }
            if (bad) break;
        }

        if (!bad) {
            if (spawnCount > 1) {
                std::cerr << "[levels] Level " << (li + 1) << ": multiple spawns ('P')\n";
                bad = true;
            }
            if (exitCount < 1) {
                std::cerr << "[levels] Level " << (li + 1) << ": missing exit ('E')\n";
                bad = true;
            }
        }

        if (!bad) valid.push_back(rows);
    }

    m_levels = std::move(valid);
    std::cerr << "[levels] Loaded " << m_levels.size() << " valid levels from " << path << "\n";
    m_levelSelectIndex = ClampInt(m_levelSelectIndex, 0, std::max(0, (int)m_levels.size() - 1));
    m_currentLevel = ClampInt(m_currentLevel, 0, std::max(0, (int)m_levels.size() - 1));
    return !m_levels.empty();
}

bool App::LoadLevelIndex(int idx) {
    if (m_levels.empty()) return false;
    idx = ClampInt(idx, 0, (int)m_levels.size() - 1);
    m_currentLevel = idx;
    m_levelSelectIndex = idx;

    // Extract crates (C) into runtime entities, and remove them from the static map.
    std::vector<std::string> rows = m_levels[idx];
    ResetCratesFromLevelRows(rows);

    m_map.LoadFromAscii(rows);
    BuildWallPoints();
    ResetLevelRuntime();
    return true;
}

void App::ResetCratesFromLevelRows(std::vector<std::string>& rows) {
    m_crates.clear();
    if (rows.empty()) return;

    const int h = (int)rows.size();
    const int w = (int)rows[0].size();

    // Note: rows are authored top-to-bottom, but the game world's Y grows upward.
    // Match GridMap's mapping: srcY=0 -> y=(h-1)
    for (int srcY = 0; srcY < h; ++srcY) {
        int y = (h - 1) - srcY;
        for (int x = 0; x < w; ++x) {
            if (rows[srcY][x] == 'C' || rows[srcY][x] == 'c') {
                m_crates.push_back({ x, y });
                rows[srcY][x] = '.'; // remove from static map
            }
        }
    }

    // Save the authored start state so R reset can restore it.
    m_cratesStart = m_crates;
}

void App::ResetLevelRuntime() {
    m_won = false;
    m_winTime = -1e9f;
    m_dying = false;
    m_deathT = 0.0f;

    m_energy = 1.0f;

    // Door controls
    m_doorsOpenByPlate = false;
    m_doorsOpenByButton = false;
    m_prevButtonCellX = -9999;
    m_prevButtonCellY = -9999;

    // Restore crates to their authored positions
    if (!m_cratesStart.empty()) {
        m_crates = m_cratesStart;
    }

    // Restore crates to their authored start positions.
    m_crates = m_cratesStart;

    // Spawn
    if (m_map.HasPlayerSpawn()) {
        m_playerPos = { m_map.PlayerSpawnWorldX(), m_map.PlayerSpawnWorldY() };
    }
    m_playerSpawn = m_playerPos;

    // Reset reveal/waves
    m_waves.clear();
    m_prevSpaceDown = false;
    EnsureRevealGrid();
    std::fill(m_revealUntil.begin(), m_revealUntil.end(), -1e9f);

    // Reset wall hit times
    m_wallLastHitTime.assign(m_wallPoints.size(), -1e9f);
    m_wallLastHitStrength.assign(m_wallPoints.size(), 0.0f);
    m_wallLitPoints.clear();

    m_lastHazardHitTime = -1e9f;
}

void App::NextLevel() {
    if (m_levels.empty()) return;
    int next = m_currentLevel + 1;
    if (next >= (int)m_levels.size()) next = 0;
    LoadLevelIndex(next);
}

// -----------------------------
// Death
// -----------------------------

void App::EnterDeath() {
    if (m_dying) return;
    m_dying = true;
    m_deathT = 0.0f;
    m_deathPos = m_playerPos;
    m_waves.clear();
}

void App::UpdateDeath(float dt) {
    if (!m_dying) return;
    m_deathT += dt;
    if (m_deathT >= m_deathDuration) {
        m_dying = false;
        m_deathT = 0.0f;
        // Respawn
        m_playerPos = m_playerSpawn;
        m_lastHazardHitTime = m_nowSeconds;
    }
}

void App::DrawDeathFX(int fbW, int fbH) {
    if (!m_dying) return;

    float t = (m_deathDuration <= 1e-6f) ? 1.0f : (m_deathT / m_deathDuration);
    t = Clamp01(t);
    float e = EaseInOut(t);

    // Expand ring in world space
    const glm::vec2 worldMin(0.0f, 0.0f);
    const glm::vec2 worldMax((float)m_map.Width(), (float)m_map.Height());

    std::vector<glm::vec2> ring;
    ring.reserve(220);
    float radius = 0.2f + 4.0f * e;
    for (int i = 0; i < 220; ++i) {
        float a = (float)i / 220.0f * 6.2831853f;
        ring.emplace_back(m_deathPos.x + std::cos(a) * radius, m_deathPos.y + std::sin(a) * radius);
    }
    m_renderer.DrawPointsAdditive(ring, worldMin, worldMax, glm::vec4(1.0f, 0.25f, 0.85f, 0.45f * (1.0f - e)), 10.0f, m_nowSeconds);

    // Shards
    std::vector<glm::vec2> shards;
    shards.reserve(160);
    for (int i = 0; i < 160; ++i) {
        float ang = Hash12((float)i, 9.3f) * 6.2831853f;
        float spd = 1.0f + 3.0f * Hash12((float)i, 31.7f);
        float d = spd * e;
        shards.emplace_back(m_deathPos.x + std::cos(ang) * d, m_deathPos.y + std::sin(ang) * d);
    }
    m_renderer.DrawPointsAdditive(shards, worldMin, worldMax, glm::vec4(0.55f, 0.90f, 1.0f, 0.35f * (1.0f - e)), 7.0f, m_nowSeconds);

    // Screen-space dark pulse
    std::vector<glm::vec2> ov;
    AddRectPts(ov, 0.0f, 0.0f, (float)fbW, (float)fbH, 16.0f);
    m_renderer.DrawPoints(ov, { 0,0 }, { (float)fbW, (float)fbH }, glm::vec4(0, 0, 0, 0.20f * e), 16.0f, m_nowSeconds);
}


