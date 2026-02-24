#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::string Shader::ReadTextFile(const std::string& path) {
    std::ifstream f(path, std::ios::in);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint Shader::Compile(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::cerr << "Shader compile error:\n" << log << "\n";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Shader::LoadFromFiles(const std::string& vsPath, const std::string& fsPath) {
    Destroy();

    auto vsSrc = ReadTextFile(vsPath);
    auto fsSrc = ReadTextFile(fsPath);
    if (vsSrc.empty() || fsSrc.empty()) {
        std::cerr << "Failed to read shader files: " << vsPath << " / " << fsPath << "\n";
        return false;
    }

    GLuint vs = Compile(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return false;

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetProgramInfoLog(m_program, len, nullptr, log.data());
        std::cerr << "Program link error:\n" << log << "\n";
        Destroy();
        return false;
    }

    return true;
}

void Shader::Destroy() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

void Shader::Bind() const {
    glUseProgram(m_program);
}

void Shader::SetFloat(const char* name, float v) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc >= 0) glUniform1f(loc, v);
}

void Shader::SetInt(const char* name, int v) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc >= 0) glUniform1i(loc, v);
}

void Shader::SetVec2(const char* name, float x, float y) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc >= 0) glUniform2f(loc, x, y);
}

void Shader::SetVec4(const char* name, float x, float y, float z, float w) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc >= 0) glUniform4f(loc, x, y, z, w);
}
