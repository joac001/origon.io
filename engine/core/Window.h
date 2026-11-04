#pragma once

#include <glm/vec2.hpp>
#include <string>

struct GLFWwindow;

namespace origon {

struct WindowDesc {
    std::string title = "Origon";
    int width = 1600;
    int height = 900;
    bool vsync = true;
};

class Window {
public:
    explicit Window(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void requestClose();
    void pollEvents();
    void swapBuffers();

    void setVsync(bool enabled);
    void setCursorCaptured(bool captured);
    bool cursorCaptured() const { return m_cursorCaptured; }

    glm::ivec2 framebufferSize() const;
    GLFWwindow* handle() const { return m_window; }

private:
    GLFWwindow* m_window = nullptr;
    bool m_cursorCaptured = false;
};

} // namespace origon
