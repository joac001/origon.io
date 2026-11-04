#pragma once

#include <glm/vec2.hpp>

#include <array>

struct GLFWwindow;

namespace origon {

// Polled keyboard and mouse state. Key codes are GLFW key codes.
// Call beginFrame() once per frame, before glfwPollEvents().
class Input {
public:
    static constexpr int kMaxKeys = 512;
    static constexpr int kMaxButtons = 8;

    void attach(GLFWwindow* window);
    void beginFrame();

    bool keyDown(int key) const;
    bool keyPressed(int key) const;
    bool mouseDown(int button) const;
    bool mousePressed(int button) const;

    // Mouse movement in pixels since the last beginFrame().
    glm::vec2 mouseDelta() const { return m_mouseDelta; }
    float scrollDelta() const { return m_scrollDelta; }

private:
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double x, double y);
    static void scrollCallback(GLFWwindow* window, double x, double y);

    std::array<bool, kMaxKeys> m_keys{};
    std::array<bool, kMaxKeys> m_keysPressed{};
    std::array<bool, kMaxButtons> m_buttons{};
    std::array<bool, kMaxButtons> m_buttonsPressed{};
    glm::dvec2 m_lastCursor{0.0};
    bool m_hasCursor = false;
    glm::vec2 m_mouseDelta{0.0f};
    float m_scrollDelta = 0.0f;
};

} // namespace origon
