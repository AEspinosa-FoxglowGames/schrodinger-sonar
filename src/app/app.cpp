#include "app/app.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <glm/geometric.hpp>
#include <iostream>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static void PrintGLInfo() {
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::cout << "OpenGL loaded. Vendor: " << (vendor ? vendor : "?")
        << " | Renderer: " << (renderer ? renderer : "?")
        << " | Version: " << (version ? version : "?") << "\n";
}

static float ClampF(float x, float a, float b) {
    return (x < a) ? a : (x > b) ? b : x;
}
static float Clamp01(float x) { return ClampF(x, 0.0f, 1.0f); }

static float Hash12(float x, float y) {
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return h - std::floor(h);
}

// Sonar radius curve in BFS units (≈ tiles)
static float WaveRadius(float ageSeconds) {
    const float v0 = 10.5f;
    const float k = 0.95f;
    return (v0 / k) * (1.0f - std::exp(-k * ageSeconds));
}

// Add evenly spaced points along a segment.
static void AddSegmentSamples(std::vector<glm::vec2>& out,
    const glm::vec2& a,
    const glm::vec2& b,
    float spacing,
    float oscAmp,
    float oscPhase,
    float oscFreq)
{
    glm::vec2 d = b - a;
    float len = glm::length(d);
    if (len < 1e-6f) return;

    glm::vec2 t = d / len;               // tangent
    glm::vec2 n(-t.y, t.x);              // normal

    int steps = std::max(1, (int)std::floor(len / spacing));
    float inv = 1.0f / (float)steps;

    for (int i = 0; i <= steps; ++i) {
        float u = (float)i * inv;
        glm::vec2 p = a + d * u;

        // subtle traveling oscillation, not “noisy”
        float w = std::sin(oscPhase + u * oscFreq);
        p += n * (oscAmp * w);

        out.push_back(p);
    }
}

// Linear interpolation parameter where value crosses iso between v0 and v1
static float IsoT(float v0, float v1, float iso) {
    float dv = v1 - v0;
    if (std::fabs(dv) < 1e-6f) return 0.5f;
    return (iso - v0) / dv;
}

bool App::Init() {
    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(1280, 720, "Sonar POC", nullptr, nullptr);
    if (!m_window) {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "gladLoadGLLoader failed\n";
        glfwDestroyWindow(m_window);
        glfwTerminate();
        return false;
    }

    PrintGLInfo();
    glfwSwapInterval(1);

    if (!m_renderer.Init()) {
        std::cerr << "Renderer::Init failed\n";
        return false;
    }

    // Levels
    // Prefer assets/levels.txt (expected in the project). If missing, fall back to a local levels.txt.
    if (!LoadLevelsFile("assets/levels.txt")) {
        LoadLevelsFile("levels.txt");
    }
    if (!LoadLevelIndex(0)) {
        std::cerr << "[levels] No valid levels loaded. Using built-in fallback.\n";
        m_map.LoadFromAscii({
            "########",
            "#..H.DE#",
            "#.HPH#D#",
            "#..H...#",
            "########",
            });
        BuildWallPoints();
        ResetLevelRuntime();
    }

    m_state = AppState::Title;

    return true;
}


