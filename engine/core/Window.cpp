#include "core/Window.h"

#include "core/Log.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace origon {
namespace {

void glfwErrorCallback(int code, const char* description) {
    log::error("GLFW error {}: {}", code, description);
}

void APIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* message,
                              const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    if (type == GL_DEBUG_TYPE_ERROR) {
        log::error("GL [{}:{}:{}] {}", source, type, id, message);
    } else if (severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM) {
        log::warn("GL [{}:{}:{}] {}", source, type, id, message);
    }
}

} // namespace

Window::Window(const WindowDesc& desc) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Cannot create an OpenGL 4.5 core window");
    }

    glfwMakeContextCurrent(m_window);
    if (!gladLoadGL(glfwGetProcAddress)) {
        throw std::runtime_error("gladLoadGL failed");
    }

    log::info("OpenGL {} | {} | {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
              reinterpret_cast<const char*>(glGetString(GL_VENDOR)));

    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugCallback, nullptr);
    }

    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    setVsync(desc.vsync);
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window); }

void Window::requestClose() { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }

void Window::pollEvents() { glfwPollEvents(); }

void Window::swapBuffers() { glfwSwapBuffers(m_window); }

void Window::setVsync(bool enabled) { glfwSwapInterval(enabled ? 1 : 0); }

void Window::setCursorCaptured(bool captured) {
    m_cursorCaptured = captured;
    glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

glm::ivec2 Window::framebufferSize() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return {width, height};
}

} // namespace origon
