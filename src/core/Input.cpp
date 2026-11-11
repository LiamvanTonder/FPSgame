#include "core/Input.h"

#include <GLFW/glfw3.h>

void Input::attach(GLFWwindow* window) {
    m_window = window;
    m_resetFrames = 3;
    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, scrollCallback);
}

void Input::scrollCallback(GLFWwindow* window, double, double dy) {
    if (auto* self = static_cast<Input*>(glfwGetWindowUserPointer(window))) self->m_scrollAccum += float(dy);
}

void Input::update() {
    m_scrollDelta = m_scrollAccum;
    m_scrollAccum = 0.0f;
    m_prevKeys = m_keys;
    m_prevButtons = m_buttons;
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST && k < kMaxKeys; ++k)
        m_keys[k] = glfwGetKey(m_window, k) == GLFW_PRESS;
    for (int b = 0; b < kMaxButtons; ++b)
        m_buttons[b] = glfwGetMouseButton(m_window, b) == GLFW_PRESS;

    double x, y;
    glfwGetCursorPos(m_window, &x, &y);
    if (m_resetFrames > 0) {
        m_lastX = x;
        m_lastY = y;
        --m_resetFrames;
    }
    m_mouseDelta = glm::vec2(float(x - m_lastX), float(y - m_lastY));
    m_lastX = x;
    m_lastY = y;
}

bool Input::down(int key) const { return key >= 0 && key < kMaxKeys && m_keys[key]; }
bool Input::pressed(int key) const { return down(key) && !m_prevKeys[key]; }
bool Input::mouseDown(int button) const { return button >= 0 && button < kMaxButtons && m_buttons[button]; }
bool Input::mousePressed(int button) const { return mouseDown(button) && !m_prevButtons[button]; }