void App::Shutdown() {
    m_renderer.Shutdown();

    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

void App::Run() {
    double prev = glfwGetTime();

    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        const double nowD = glfwGetTime();
        float dt = (float)(nowD - prev);
        prev = nowD;

        dt = std::min(dt, 0.1f);

        const float now = (float)nowD;
        m_nowSeconds = now;

        // State updates
        if (m_state == AppState::Transition) {
            UpdateTransition(dt);
        }

        if (m_state == AppState::Playing || m_state == AppState::Transition) {
            // Maintain waves / reveal
            m_waves.erase(
                std::remove_if(m_waves.begin(), m_waves.end(),
                    [this, now](const Wave& w) { return (now - w.t0) > m_waveMaxAge; }),
                m_waves.end()
            );

            ApplyWaveReveal(now);
        }

        // Death timing (gameplay only)
        if (m_state == AppState::Playing || m_state == AppState::Transition) {
            UpdateDeath(dt);
        }

        HandleInput(dt);

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(m_window, &fbW, &fbH);
        m_renderer.BeginFrame(fbW, fbH);

        // Render
        if (m_state == AppState::Title) {
            DrawTitleScreen(fbW, fbH);
            glfwSwapBuffers(m_window);
            continue;
        }
        if (m_state == AppState::LevelSelect) {
            DrawLevelSelect(fbW, fbH);
            glfwSwapBuffers(m_window);
            continue;
        }

        const glm::vec2 worldMin(0.0f, 0.0f); // Creating these every frame might be a bit pricy if expanded...
        const glm::vec2 worldMax((float)m_map.Width(), (float)m_map.Height());//Maybe create them outside the look, then just reallocate variables?

        UpdateWallDotsFromWaves(now);
        {
            std::vector<Renderer::PointA> wallPts;
            std::vector<Renderer::PointA> ghostPts;
            std::vector<Renderer::PointA> doorPts;
            std::vector<Renderer::PointA> doorLinePts;
            wallPts.reserve(m_wallLitPoints.size());
            ghostPts.reserve(m_wallLitPoints.size());
            doorPts.reserve(m_wallLitPoints.size());
            doorLinePts.reserve(m_wallLitPoints.size() * 40);

            auto isWallish = [&](int x, int y) {
                if (!m_map.InBounds(x, y)) return false;
                if (m_map.IsWall(x, y)) return true;
                GridMap::Tile u = m_map.GetTile(x, y, GridMap::Layer::Unseen);
                GridMap::Tile s = m_map.GetTile(x, y, GridMap::Layer::Seen);
                return (u == GridMap::Tile::Door) || (s == GridMap::Tile::GhostWall);
                };

            for (const auto& p : m_wallLitPoints) {
                Renderer::PointA pa{ p.pos, p.a };
                if (p.kind == 0) {
                    wallPts.push_back(pa);
                    continue;
                }
                if (p.kind == 1) {
                    ghostPts.push_back(pa);
                    continue;
                }

                // kind==2 => Door
                doorPts.push_back(pa);

                const int cx = (int)std::floor(p.pos.x);
                const int cy = (int)std::floor(p.pos.y);
                if (!m_map.InBounds(cx, cy)) continue;

                const bool lr = isWallish(cx - 1, cy) && isWallish(cx + 1, cy);
                const bool ud = isWallish(cx, cy - 1) && isWallish(cx, cy + 1);

                glm::vec2 a, b, n;
                if (lr) {
                    a = { (float)cx + 0.02f, (float)cy + 0.5f };
                    b = { (float)cx + 0.98f, (float)cy + 0.5f };
                    n = { 0.0f, 1.0f };
                }
                else if (ud) {
                    a = { (float)cx + 0.5f, (float)cy + 0.02f };
                    b = { (float)cx + 0.5f, (float)cy + 0.98f };
                    n = { 1.0f, 0.0f };
                }
                else {
                    continue;
                }

                glm::vec2 d = b - a;
                float len = glm::length(d);
                if (len < 1e-4f) continue;

                // Thicker line: denser samples + 3 parallel bands.
                const float spacing = 0.06f;
                const float band = 0.09f; // half-thickness in world units
                const float bands[3] = { -band, 0.0f, +band };

                int steps = std::max(1, (int)std::floor(len / spacing));
                float inv = 1.0f / (float)steps;

                for (float off : bands) {
                    glm::vec2 aa = a + n * off;
                    glm::vec2 bb = b + n * off;
                    glm::vec2 dd = bb - aa;

                    for (int i = 0; i <= steps; ++i) {
                        float u = (float)i * inv;
                        glm::vec2 s = aa + dd * u;
                        doorLinePts.push_back({ s, p.a });
                    }
                }
            }

            // Walls (blue)
            m_renderer.DrawPointsAlpha(
                wallPts,
                worldMin, worldMax,
                glm::vec4(0.08f, 0.75f, 1.0f, 0.88f),//rgba
                6.8f,
                now,
                true
            );

            // Ghost walls (gold)
            m_renderer.DrawPointsAlpha(
                ghostPts,
                worldMin, worldMax,
                glm::vec4(1.0f, 0.95f, 0.25f, 0.85f),//rgba
                7.1f,
                now,
                true
            );

            const bool doorsAreOpen = (m_doorsOpenByPlate || m_doorsOpenByButton);
            // Doors: same geometry as ghost walls, but with "powered security door" read.
            // - CLOSED (blocking): bright + pulsing + subtle additive halo.
            // - OPEN (unpowered): muted, low alpha, no halo.
            if (!doorPts.empty() || !doorLinePts.empty()) {
                const float pulse = 0.78f + 0.22f * std::sin(now * 6.0f);
                if (!doorsAreOpen) {
                    // CLOSED / powered
                    const glm::vec4 doorCol = glm::vec4(1.00f, 0.55f, 0.18f, 0.92f * pulse);
                    const glm::vec4 glowCol = glm::vec4(1.00f, 0.42f, 0.12f, 0.20f * pulse);

                    m_renderer.DrawPointsAlpha(doorPts, worldMin, worldMax, doorCol, 7.4f, now, true);
                    m_renderer.DrawPointsAlpha(doorLinePts, worldMin, worldMax, doorCol, 10.2f, now, true);

                    // Subtle additive halo to read as "active barrier".
                    // DrawPointsAlpha can do additive blending for PointA; this avoids needing vec2-only APIs.
                    m_renderer.DrawPointsAlpha(doorPts, worldMin, worldMax, glowCol, 14.0f, now, true, 2);
                    m_renderer.DrawPointsAlpha(doorLinePts, worldMin, worldMax, glowCol, 16.0f, now, true, 2);
                }
                else {
                    // OPEN / unpowered
                    const glm::vec4 doorCol = glm::vec4(0.55f, 0.28f, 0.10f, 0.22f);
                    m_renderer.DrawPointsAlpha(doorPts, worldMin, worldMax, doorCol, 7.0f, now, false);
                    m_renderer.DrawPointsAlpha(doorLinePts, worldMin, worldMax, doorCol, 9.0f, now, false);
                }
            }
        }

        for (const auto& w : m_waves) {
            std::vector<glm::vec2> ring;
            BuildBFSWavefront(ring, w, now);

            float age = now - w.t0;
            float t = Clamp01(age / m_waveMaxAge);

            float alpha = 0.34f * (1.0f - t);
            float size = 8.5f * (0.80f + 0.20f * (1.0f - t));

            m_renderer.DrawPointsAdditive(
                ring, worldMin, worldMax,
                glm::vec4(0.10f, 0.95f, 1.0f, alpha),
                size,
                now
            );
        }

        std::vector<glm::vec2> hazardPts;
        for (int y = 0; y < m_map.Height(); ++y) {
            for (int x = 0; x < m_map.Width(); ++x) {
                if (IsCellRevealed(x, y, now)) continue;
                if (m_map.IsHazard(x, y, GridMap::Layer::Unseen)) {
                    hazardPts.emplace_back(x + 0.5f, y + 0.5f);
                }
            }
        }
        m_renderer.DrawPoints(hazardPts, worldMin, worldMax, { 1.0f, 0.2f, 0.2f, 1.0f }, 16.0f, now);

        // Exit: always visible, at least for now
        std::vector<glm::vec2> exitPts;
        for (int y = 0; y < m_map.Height(); ++y) {
            for (int x = 0; x < m_map.Width(); ++x) {
                if (m_map.GetTile(x, y, GridMap::Layer::Unseen) == GridMap::Tile::Exit) {
                    exitPts.emplace_back(x + 0.5f, y + 0.5f);
                }
            }
        }
        m_renderer.DrawPoints(exitPts, worldMin, worldMax, { 1.0f, 0.65f, 1.0f, 1.0f }, 18.0f, now);

        // Crates / triggers (visible only when revealed)
        DrawTriggers(fbW, fbH);
        DrawCrates(fbW, fbH);

        std::vector<glm::vec2> playerPts;
        playerPts.emplace_back(m_playerPos);
        glm::vec4 playerColor = m_won ? glm::vec4(1.0f, 1.0f, 0.35f, 1.0f)
            : glm::vec4(0.20f, 1.0f, 0.20f, 1.0f);
        m_renderer.DrawPoints(playerPts, worldMin, worldMax, playerColor, 25.0f, now);

        // HUD + FX (screen space)
        DrawHUD(fbW, fbH);
        DrawDeathFX(fbW, fbH);
        if (m_state == AppState::Transition) {
            DrawTransitionOverlay(fbW, fbH);
        }

        glfwSwapBuffers(m_window);
    }
}




