#pragma once

#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "render/Shader.h"

class Renderer {
public:
    struct PointA {
        glm::vec2 pos;
        float     a;
    };

public:
    bool Init();
    void Shutdown();

    void BeginFrame(int fbWidth, int fbHeight);

    void DrawPoints(const std::vector<glm::vec2>& points,
        glm::vec2 worldMin, glm::vec2 worldMax,
        glm::vec4 color,
        float pointSize,
        float timeSeconds,
        int mode = 0) const;

    void DrawPointsAdditive(const std::vector<glm::vec2>& points,
        glm::vec2 worldMin, glm::vec2 worldMax,
        glm::vec4 color,
        float pointSize,
        float timeSeconds,
        int mode = 0) const;

    void DrawPointsAlpha(const std::vector<PointA>& points,
        glm::vec2 worldMin, glm::vec2 worldMax,
        glm::vec4 color,
        float pointSize,
        float timeSeconds,
        bool additive,
        int mode = 2) const;

private:
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;

    // VAO/VBO for PointA (vec2 + float)
    unsigned int m_vaoA = 0;
    unsigned int m_vboA = 0;

    Shader m_pointsShader;
};



