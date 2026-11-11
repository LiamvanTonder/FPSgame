#pragma once

struct GLFWwindow;

class Window {
public:
    bool create(int width, int height, const char* title);
    void destroy();

    bool shouldClose() const;
    void requestClose();
    void swapBuffers();
    void pollEvents();

    // Captured = cursor hidden and locked, raw mouse motion enabled.
    void setMouseCaptured(bool captured);
    bool isMouseCaptured() const { return m_captured; }
    bool isFocused() const;

    // Framebuffer size in pixels.
    int width() const;
    int height() const;

    GLFWwindow* handle() const { return m_window; }

private:
    GLFWwindow* m_window = nullptr;
    bool m_captured = false;
};