void App::BuildWallPoints() {
    m_wallPoints.clear();
    m_wallPoints.reserve((size_t)(m_map.Width() * m_map.Height()));

    m_wallKind.clear();
    m_wallKind.reserve((size_t)(m_map.Width() * m_map.Height()));

    m_cellToWallPoint.assign((size_t)(m_map.Width() * m_map.Height()), -1);

    for (int y = 0; y < m_map.Height(); ++y) {
        for (int x = 0; x < m_map.Width(); ++x) {
            const bool isWall = m_map.IsWall(x, y, GridMap::Layer::Unseen);
            const bool isGhost = (m_map.GetTile(x, y, GridMap::Layer::Seen) == GridMap::Tile::GhostWall);
            const bool isDoor = (m_map.GetTile(x, y, GridMap::Layer::Unseen) == GridMap::Tile::Door);

            if (isWall || isGhost || isDoor) {
                const int idx = y * m_map.Width() + x;
                const int wallIndex = (int)m_wallPoints.size();
                m_wallPoints.emplace_back(x + 0.5f, y + 0.5f);
                uint8_t kind = 0;
                if (isGhost) kind = 1;
                if (isDoor)  kind = 2;
                m_wallKind.push_back(kind);
                m_cellToWallPoint[idx] = wallIndex;
            }
        }
    }
}



void App::HandleInput(float dt) {
    // --- universal keys ---
    //maybe add specific keys later, but will need a revamping of the ascii level mapping...
    const bool rDown = glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS;
    if (rDown && !m_prevRDown) {
        if (m_state == AppState::Playing || m_state == AppState::Transition) {
            ResetLevelRuntime();
        }
    }
    m_prevRDown = rDown;

    // ESC behavior
    const bool escDown = glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    static bool prevEscDown = false;
    const bool escPressed = escDown && !prevEscDown;
    prevEscDown = escDown;

    if (escPressed) {
        if (m_state == AppState::Playing || m_state == AppState::Transition) {
            StartTransition(TransitionKind::ToMenu);
            return;
        }
        if (m_state == AppState::LevelSelect) {
            m_state = AppState::Title;
            return;
        }
        if (m_state == AppState::Title) {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            return;
        }
    }

    // --- menu input ---
    if (m_state == AppState::Title) {
        const bool up = glfwGetKey(m_window, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS;
        const bool dn = glfwGetKey(m_window, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS;
        static bool prevUp = false, prevDn = false;
        if (up && !prevUp) m_menuIndex = (m_menuIndex + 2) % 3;
        if (dn && !prevDn) m_menuIndex = (m_menuIndex + 1) % 3;
        prevUp = up;
        prevDn = dn;

        const bool enter = glfwGetKey(m_window, GLFW_KEY_ENTER) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS;
        static bool prevEnter = false;
        const bool enterPressed = enter && !prevEnter;
        prevEnter = enter;

        if (enterPressed) {
            if (m_menuIndex == 0) {
                // Play (current level)
                StartTransition(TransitionKind::StartLevel);
            }
            else if (m_menuIndex == 1) {
                // Enter level select. Start selection at current level for convenience.
                if (!m_levels.empty()) {
                    m_levelSelectIndex = std::max(0, std::min((int)m_levels.size() - 1, m_currentLevel));
                }
                else {
                    m_levelSelectIndex = 0;
                }
                m_state = AppState::LevelSelect;
            }
            else {
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }
        }
        return;
    }

    if (m_state == AppState::LevelSelect) {
        const bool up = glfwGetKey(m_window, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS;
        const bool dn = glfwGetKey(m_window, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS;
        static bool prevUp = false, prevDn = false;
        if (up && !prevUp) m_levelSelectIndex = std::max(0, m_levelSelectIndex - 1);
        if (dn && !prevDn) m_levelSelectIndex = std::min((int)m_levels.size() - 1, m_levelSelectIndex + 1);
        prevUp = up;
        prevDn = dn;

        const bool enter = glfwGetKey(m_window, GLFW_KEY_ENTER) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS;
        static bool prevEnter = false;
        const bool enterPressed = enter && !prevEnter;
        prevEnter = enter;

        if (enterPressed && !m_levels.empty()) {
            m_currentLevel = m_levelSelectIndex;
            StartTransition(TransitionKind::StartLevel);
        }
        return;
    }

    // --- gameplay input ---
    if (m_state != AppState::Playing && m_state != AppState::Transition) return;

    // Freeze input during death
    if (m_dying) return;

    // Energy regen (gameplay only)
    m_energy = std::min(1.0f, m_energy + m_energyRegenPerSec * dt);

    const bool spaceDown = glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (spaceDown && !m_prevSpaceDown && !m_won) {
        if (m_energy >= m_pingCost) {
            m_energy = std::max(0.0f, m_energy - m_pingCost);

            Wave w;
            w.origin = m_playerPos;
            w.t0 = (float)glfwGetTime();
            w.strength = 1.0f;

            PrecomputeWaveBFS(w);
            m_waves.push_back(std::move(w));

            // The cell you're in should become seen immediately on ping.
            EnsureRevealGrid();
            {
                const int cx = (int)std::floor(m_playerPos.x);
                const int cy = (int)std::floor(m_playerPos.y);
                if (m_map.InBounds(cx, cy)) {
                    const int idx = cy * m_map.Width() + cx;
                    m_revealUntil[idx] = std::max(m_revealUntil[idx], m_nowSeconds + m_revealHoldSeconds);
                }
            }

            const size_t maxWaves = 16;
            if (m_waves.size() > maxWaves) {
                m_waves.erase(m_waves.begin(), m_waves.begin() + (m_waves.size() - maxWaves));
            }
        }
    }
    m_prevSpaceDown = spaceDown;

    // Movement
    if (m_won) return;

    glm::vec2 move(0.0f);
    if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) move.y += 1.0f;
    if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) move.y -= 1.0f;
    if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) move.x += 1.0f;
    if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) move.x -= 1.0f;

    if (glm::length(move) > 0.0f) move = glm::normalize(move);
    // Hybrid grid: crates push in grid steps, player still moves freely.
    if (glm::length(move) > 0.0f) {
        TryPushCrateFromPlayer(move);
    }

    const glm::vec2 delta = move * (m_playerSpeed * dt);
    MoveWithCollisions(delta);

    UpdateTriggers();
}


