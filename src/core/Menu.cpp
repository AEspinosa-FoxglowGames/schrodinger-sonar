#include "app/app.h"

#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>

// Local helpers (were originally in app.cpp) - app cpp was 2k lines long!!!
static float Hash12(float x, float y) {
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return h - std::floor(h);
}

// Menu + tiny bitmap font live here (split from app.cpp).
// Note: these methods are intentionally non-const because they issue draw calls.

// -----------------------------
// Text rendering (tiny bitmap)
// -----------------------------

// 5x7 font, stored as 7 rows of 5 bits (MSB-left)
struct Glyph57 { char c; uint8_t r[7]; };

static const Glyph57 kFont[] = {
    {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'D',{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'I',{0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}},
    {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
    {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3',{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}},
    {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6',{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
    {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9',{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {':',{0x00,0x04,0x00,0x00,0x00,0x04,0x00}},
};

static const Glyph57* FindGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (const auto& g : kFont) if (g.c == c) return &g;
    return nullptr;
}

void App::DrawText(int fbW, int fbH, glm::vec2 px, float scale, const char* text, glm::vec4 color) {
    std::vector<glm::vec2> pts;
    pts.reserve(2048);

    float x = px.x;
    // Treat px as TOP-left in pixels; convert to GL-style bottom-left space.
    float y = (float)fbH - px.y;
    const float cell = 2.0f * scale;
    const float adv = 6.0f * cell;

    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { y -= 9.0f * cell; x = px.x; continue; }
        if (*p == ' ') { x += adv; continue; }
        const Glyph57* g = FindGlyph(*p);
        if (!g) { x += adv; continue; }

        for (int row = 0; row < 7; ++row) {
            uint8_t bits = g->r[row];
            for (int col = 0; col < 5; ++col) {
                if (bits & (1u << (4 - col))) {
                    pts.emplace_back(x + col * cell, y - row * cell);
                }
            }
        }
        x += adv;
    }

    m_renderer.DrawPoints(pts, { 0,0 }, { (float)fbW, (float)fbH }, color, 6.0f * scale, m_nowSeconds);
}

// -----------------------------
// Menu screens
// -----------------------------

struct UiXform {
    float s;
    float ox;
    float oy;
    float vw;
    float vh;
};

// Render UI in a stable virtual resolution so fullscreen / ultrawide doesn't stretch layout.
static UiXform GetUiXform(int fbW, int fbH) {
    // Smaller virtual canvas => UI remains legible in portrait and on small windows.
    const float vw = 320.0f;
    const float vh = 180.0f;
    const float s = std::min((float)fbW / vw, (float)fbH / vh);
    const float ox = ((float)fbW - vw * s) * 0.5f;
    const float oy = ((float)fbH - vh * s) * 0.5f;
    return { s, ox, oy, vw, vh };
}

static glm::vec2 UiToPx(const UiXform& u, float x, float y) {
    // x,y are in virtual pixels with origin at top-left.
    return glm::vec2(u.ox + x * u.s, u.oy + y * u.s);
}

static float TextWidthPx(const char* text, float scale) {
    // Matches DrawText advance rules.
    const float cell = 2.0f * scale;
    const float adv = 6.0f * cell;
    float w = 0.0f;
    float lineW = 0.0f;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            w = std::max(w, lineW);
            lineW = 0.0f;
            continue;
        }
        lineW += adv;
    }
    w = std::max(w, lineW);
    return w;
}

void App::DrawTitleScreen(int fbW, int fbH) {
    const UiXform ui = GetUiXform(fbW, fbH);

    // Minimal sci-fi palette distinct from blue walls
    const glm::vec4 titleCol(1.0f, 0.25f, 0.85f, 0.95f);
    const glm::vec4 textCol(0.95f, 0.78f, 0.30f, 0.90f);
    const glm::vec4 dimCol(0.75f, 0.65f, 0.35f, 0.60f);

    // Subtle background speckle
    std::vector<glm::vec2> speck;
    speck.reserve(1400);
    for (int i = 0; i < 1400; ++i) {
        float fx = Hash12((float)i, 13.1f);
        float fy = Hash12((float)i, 71.7f);
        speck.emplace_back(fx * fbW, fy * fbH);
    }
    m_renderer.DrawPointsAdditive(speck, { 0,0 }, { (float)fbW, (float)fbH }, glm::vec4(0.25f, 0.10f, 0.35f, 0.10f), 2.0f, m_nowSeconds);

    {
        const float titleScale = 3.0f;
        const float subtitleScale = 1.0f;
        const float tx = (ui.vw - TextWidthPx("SONAR PUZZLE", 0.5)) * 0.5f;
        //const float sx = (ui.vw - TextWidthPx("PUZZLE", 0)) * 0.5f;
        DrawText(fbW, fbH, UiToPx(ui, tx, 18.0f), titleScale, "SONAR PUZZLE", titleCol);
        //DrawText(fbW, fbH, UiToPx(ui, sx, 46.0f), subtitleScale, "PUZZLE", glm::vec4(0.55f, 0.90f, 1.0f, 0.55f));
    }

    {
        const char* items[3] = { "PLAY", "LEVEL SELECT", "QUIT" };
        const float itemScale = 1.65f;
        const float baseY = 82.0f;
        const float stepY = 22.0f;
        for (int i = 0; i < 3; ++i) {
            const float ix = (ui.vw - TextWidthPx(items[i], 0)) * 0.5f;
            const float iy = baseY + stepY * (float)i;
            glm::vec4 c = (i == m_menuIndex) ? textCol : dimCol;
            DrawText(fbW, fbH, UiToPx(ui, ix, iy), itemScale, items[i], c);
        }

        const char* hint = "ENTER/SPACE - SELECT   ESC - QUIT";
        const float hintScale = 0.9f;
        const float hx = (ui.vw - TextWidthPx(hint, .5)) * 0.5f;
        DrawText(fbW, fbH, UiToPx(ui, hx, 162.0f), hintScale, hint, glm::vec4(0.55f, 0.90f, 1.0f, 0.35f));
    }
}

void App::DrawLevelSelect(int fbW, int fbH) {
    const UiXform ui = GetUiXform(fbW, fbH);

    const glm::vec4 titleCol(1.0f, 0.25f, 0.85f, 0.90f);
    const glm::vec4 textCol(0.95f, 0.78f, 0.30f, 0.90f);
    const glm::vec4 dimCol(0.75f, 0.65f, 0.35f, 0.55f);

    {
        const float tScale = 1.8f;
        const float tx = (ui.vw - TextWidthPx("LEVEL SELECT", tScale)) * 0.5f;
        DrawText(fbW, fbH, UiToPx(ui, tx, 18.0f), tScale, "LEVEL SELECT", titleCol);
    }

    int n = (int)m_levels.size();
    if (n <= 0) {
        DrawText(fbW, fbH, UiToPx(ui, 52.0f, 80.0f), 1.1f, "NO LEVELS LOADED", dimCol);
        DrawText(fbW, fbH, UiToPx(ui, 52.0f, 162.0f), 0.9f, "ESC - BACK", glm::vec4(0.55f, 0.90f, 1.0f, 0.35f));
        return;
    }

    int start = std::max(0, m_levelSelectIndex - 4);
    int end = std::min(n, start + 10);

    float y0 = 54.0f;
    for (int i = start; i < end; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "LEVEL %d", i + 1);
        const float itemScale = 1.05f;
        const float ix = 88.0f;
        glm::vec2 p = UiToPx(ui, ix, y0 + (float)(i - start) * 16.0f);
        glm::vec4 c = (i == m_levelSelectIndex) ? textCol : dimCol;
        DrawText(fbW, fbH, p, itemScale, buf, c);

        if (i == m_levelSelectIndex) {
            // Small selector chevrons.
            DrawText(fbW, fbH, UiToPx(ui, ix - 28.0f, y0 + (float)(i - start) * 16.0f), itemScale, ">", glm::vec4(0.55f, 0.90f, 1.0f, 0.55f));
            DrawText(fbW, fbH, UiToPx(ui, ix + 150.0f, y0 + (float)(i - start) * 16.0f), itemScale, "<", glm::vec4(0.55f, 0.90f, 1.0f, 0.25f));
        }
    }

    {
        const char* hint = "ENTER/SPACE - PLAY   ESC - BACK";
        const float hintScale = 0.9f;
        const float hx = (ui.vw - TextWidthPx(hint, 0)) * 0.5f;
        DrawText(fbW, fbH, UiToPx(ui, hx, 162.0f), hintScale, hint, glm::vec4(0.55f, 0.90f, 1.0f, 0.35f));
    }
}




