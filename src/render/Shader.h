#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

class Shader {
public:
    bool load(const std::string& vertexPath, const std::string& fragmentPath);
    void destroy();
    void use() const;

    void set(const char* name, int v) const;
    void set(const char* name, float v) const;
    void set(const char* name, const glm::vec2& v) const;
    void set(const char* name, const glm::vec3& v) const;
    void set(const char* name, const glm::vec4& v) const;
    void set(const char* name, const glm::mat3& m) const;
    void set(const char* name, const glm::mat4& m) const;

    GLuint id() const { return m_program; }

private:
    GLint location(const char* name) const;

    GLuint m_program = 0;
    mutable std::unordered_map<std::string, GLint> m_locations;
};