void App::EnsureRevealGrid() {
    const int n = m_map.Width() * m_map.Height();
    if ((int)m_revealUntil.size() != n) {
        m_revealUntil.assign((size_t)n, -1e9f);
    }
}

bool App::IsCellRevealed(int x, int y, float now) const {
    if (!m_map.InBounds(x, y)) return false;
    const int idx = y * m_map.Width() + x;
    if (idx < 0 || idx >= (int)m_revealUntil.size()) return false;
    return now < m_revealUntil[idx];
}

GridMap::Layer App::LayerAtCell(int x, int y, float now) const {
    return IsCellRevealed(x, y, now) ? GridMap::Layer::Seen : GridMap::Layer::Unseen;
}

void App::ApplyWaveReveal(float now) {
    EnsureRevealGrid();

    for (const Wave& w : m_waves) {
        if (w.gridW != m_map.Width() || w.gridH != m_map.Height()) continue;
        if ((int)w.distField.size() != w.gridW * w.gridH) continue;

        const float age = now - w.t0;
        if (age <= 0.0f) continue;

        // Use the SAME radius curve as the ring so "visibility" matches the wavefront.
        const float r = WaveRadius(age);

        const int n = w.gridW * w.gridH;
        for (int i = 0; i < n; ++i) {
            const float d = w.distField[i];
            if (d > 1e8f) continue;
            if (d <= r) {
                const float until = now + m_revealHoldSeconds;
                if (until > m_revealUntil[i]) m_revealUntil[i] = until;
            }
        }
    }
}

void App::PrecomputeWaveBFS(Wave& w) {
    const int W = m_map.Width();
    const int H = m_map.Height();
    if (W <= 0 || H <= 0) return;

    w.gridW = W;
    w.gridH = H;
    w.distField.assign((size_t)W * (size_t)H, 1e9f);

    auto idx = [W](int x, int y) { return y * W + x; };

    const int sx = (int)std::floor(w.origin.x);
    const int sy = (int)std::floor(w.origin.y);

    w.wallIdx.clear();
    w.wallDist.clear();

    if (!m_map.IsEmpty(sx, sy)) return;

    std::deque<std::pair<int, int>> q;
    w.distField[idx(sx, sy)] = 0.0f;
    q.push_back({ sx, sy });

    const int dx[4] = { 1, -1, 0, 0 };
    const int dy[4] = { 0, 0, 1, -1 };

    while (!q.empty()) {
        auto [x, y] = q.front();
        q.pop_front();

        float d = w.distField[idx(x, y)];
        for (int k = 0; k < 4; ++k) {
            int nx = x + dx[k];
            int ny = y + dy[k];
            if (!m_map.IsEmpty(nx, ny)) continue;

            int ni = idx(nx, ny);
            float nd = d + 1.0f;
            if (w.distField[ni] <= nd) continue;

            w.distField[ni] = nd;
            q.push_back({ nx, ny });
        }
    }

    // Precompute wall hit distances (nearest adjacent empty-cell distance + offset)
    w.wallIdx.reserve(m_wallPoints.size());
    w.wallDist.reserve(m_wallPoints.size());

    for (size_t i = 0; i < m_wallPoints.size(); ++i) {
        int wx = (int)std::floor(m_wallPoints[i].x);
        int wy = (int)std::floor(m_wallPoints[i].y);
        const bool isWall = m_map.IsWall(wx, wy);
        const bool isDoor = (m_map.GetTile(wx, wy, GridMap::Layer::Seen) == GridMap::Tile::Door);
        if (!isWall && !isDoor) continue;

        float best = 1e9f;
        for (int k = 0; k < 4; ++k) { //325 - 440 : 450 - 605 : 615 - 730......
            int ex = wx + dx[k];
            int ey = wy + dy[k];
            if (!m_map.IsEmpty(ex, ey)) continue;
            best = std::min(best, w.distField[idx(ex, ey)]);
        }
        if (best > 1e8f) continue;

        float arrival = best + 0.55f;
        if (i > 65535) continue;

        w.wallIdx.push_back((uint16_t)i);
        w.wallDist.push_back(arrival);
    }

    // sort wall hits by distance
    {
        std::vector<size_t> order(w.wallDist.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;

        std::sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return w.wallDist[a] < w.wallDist[b]; });

        std::vector<uint16_t> newIdx;
        std::vector<float>    newDist;
        newIdx.reserve(order.size());
        newDist.reserve(order.size());

        for (size_t j = 0; j < order.size(); ++j) {
            newIdx.push_back(w.wallIdx[order[j]]);
            newDist.push_back(w.wallDist[order[j]]);
        }

        w.wallIdx.swap(newIdx);
        w.wallDist.swap(newDist);
    }
}


