#pragma once

#include <glm/vec2.hpp>
#include <array>

struct GLFWwindow;

// Polled once per rendered frame. Edge queries (pressed) are valid for that frame.
class Input {
public:
    void attach(GLFWwindow* window);
    void update();

    bool down(int key) const;
    bool pressed(int key) const;
    bool mouseDown(int button) const;
    bool mousePressed(int button) const;

    // Cursor movement in pixels/counts since the previous update.
    glm::vec2 mouseDelta() const { return m_mouseDelta; }
    // Wheel notches since the previous update (positive = away from the user).
    float scrollDelta() const { return m_scrollDelta; }
    // Call after capture state changes to avoid a jump. The OS warps the cursor
    // asynchronously, so deltas are ignored for a couple of frames.
    void resetMouse() { m_resetFrames = 3; }

private:
    static constexpr int kMaxKeys = 512;
    static constexpr int kMaxButtons = 8;

    GLFWwindow* m_window = nullptr;
    std::array<bool, kMaxKeys> m_keys{}, m_prevKeys{};
    std::array<bool, kMaxButtons> m_buttons{}, m_prevButtons{};
    double m_lastX = 0, m_lastY = 0;
    int m_resetFrames = 3;
    glm::vec2 m_mouseDelta{0.0f};
    float m_scrollAccum = 0.0f;
    float m_scrollDelta = 0.0f;

    static void scrollCallback(GLFWwindow* window, double dx, double dy);
};
