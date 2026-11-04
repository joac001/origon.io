#include "core/Input.h"

#include <GLFW/glfw3.h>

namespace origon {
namespace {

Input* fromWindow(GLFWwindow* window) { return static_cast<Input*>(glfwGetWindowUserPointer(window)); }

} // namespace

void Input::attach(GLFWwindow* window) {
    glfwSetWindowUserPointer(window, this);
    // Installed before ImGui: the ImGui GLFW backend chains to these callbacks.
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
}

void Input::beginFrame() {
    m_keysPressed.fill(false);
    m_buttonsPressed.fill(false);
    m_mouseDelta = glm::vec2(0.0f);
    m_scrollDelta = 0.0f;
}

bool Input::keyDown(int key) const { return key >= 0 && key < kMaxKeys && m_keys[key]; }

bool Input::keyPressed(int key) const { return key >= 0 && key < kMaxKeys && m_keysPressed[key]; }

bool Input::mouseDown(int button) const { return button >= 0 && button < kMaxButtons && m_buttons[button]; }

bool Input::mousePressed(int button) const {
    return button >= 0 && button < kMaxButtons && m_buttonsPressed[button];
}

void Input::keyCallback(GLFWwindow* window, int key, int, int action, int) {
    Input* input = fromWindow(window);
    if (!input || key < 0 || key >= kMaxKeys) {
        return;
    }
    if (action == GLFW_PRESS) {
        input->m_keys[key] = true;
        input->m_keysPressed[key] = true;
    } else if (action == GLFW_RELEASE) {
        input->m_keys[key] = false;
    }
}

void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    Input* input = fromWindow(window);
    if (!input || button < 0 || button >= kMaxButtons) {
        return;
    }
    if (action == GLFW_PRESS) {
        input->m_buttons[button] = true;
        input->m_buttonsPressed[button] = true;
    } else if (action == GLFW_RELEASE) {
        input->m_buttons[button] = false;
    }
}

void Input::cursorPosCallback(GLFWwindow* window, double x, double y) {
    Input* input = fromWindow(window);
    if (!input) {
        return;
    }
    const glm::dvec2 cursor(x, y);
    if (input->m_hasCursor) {
        input->m_mouseDelta += glm::vec2(cursor - input->m_lastCursor);
    }
    input->m_lastCursor = cursor;
    input->m_hasCursor = true;
}

void Input::scrollCallback(GLFWwindow* window, double, double y) {
    Input* input = fromWindow(window);
    if (input) {
        input->m_scrollDelta += static_cast<float>(y);
    }
}

} // namespace origon