void App::UpdateWallDotsFromWaves(float now) {
    if (m_wallPoints.empty()) return;

    if (m_wallLastHitTime.size() != m_wallPoints.size())
        m_wallLastHitTime.assign(m_wallPoints.size(), -1e9f);
    if (m_wallLastHitStrength.size() != m_wallPoints.size())
        m_wallLastHitStrength.assign(m_wallPoints.size(), 0.0f);

    for (const auto& w : m_waves) {
        float age = now - w.t0;
        if (age <= 0.0f) continue;

        float r = WaveRadius(age);

        float sigma = m_wallHitSigma * (0.90f + 0.10f * std::sin(age * 2.2f));
        float pad = sigma + 0.35f;

        auto lo = std::lower_bound(w.wallDist.begin(), w.wallDist.end(), r - pad);
        auto hi = std::upper_bound(w.wallDist.begin(), w.wallDist.end(), r + pad);

        size_t i0 = (size_t)std::distance(w.wallDist.begin(), lo);
        size_t i1 = (size_t)std::distance(w.wallDist.begin(), hi);

        float phase = age * 2.6f;

        for (size_t j = i0; j < i1; ++j) {
            uint16_t wi = w.wallIdx[j];
            const glm::vec2 p = m_wallPoints[wi];

            float n = Hash12(p.x + w.origin.x * 0.13f, p.y + w.origin.y * 0.17f);
            float jitter = (n - 0.5f) * 0.10f;

            float d0 = std::fabs((w.wallDist[j] + jitter) - r);
            float lead = 1.0f - (d0 / sigma);
            lead = Clamp01(lead);
            lead *= lead;

            float echoOffset = 0.85f;
            float d1 = std::fabs((w.wallDist[j] + jitter) - (r - echoOffset));
            float echo = 1.0f - (d1 / (sigma * 1.35f));
            echo = Clamp01(echo);
            echo *= echo;
            echo *= 0.55f;

            float s = std::max(lead, echo);

            float shimmer = 0.90f + 0.10f * std::sin(phase + n * 6.2831f);
            s *= shimmer;

            float distFall = 1.0f / (1.0f + 0.08f * w.wallDist[j]);
            s *= w.strength * distFall;

            if (s <= 0.02f) continue;

            m_wallLastHitTime[wi] = now;
            m_wallLastHitStrength[wi] = std::max(m_wallLastHitStrength[wi], Clamp01(s));
        }
    }

    m_wallLitPoints.clear();
    m_wallLitPoints.reserve(m_wallPoints.size());

    for (size_t i = 0; i < m_wallPoints.size(); ++i) {
        float age = now - m_wallLastHitTime[i];
        if (age < 0.0f) continue;

        float fade = 0.0f;
        if (age <= m_wallHoldSeconds) {
            fade = 1.0f;
        }
        else {
            float t = (age - m_wallHoldSeconds) / m_wallFadeSeconds;
            if (t >= 1.0f) {
                m_wallLastHitStrength[i] = 0.0f;
                continue;
            }
            fade = 1.0f - t;
            fade = fade * fade * (3.0f - 2.0f * fade);
        }

        float a = fade * ClampF(m_wallLastHitStrength[i], 0.15f, 1.0f);
        if (a <= 0.01f) continue;

        const uint8_t kind = (i < m_wallKind.size()) ? m_wallKind[i] : 0;
        m_wallLitPoints.push_back({ m_wallPoints[i], a, kind });
    }
}


void App::BuildBFSWavefront(std::vector<glm::vec2>& outPts, const Wave& w, float now) const {
    outPts.clear();
    if (w.gridW <= 1 || w.gridH <= 1) return;
    if ((int)w.distField.size() != w.gridW * w.gridH) return;

    float age = now - w.t0;
    if (age <= 0.0f) return;

    // Two contours: leading crest + trailing echo
    float r0 = WaveRadius(age);
    float r1 = r0 - m_ringEchoDelay;

    // Oscillation travels along segments
    float oscPhase = age * 6.0f;
    float oscFreq = 7.0f; // along each segment

    auto sampleContour = [&](float iso, float thicknessAmp) {
        // marching squares over cell corners (x,y) to (x+1,y+1)
        for (int y = 0; y < w.gridH - 1; ++y) {
            for (int x = 0; x < w.gridW - 1; ++x) {
                auto at = [&](int ix, int iy) -> float {
                    // Treat walls/outside as unreachable so the contour doesn’t “cut through” solids
                    if (!m_map.InBounds(ix, iy)) return 1e9f;
                    if (!m_map.IsEmpty(ix, iy))  return 1e9f;
                    return w.distField[iy * w.gridW + ix];
                    };

                float d00 = at(x, y);
                float d10 = at(x + 1, y);
                float d01 = at(x, y + 1);
                float d11 = at(x + 1, y + 1);

                // ignore cells fully unreachable/walls
                if (d00 > 1e8f && d10 > 1e8f && d01 > 1e8f && d11 > 1e8f) continue;

                bool s00 = d00 < iso;
                bool s10 = d10 < iso;
                bool s01 = d01 < iso;
                bool s11 = d11 < iso;

                int mask = (s00 ? 1 : 0) | (s10 ? 2 : 0) | (s11 ? 4 : 0) | (s01 ? 8 : 0);
                if (mask == 0 || mask == 15) continue;

                // Find intersections on edges where sign changes:
                // Edges: 0: (x,y)-(x+1,y), 1:(x+1,y)-(x+1,y+1), 2:(x+1,y+1)-(x,y+1), 3:(x,y+1)-(x,y)
                glm::vec2 e[4];
                bool has[4] = { false,false,false,false };

                auto interp = [&](glm::vec2 a, glm::vec2 b, float va, float vb) -> glm::vec2 {
                    const float INF = 1e8f;

                    bool aBad = (va > INF);
                    bool bBad = (vb > INF);

                    // If one side is “unreachable/wall”, don’t lerp toward INF.
                    // The wave hits the boundary between cells -> midpoint.
                    if (aBad != bBad) {
                        return 0.5f * (a + b);
                    }

                    // If both are bad, caller shouldn’t be making an edge, but be safe.
                    if (aBad && bBad) {
                        return 0.5f * (a + b);
                    }

                    float t = Clamp01(IsoT(va, vb, iso));
                    return a + (b - a) * t;
                    };

                glm::vec2 p00(x + 0.5f, y + 0.5f);
                glm::vec2 p10(x + 1.5f, y + 0.5f);
                glm::vec2 p01(x + 0.5f, y + 1.5f);
                glm::vec2 p11(x + 1.5f, y + 1.5f);

                if (s00 != s10) { e[0] = interp(p00, p10, d00, d10); has[0] = true; }
                if (s10 != s11) { e[1] = interp(p10, p11, d10, d11); has[1] = true; }
                if (s11 != s01) { e[2] = interp(p11, p01, d11, d01); has[2] = true; }
                if (s01 != s00) { e[3] = interp(p01, p00, d01, d00); has[3] = true; }

                // Collect up to 4 intersection points in order around the cell
                glm::vec2 pts[4];
                int nPts = 0;
                for (int k = 0; k < 4; ++k) {
                    if (has[k]) pts[nPts++] = e[k];
                }
                if (nPts < 2) continue;

                // Typical cases produce 2 intersections → single segment
                // Ambiguous saddle cases produce 4 → two segments; connect (0-1) and (2-3)
                float localPhase = oscPhase + Hash12((float)x, (float)y) * 6.2831f;

                auto emitSeg = [&](glm::vec2 a, glm::vec2 b) {
                    AddSegmentSamples(outPts, a, b, m_ringSpacing, thicknessAmp, localPhase, oscFreq);
                    };

                if (nPts == 2) {
                    emitSeg(pts[0], pts[1]);
                }
                else if (nPts == 4) { // THis section sucked...so much devugging (*^*)
                    const float INF = 1e8f;
                    bool anyBad = (d00 > INF) || (d10 > INF) || (d01 > INF) || (d11 > INF);

                    if (!anyBad) {
                        // Real asymptotic decider when the field is valid
                        float center = 0.25f * (d00 + d10 + d01 + d11);
                        if (center < iso) {
                            emitSeg(pts[0], pts[3]);
                            emitSeg(pts[1], pts[2]);
                        }
                        else {
                            emitSeg(pts[0], pts[1]);
                            emitSeg(pts[2], pts[3]);
                        }
                    }
                    else {
                        // If walls are involved, pick a stable pairing based on the mask
                        // Ambiguous masks are 5 and 10 in marching squares
                        if (mask == 5) {           // s00 and s11 inside
                            emitSeg(pts[0], pts[1]);
                            emitSeg(pts[2], pts[3]);
                        }
                        else if (mask == 10) {   // s10 and s01 inside
                            emitSeg(pts[0], pts[3]);
                            emitSeg(pts[1], pts[2]);
                        }
                        else {
                            // Fallback
                            emitSeg(pts[0], pts[1]);
                            emitSeg(pts[2], pts[3]);
                        }
                    }
                }
            }
        }
        };

    // Leading crest (stronger oscillation)
    sampleContour(r0, m_ringOscAmp);

    // Trailing echo (weaker, only if it exists) - I like this effect, maybe fine tune later
    if (r1 > 0.5f) {
        sampleContour(r1, m_ringOscAmp * 0.55f);
    }

    // Subtle thickness band: add an inner offset contour (gives “wave thickness” without clutter)
    if (m_ringThickness > 0.15f) {
        float inner = r0 - (m_ringThickness * 0.35f);
        if (inner > 0.5f) sampleContour(inner, m_ringOscAmp * 0.35f);
    }
}

