#include "render/DebugDraw.h"

#include "physics/Collision.h"

bool DebugDraw::init(const std::string& shaderDir) {
    if (!m_shader.load(shaderDir + "line.vert", shaderDir + "line.frag")) return false;
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)offsetof(LineVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)offsetof(LineVertex, color));
    glBindVertexArray(0);
    return true;
}

void DebugDraw::shutdown() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_shader.destroy();
}

void DebugDraw::line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
    m_vertices.push_back({a, color});
    m_vertices.push_back({b, color});
}

void DebugDraw::box(const OBB& b, const glm::vec4& color) {
    glm::vec3 c[8];
    for (int i = 0; i < 8; ++i) {
        glm::vec3 s((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
        c[i] = b.center + b.rotation * (s * b.half);
    }
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) line(c[e[0]], c[e[1]], color);
}

void DebugDraw::cross(const glm::vec3& p, float s, const glm::vec4& color) {
    line(p - glm::vec3(s, 0, 0), p + glm::vec3(s, 0, 0), color);
    line(p - glm::vec3(0, s, 0), p + glm::vec3(0, s, 0), color);
    line(p - glm::vec3(0, 0, s), p + glm::vec3(0, 0, s), color);
}

void DebugDraw::render(const glm::mat4& viewProj, bool depthTest) {
    if (m_vertices.empty()) return;
    if (!depthTest) glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m_vertices.size() * sizeof(LineVertex)), m_vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, GLsizei(m_vertices.size()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
