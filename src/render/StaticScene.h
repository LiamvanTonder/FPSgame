#pragma once

#include "physics/Collision.h"
#include "render/Pattern.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct SceneBlock;
class Terrain;

// Static scenery baked into per-chunk vertex buffers (colour and pattern per vertex),
// drawn with frustum and distance culling.
class StaticScene {
public:
    struct Vertex {
        glm::vec3 position, normal, color;
        float pattern;
    };
    struct Chunk {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei count = 0;
        glm::vec3 min{1e9f}, max{-1e9f};
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    void clear();
    void add(const SceneBlock& b);
    void addTerrain(const Terrain& t, const std::vector<glm::vec3>& vertexColors);
    void upload();
    void destroy();

    std::vector<Chunk>& chunks() { return m_list; }
    const std::vector<Chunk>& chunks() const { return m_list; }
    size_t triangleCount() const;

    float chunkSize = 64.0f;

private:
    Chunk& chunkAt(const glm::vec3& p);
    std::unordered_map<int64_t, int> m_index;
    std::vector<Chunk> m_list;
};