bool App::CollidesAt(const glm::vec2& p) const {
    const float r = m_playerRadius;

    const int minX = (int)std::floor(p.x - r);
    const int maxX = (int)std::floor(p.x + r);
    const int minY = (int)std::floor(p.y - r);
    const int maxY = (int)std::floor(p.y + r);

    auto ghostAlphaNow = [&](int x, int y) -> float {
        if (!m_map.InBounds(x, y)) return 0.0f;

        // Only relevant if this cell is a ghost wall tile in the SEEN layer.
        if (m_map.GetTile(x, y, GridMap::Layer::Seen) != GridMap::Tile::GhostWall) return 0.0f;

        const int cellIdx = y * m_map.Width() + x;
        if (cellIdx < 0 || cellIdx >= (int)m_cellToWallPoint.size()) return 0.0f;

        const int wi = m_cellToWallPoint[cellIdx];
        if (wi < 0 || wi >= (int)m_wallLastHitTime.size()) return 0.0f;

        const float age = m_nowSeconds - m_wallLastHitTime[wi];
        if (age < 0.0f) return 0.0f;

        float fade = 0.0f;
        if (age <= m_wallHoldSeconds) {
            fade = 1.0f;
        }
        else {
            float t = (age - m_wallHoldSeconds) / m_wallFadeSeconds;
            if (t >= 1.0f) return 0.0f;
            fade = 1.0f - t;
            // smoothstep-ish at least i hope tis smooth enough.
            fade = fade * fade * (3.0f - 2.0f * fade);
        }

        float s = m_wallLastHitStrength[wi];
        if (s < 0.15f) s = 0.15f;
        if (s > 1.0f)  s = 1.0f;
        float a = fade * s;
        return (a > 0.0f) ? a : 0.0f;
        };

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!m_map.InBounds(x, y)) return true;

            // Crates always block.
            if (IsCrateAt(x, y)) {
                // continue to collision test
            }
            else {
                // Doors: solid in both layers unless a trigger is active.
                if (m_map.GetTile(x, y, GridMap::Layer::Unseen) == GridMap::Tile::Door) {
                    if (m_doorsOpenByPlate || m_doorsOpenByButton) {
                        continue; // opened
                    }
                    // otherwise blocks
                }
                else if (m_map.GetTile(x, y, GridMap::Layer::Seen) == GridMap::Tile::GhostWall) {
                    // Ghost walls block while their reveal is still visible; once it fades, they become passable.
                    const float a = ghostAlphaNow(x, y);
                    if (a <= 0.01f) {
                        continue;
                    }
                    // otherwise blocks
                }
                else {
                    // Normal per-cell realm switching:
                    GridMap::Layer layer = LayerAtCell(x, y, m_nowSeconds);
                    if (!m_map.IsBlocked(x, y, layer)) continue;
                }
            }

            float cellMinX = (float)x;
            float cellMinY = (float)y;
            float cellMaxX = cellMinX + 1.0f;
            float cellMaxY = cellMinY + 1.0f;

            float cx = ClampF(p.x, cellMinX, cellMaxX);
            float cy = ClampF(p.y, cellMinY, cellMaxY);

            float dx = p.x - cx;
            float dy = p.y - cy;

            if (dx * dx + dy * dy < r * r) return true;
        }
    }
    return false;
}


void App::MoveWithCollisions(const glm::vec2& delta) {
    if (delta.x != 0.0f) {
        glm::vec2 p = m_playerPos;
        p.x += delta.x;
        if (!CollidesAt(p)) m_playerPos.x = p.x;
    }

    if (delta.y != 0.0f) {
        glm::vec2 p = m_playerPos;
        p.y += delta.y;
        if (!CollidesAt(p)) m_playerPos.y = p.y;
    }

    CheckHazards();
    CheckWin();
}


void App::CheckHazards() {
    const float now = m_nowSeconds;
    if (now - m_lastHazardHitTime < m_hazardInvulnSeconds) return;

    const int cx = (int)std::floor(m_playerPos.x);
    const int cy = (int)std::floor(m_playerPos.y);
    if (!m_map.InBounds(cx, cy)) return;

    if (!IsCellRevealed(cx, cy, now) && m_map.IsHazard(cx, cy, GridMap::Layer::Unseen)) {
        m_lastHazardHitTime = now;
        EnterDeath();
    }
}

