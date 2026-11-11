#include "render/Shader.h"

#include "core/Paths.h"

#include <glm/gtc/type_ptr.hpp>
#include <cstdio>

static GLuint compileStage(GLenum type, const std::string& source, const std::string& path) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error in %s:\n%s\n", path.c_str(), log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::load(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vsSrc, fsSrc;
    if (!readTextFile(vertexPath, vsSrc) || !readTextFile(fragmentPath, fsSrc)) {
        std::fprintf(stderr, "Cannot read shader %s / %s\n", vertexPath.c_str(), fragmentPath.c_str());
        return false;
    }
    GLuint vs = compileStage(GL_VERTEX_SHADER, vsSrc, vertexPath);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fsSrc, fragmentPath);
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
        char log[2048];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader link error (%s):\n%s\n", vertexPath.c_str(), log);
        return false;
    }
    return true;
}

void Shader::destroy() {
    if (m_program) glDeleteProgram(m_program);
    m_program = 0;
}

void Shader::use() const { glUseProgram(m_program); }

GLint Shader::location(const char* name) const {
    auto it = m_locations.find(name);
    if (it != m_locations.end()) return it->second;
    GLint loc = glGetUniformLocation(m_program, name);
    m_locations.emplace(name, loc);
    return loc;
}

void Shader::set(const char* name, int v) const { glUniform1i(location(name), v); }
void Shader::set(const char* name, float v) const { glUniform1f(location(name), v); }
void Shader::set(const char* name, const glm::vec2& v) const { glUniform2fv(location(name), 1, glm::value_ptr(v)); }
void Shader::set(const char* name, const glm::vec3& v) const { glUniform3fv(location(name), 1, glm::value_ptr(v)); }
void Shader::set(const char* name, const glm::vec4& v) const { glUniform4fv(location(name), 1, glm::value_ptr(v)); }
void Shader::set(const char* name, const glm::mat3& m) const { glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::set(const char* name, const glm::mat4& m) const { glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(m)); }
