#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
};

// GPU mesh. Plain handle type: copying shares the GL objects; call destroy() once.
class Mesh {
public:
    void upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void draw() const;
    void destroy();

    // Unit primitives, all centred on the origin.
    static Mesh makeBox();                   // 1x1x1
    static Mesh makeCylinder(int segments);  // radius 0.5, length 1 along Z
    static Mesh makeSphere(int stacks, int slices); // radius 0.5

private:
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLsizei m_count = 0;
};