void App::CheckWin() {
    if (m_won) return;

    const int cx = (int)std::floor(m_playerPos.x);
    const int cy = (int)std::floor(m_playerPos.y);
    if (!m_map.InBounds(cx, cy)) return;

    if (m_map.GetTile(cx, cy, GridMap::Layer::Unseen) == GridMap::Tile::Exit) {
        m_won = true;
        m_winTime = m_nowSeconds;
        StartTransition(TransitionKind::NextLevel);
    }
}

// -----------------------------
// Crates / triggers (hybrid grid) - full freeform is too hard a d buggy
// -----------------------------

int App::FindCrateIndex(int x, int y) const {
    for (int i = 0; i < (int)m_crates.size(); ++i) {
        if (m_crates[i].x == x && m_crates[i].y == y) return i;
    }
    return -1;
}

bool App::IsCrateAt(int x, int y) const {
    return FindCrateIndex(x, y) >= 0;
}

bool App::TryPushCrateFromPlayer(const glm::vec2& moveDir) {
    // Determine a dominant axis push direction.
    glm::ivec2 dir(0, 0);
    if (std::fabs(moveDir.x) > std::fabs(moveDir.y)) dir.x = (moveDir.x > 0.0f) ? 1 : -1;
    else if (std::fabs(moveDir.y) > 0.0f) dir.y = (moveDir.y > 0.0f) ? 1 : -1;
    else return false;

    const int px = (int)std::floor(m_playerPos.x);
    const int py = (int)std::floor(m_playerPos.y);
    if (!m_map.InBounds(px, py)) return false;

    // Only allow a push when the player is reasonably aligned to the grid.(Might be too harsh right now?)
    const float fx = m_playerPos.x - (float)px;
    const float fy = m_playerPos.y - (float)py;
    const float align = 0.23f;
    if (dir.x != 0 && std::fabs(fy - 0.5f) > align) return false;
    if (dir.y != 0 && std::fabs(fx - 0.5f) > align) return false;

    const int cx = px + dir.x;
    const int cy = py + dir.y;
    const int ci = FindCrateIndex(cx, cy);
    if (ci < 0) return false;

    const int nx = cx + dir.x;
    const int ny = cy + dir.y;
    if (!m_map.InBounds(nx, ny)) return false;
    if (IsCrateAt(nx, ny)) return false;

    // Crates treat doors as solid unless doors are forced open by plate/button.
    const bool doorsForcedOpen = (m_doorsOpenByPlate || m_doorsOpenByButton);
    if (m_map.GetTile(nx, ny, GridMap::Layer::Unseen) == GridMap::Tile::Door && !doorsForcedOpen) {
        return false;
    }

    // Use realm blocking for walls/ghost walls; plates/buttons are non-blocking.
    GridMap::Layer layer = LayerAtCell(nx, ny, m_nowSeconds);
    if (m_map.IsBlocked(nx, ny, layer)) return false;

    m_crates[ci].x = nx;
    m_crates[ci].y = ny;
    return true;
}

void App::UpdateTriggers() {
    // Pressure plates: open doors while held (even if unseen, so you can leave a crate and walk away).
    bool anyPlateHeld = false;

    auto cellHeld = [&](int x, int y) -> bool {
        if ((int)std::floor(m_playerPos.x) == x && (int)std::floor(m_playerPos.y) == y) return true;
        return IsCrateAt(x, y);
        };

    for (int y = 0; y < m_map.Height(); ++y) {
        for (int x = 0; x < m_map.Width(); ++x) {
            if (m_map.IsPlateAnyLayer(x, y) && cellHeld(x, y)) {
                anyPlateHeld = true;
            }
        }
    }
    m_doorsOpenByPlate = anyPlateHeld;

    // Buttons: toggle door state on entry, but only if revealed.
    const int px = (int)std::floor(m_playerPos.x);
    const int py = (int)std::floor(m_playerPos.y);
    if (m_map.InBounds(px, py)) {
        if (IsCellRevealed(px, py, m_nowSeconds) &&
            m_map.GetTile(px, py, GridMap::Layer::Seen) == GridMap::Tile::Button) {
            if (!(px == m_prevButtonCellX && py == m_prevButtonCellY)) {
                m_doorsOpenByButton = !m_doorsOpenByButton;
            }
            m_prevButtonCellX = px;
            m_prevButtonCellY = py;
        }
        else {
            m_prevButtonCellX = -9999;
            m_prevButtonCellY = -9999;
        }
    }
}

void App::DrawCrates(int fbW, int fbH) {
    (void)fbW; (void)fbH;
    const glm::vec2 worldMin(0.0f, 0.0f);
    const glm::vec2 worldMax((float)m_map.Width(), (float)m_map.Height());

    std::vector<glm::vec2> pts;
    pts.reserve(m_crates.size() * 32);
    for (const auto& c : m_crates) {
        if (!IsCellRevealed(c.x, c.y, m_nowSeconds)) continue;
        const float x0 = c.x + 0.22f;
        const float y0 = c.y + 0.22f;
        const float x1 = c.x + 0.78f;
        const float y1 = c.y + 0.78f;
        for (float y = y0; y <= y1; y += 0.14f) {
            for (float x = x0; x <= x1; x += 0.14f) {
                pts.emplace_back(x, y);
            }
        }
    }
    if (!pts.empty()) {
        m_renderer.DrawPointsAdditive(pts, worldMin, worldMax, glm::vec4(1.0f, 0.65f, 0.15f, 0.55f), 12.0f, m_nowSeconds);
    }
}

