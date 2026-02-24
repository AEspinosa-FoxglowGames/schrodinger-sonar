#pragma once
#include <string>
#include <glad/glad.h>

class Shader {
public:
    bool LoadFromFiles(const std::string& vsPath, const std::string& fsPath);
    void Destroy();

    void Bind() const;
    GLuint Program() const { return m_program; }

    void SetFloat(const char* name, float v) const;
    void SetInt(const char* name, int v) const;
    void SetVec2(const char* name, float x, float y) const;
    void SetVec4(const char* name, float x, float y, float z, float w) const;

private:
    GLuint m_program = 0;

    static std::string ReadTextFile(const std::string& path);
    static GLuint Compile(GLenum type, const std::string& src);
};

