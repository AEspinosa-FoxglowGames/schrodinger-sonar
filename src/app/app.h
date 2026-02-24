#pragma once

#include <vector>
#include <cstdint>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "render/Renderer.h"
#include "sim/GridMap.h"

struct GLFWwindow;

class App {
public:
    bool Init();
    void Run();
    void Shutdown();

private:
    // -----------------------------
    // Menu / Title / Level Select
    // -----------------------------
    enum class AppState { Title, LevelSelect, Playing, Transition };
    enum class TransitionKind { None, StartLevel, NextLevel, ToMenu };

    void DrawTitleScreen(int fbW, int fbH);
    void DrawLevelSelect(int fbW, int fbH);
    void DrawHUD(int fbW, int fbH);
    void DrawText(int fbW, int fbH, glm::vec2 px, float scale, const char* text, glm::vec4 color);

    void StartTransition(TransitionKind kind);
    void UpdateTransition(float dt);
    void DrawTransitionOverlay(int fbW, int fbH);

    // -----------------------------
    // Levels
    // -----------------------------
    bool LoadLevelsFile(const char* path);
    bool LoadLevelIndex(int idx);
    void ResetLevelRuntime();
    void NextLevel();

    // -----------------------------
    // Death (instant respawn)
    // -----------------------------
    void EnterDeath();
    void UpdateDeath(float dt);
    void DrawDeathFX(int fbW, int fbH);
    void BuildWallPoints();
    void HandleInput(float dt);

    bool CollidesAt(const glm::vec2& p) const;
    void MoveWithCollisions(const glm::vec2& delta);
    void CheckHazards();
    void CheckWin();

    // -----------------------------
    // Crates / triggers (hybrid grid)
    // -----------------------------
    struct Crate { int x = 0; int y = 0; };

    void ResetCratesFromLevelRows(std::vector<std::string>& rows); // extracts 'C'
    bool IsCrateAt(int x, int y) const;
    int  FindCrateIndex(int x, int y) const;
    bool TryPushCrateFromPlayer(const glm::vec2& moveDir);
    void UpdateTriggers();
    void DrawCrates(int fbW, int fbH);
    void DrawTriggers(int fbW, int fbH);

    bool m_won = false;
    float m_winTime = -1e9f;

    // --- Visibility (seen vs unseen) ---
    void EnsureRevealGrid();
    void ApplyWaveReveal(float nowSeconds);
    bool IsCellRevealed(int x, int y, float nowSeconds) const;
    GridMap::Layer LayerAtCell(int x, int y, float nowSeconds) const;

    // --- BFS sonar wave ---
    struct Wave {
        glm::vec2 origin{ 0.0f, 0.0f };
        float     t0 = 0.0f;
        float     strength = 1.0f;

        int gridW = 0;
        int gridH = 0;

        std::vector<float> distField; // size = gridW * gridH

        std::vector<uint16_t> wallIdx;   // index into m_wallPoints
        std::vector<float>    wallDist;  // BFS distance to "hit" that wall point
    };

    void PrecomputeWaveBFS(Wave& w);
    void UpdateWallDotsFromWaves(float nowSeconds);

    void BuildBFSWavefront(std::vector<glm::vec2>& outPts, const Wave& w, float nowSeconds) const;

private:
    struct LitDot {
        glm::vec2 pos;
        float     a;
        // 0 = wall, 1 = ghost wall (G), 2 = door (D)
        uint8_t   kind;
    };

    GLFWwindow* m_window = nullptr;

    Renderer m_renderer;
    GridMap  m_map;

    // Crates occupy grid cells and move in grid steps only.
    std::vector<Crate> m_crates;
    std::vector<Crate> m_cratesStart;

    // Door control
    bool m_doorsOpenByPlate = false;
    bool m_doorsOpenByButton = false;

    // Button edge-trigger
    int m_prevButtonCellX = -9999;
    int m_prevButtonCellY = -9999;

    std::vector<glm::vec2> m_wallPoints;
    // 0 = wall, 1 = ghost wall (G), 2 = door (D)
    std::vector<uint8_t>   m_wallKind;

    // Maps each cell -> index into m_wallPoints (or -1 if none). Used to sync door visuals & collision.
    std::vector<int>       m_cellToWallPoint;

    std::vector<float>  m_wallLastHitTime;
    std::vector<float>  m_wallLastHitStrength;
    std::vector<LitDot> m_wallLitPoints;

    float m_wallHoldSeconds = 0.65f;
    float m_wallFadeSeconds = 1.35f;

    float m_wallHitSigma = 0.40f;

    float m_ringThickness = 0.55f;
    float m_ringSpacing = 0.09f;
    float m_ringOscAmp = 0.01f;
    float m_ringEchoDelay = 0.90f;

    glm::vec2 m_playerPos = { 2.5f, 2.5f };
    glm::vec2 m_playerSpawn = { 2.5f, 2.5f };
    float m_playerRadius = 0.20f;
    float m_playerSpeed = 4.0f;

    float m_hazardInvulnSeconds = 0.35f;
    float m_lastHazardHitTime = -1e9f;

    std::vector<float> m_revealUntil;
    float m_revealHoldSeconds = 1.25f;

    std::vector<Wave> m_waves;
    bool m_prevSpaceDown = false;

    // Restart edge-trigger
    bool m_prevRDown = false;

    // -----------------------------
    // Menu / level flow state
    // -----------------------------
    AppState m_state = AppState::Title;
    TransitionKind m_transitionKind = TransitionKind::None;
    float m_transitionT = 0.0f;
    float m_transitionDuration = 0.55f;
    bool  m_transitionSwapped = false;

    // Menu navigation
    int m_menuIndex = 0;          // 0 Play, 1 Level Select, 2 Quit
    int m_levelSelectIndex = 0;

    // Levels loaded from file (each level is a list of ascii rows)
    std::vector<std::vector<std::string>> m_levels;
    int m_currentLevel = 0;

    // -----------------------------
    // Energy (0..1)
    // -----------------------------
    float m_energy = 1.0f;
    float m_energyRegenPerSec = 0.25f;
    float m_pingCost = 0.32f;

    // -----------------------------
    // Death state
    // -----------------------------
    bool  m_dying = false;
    float m_deathT = 0.0f;
    float m_deathDuration = 0.55f;
    glm::vec2 m_deathPos = { 0,0 };

    float m_waveMaxAge = 4.5f;

    float m_nowSeconds = 0.0f;
};