void App::DrawTriggers(int fbW, int fbH) {
    (void)fbW; (void)fbH;
    const glm::vec2 worldMin(0.0f, 0.0f);
    const glm::vec2 worldMax((float)m_map.Width(), (float)m_map.Height());
    std::vector<glm::vec2> platePts;
    std::vector<glm::vec2> buttonPts;

    for (int y = 0; y < m_map.Height(); ++y) {
        for (int x = 0; x < m_map.Width(); ++x) {
            if (!IsCellRevealed(x, y, m_nowSeconds)) continue;
            // When revealed, draw based on the seen layer.
            GridMap::Tile t = m_map.GetTile(x, y, GridMap::Layer::Seen);
            if (t == GridMap::Tile::Plate) platePts.emplace_back(x + 0.5f, y + 0.5f);
            else if (t == GridMap::Tile::Button) buttonPts.emplace_back(x + 0.5f, y + 0.5f);
        }
    }

    if (!platePts.empty()) {
        glm::vec4 col = m_doorsOpenByPlate ? glm::vec4(0.25f, 1.0f, 0.95f, 0.90f) : glm::vec4(0.10f, 0.55f, 0.65f, 0.70f);
        m_renderer.DrawPointsAdditive(platePts, worldMin, worldMax, col, 18.0f, m_nowSeconds);
    }
    if (!buttonPts.empty()) {
        glm::vec4 col = m_doorsOpenByButton ? glm::vec4(1.0f, 0.95f, 0.20f, 0.90f) : glm::vec4(0.75f, 0.65f, 0.20f, 0.70f);
        m_renderer.DrawPointsAdditive(buttonPts, worldMin, worldMax, col, 18.0f, m_nowSeconds);
    }
}

// -----------------------------
// Menu / Transitions / Levels
// -----------------------------

static float EaseInOut(float t) {
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

static void AddRectPts(std::vector<glm::vec2>& out, float x, float y, float w, float h, float step) {
    const int nx = std::max(1, (int)std::floor(w / step));
    const int ny = std::max(1, (int)std::floor(h / step));
    for (int iy = 0; iy <= ny; ++iy) {
        for (int ix = 0; ix <= nx; ++ix) {
            out.emplace_back(x + ix * (w / (float)nx), y + iy * (h / (float)ny));
        }
    }
}

void App::StartTransition(TransitionKind kind) {
    m_transitionKind = kind;
    m_transitionT = 0.0f;
    m_transitionSwapped = false;
    m_state = AppState::Transition;
}

void App::UpdateTransition(float dt) {
    m_transitionT += dt;

    // Perform the swap at mid-transition (when it's darkest)
    if (!m_transitionSwapped && m_transitionT >= (m_transitionDuration * 0.5f)) {
        m_transitionSwapped = true;
        switch (m_transitionKind) {
        case TransitionKind::StartLevel:
            LoadLevelIndex(m_currentLevel);
            break;
        case TransitionKind::NextLevel:
            NextLevel();
            break;
        case TransitionKind::ToMenu:
            ResetLevelRuntime();
            break;
        default:
            break;
        }
    }

    if (m_transitionT < m_transitionDuration) return;

    // End transition
    const TransitionKind kind = m_transitionKind;
    m_transitionKind = TransitionKind::None;
    m_transitionT = 0.0f;
    m_transitionSwapped = false;

    if (kind == TransitionKind::ToMenu) m_state = AppState::Title;
    else m_state = AppState::Playing;
}

void App::DrawTransitionOverlay(int fbW, int fbH) {
    float t = (m_transitionDuration <= 1e-6f) ? 1.0f : (m_transitionT / m_transitionDuration);
    t = Clamp01(t);
    // Ease + make mid-transition darkest
    float a = 0.0f;
    if (t < 0.5f) a = EaseInOut(t * 2.0f);
    else a = EaseInOut((1.0f - t) * 2.0f);
    a = Clamp01(a);
    a = a * 0.92f;

    std::vector<glm::vec2> pts;
    pts.reserve((size_t)((fbW / 10) * (fbH / 10)));
    AddRectPts(pts, 0.0f, 0.0f, (float)fbW, (float)fbH, 10.0f);
    m_renderer.DrawPoints(pts, { 0,0 }, { (float)fbW, (float)fbH }, glm::vec4(0, 0, 0, a), 12.0f, m_nowSeconds);
}

// -----------------------------
// HUD
// -----------------------------

void App::DrawHUD(int fbW, int fbH) {
    // Fixed segment bar (10). Palette separate from walls.(Stylize up later, not a fan of current colour)
    const int segs = 10;
    float x = 20.0f;
    float y = 20.0f;
    float w = 240.0f;
    float h = 18.0f;
    float gap = 3.0f;

    // Panel background
    std::vector<glm::vec2> bg;
    AddRectPts(bg, x - 10.0f, y - 10.0f, w + 20.0f, h + 20.0f, 6.0f);
    m_renderer.DrawPoints(bg, { 0,0 }, { (float)fbW, (float)fbH }, glm::vec4(0.05f, 0.02f, 0.08f, 0.65f), 6.0f, m_nowSeconds);

    // Glow underlay
    std::vector<glm::vec2> glow;
    AddRectPts(glow, x - 8.0f, y - 8.0f, w + 16.0f, h + 16.0f, 10.0f);
    m_renderer.DrawPointsAdditive(glow, { 0,0 }, { (float)fbW, (float)fbH }, glm::vec4(1.0f, 0.25f, 0.85f, 0.08f), 10.0f, m_nowSeconds);

    float segW = (w - gap * (segs - 1)) / (float)segs;
    float energy = Clamp01(m_energy);
    float filled = energy * segs;
    int full = (int)std::floor(filled);
    float partial = filled - full;

    for (int i = 0; i < segs; ++i) {
        float sx = x + i * (segW + gap);
        float fillFrac = 0.0f;
        if (i < full) fillFrac = 1.0f;
        else if (i == full) fillFrac = partial;

        // segment frame
        std::vector<glm::vec2> frame;
        AddRectPts(frame, sx, y, segW, h, 8.0f);
        m_renderer.DrawPoints(frame, { 0,0 }, { (float)fbW, (float)fbH }, glm::vec4(0.55f, 0.90f, 1.0f, 0.14f), 4.5f, m_nowSeconds);

        if (fillFrac > 0.0f) {
            std::vector<glm::vec2> fill;
            AddRectPts(fill, sx + 1.0f, y + 1.0f, (segW - 2.0f) * fillFrac, h - 2.0f, 6.0f);
            m_renderer.DrawPointsAdditive(fill, { 0,0 }, { (float)fbW, (float)fbH }, glm::vec4(1.0f, 0.25f, 0.85f, 0.45f), 6.2f, m_nowSeconds);
        }
    }
}



