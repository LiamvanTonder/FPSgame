#pragma once

#include "render/Shader.h"

#include <glm/glm.hpp>
#include <vector>

struct OBB;

// Immediate-mode world-space line batch, cleared every frame.
class DebugDraw {
public:
    bool init(const std::string& shaderDir);
    void shutdown();

    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);
    void box(const OBB& box, const glm::vec4& color);
    void cross(const glm::vec3& p, float size, const glm::vec4& color);

    // depthTest=false draws on top of everything.
    void render(const glm::mat4& viewProj, bool depthTest = true);
    void clear() { m_vertices.clear(); }

private:
    struct LineVertex {
        glm::vec3 pos;
        glm::vec4 color;
    };
    Shader m_shader;
    GLuint m_vao = 0, m_vbo = 0;
    std::vector<LineVertex> m_vertices;
};
