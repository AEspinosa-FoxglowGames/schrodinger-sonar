#include "Renderer.h"
#include <glad/glad.h>
#include <iostream>
#include <algorithm>

static void GLCheck(const char* where) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[GL ERROR] " << where << " : 0x" << std::hex << err << std::dec << "\n";
    }
}

bool Renderer::Init() {
    // --- VAO/VBO for vec2 points ---
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    GLCheck("Init after VAO/VBO");

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);

    glBindVertexArray(0);

    // --- VAO/VBO for (vec2 pos + float alpha) ---
    glGenVertexArrays(1, &m_vaoA);
    glGenBuffers(1, &m_vboA);
    GLCheck("Init after VAO/VBO A");

    glBindVertexArray(m_vaoA);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboA);

    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    // location 0: vec2 position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PointA), (void*)0);

    // location 1: float alpha
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(PointA), (void*)(sizeof(float) * 2));

    glBindVertexArray(0);

    // Shader
    if (!m_pointsShader.LoadFromFiles("assets/Shaders/points.vert", "assets/Shaders/points.frag")) {
        std::cerr << "Failed to load points shader\n";
        return false;
    }

    glEnable(GL_PROGRAM_POINT_SIZE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return true;
}

void Renderer::Shutdown() {
    m_pointsShader.Destroy();

    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vbo = 0;
    m_vao = 0;

    if (m_vboA) glDeleteBuffers(1, &m_vboA);
    if (m_vaoA) glDeleteVertexArrays(1, &m_vaoA);
    m_vboA = 0;
    m_vaoA = 0;
}

void Renderer::BeginFrame(int fbWidth, int fbHeight) {
    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::DrawPoints(const std::vector<glm::vec2>& points,
    glm::vec2 worldMin, glm::vec2 worldMax,
    glm::vec4 color,
    float pointSize,
    float timeSeconds,
    int mode) const {

    if (points.empty()) return;

    m_pointsShader.Bind();
    m_pointsShader.SetInt("uMode", mode);
    m_pointsShader.SetVec2("uWorldMin", worldMin.x, worldMin.y);
    m_pointsShader.SetVec2("uWorldMax", worldMax.x, worldMax.y);
    m_pointsShader.SetVec4("uColor", color.x, color.y, color.z, color.w);
    m_pointsShader.SetFloat("uPointSize", pointSize);
    m_pointsShader.SetFloat("uTime", timeSeconds);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(glm::vec2), points.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_POINTS, 0, (GLsizei)points.size());

    glBindVertexArray(0);
}

void Renderer::DrawPointsAdditive(const std::vector<glm::vec2>& points,
    glm::vec2 worldMin, glm::vec2 worldMax,
    glm::vec4 color,
    float pointSize,
    float timeSeconds,
    int mode) const {

    if (points.empty()) return;

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    m_pointsShader.Bind();
    m_pointsShader.SetInt("uMode", mode);
    m_pointsShader.SetVec2("uWorldMin", worldMin.x, worldMin.y);
    m_pointsShader.SetVec2("uWorldMax", worldMax.x, worldMax.y);
    m_pointsShader.SetVec4("uColor", color.x, color.y, color.z, color.w);
    m_pointsShader.SetFloat("uPointSize", pointSize);
    m_pointsShader.SetFloat("uTime", timeSeconds);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(glm::vec2), points.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_POINTS, 0, (GLsizei)points.size());

    glBindVertexArray(0);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::DrawPointsAlpha(const std::vector<PointA>& points,
    glm::vec2 worldMin, glm::vec2 worldMax,
    glm::vec4 color,
    float pointSize,
    float timeSeconds,
    bool additive,
    int mode) const {

    if (points.empty()) return;

    if (additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    m_pointsShader.Bind();
    m_pointsShader.SetInt("uMode", mode);
    m_pointsShader.SetVec2("uWorldMin", worldMin.x, worldMin.y);
    m_pointsShader.SetVec2("uWorldMax", worldMax.x, worldMax.y);
    m_pointsShader.SetVec4("uColor", color.x, color.y, color.z, color.w);
    m_pointsShader.SetFloat("uPointSize", pointSize);
    m_pointsShader.SetFloat("uTime", timeSeconds);

    glBindVertexArray(m_vaoA);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboA);
    glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(PointA), points.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_POINTS, 0, (GLsizei)points.size());

    glBindVertexArray(0);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


