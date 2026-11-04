#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace origon {

// GLSL program built from one file that holds both stages.
// The file uses "#pragma stage vertex" and "#pragma stage fragment" markers.
class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    bool loadFile(const std::filesystem::path& path, const std::string& defines = {});
    void bind() const;
    unsigned id() const { return m_program; }

    void set(const char* name, int value);
    void set(const char* name, float value);
    void set(const char* name, const glm::vec2& value);
    void set(const char* name, const glm::vec3& value);
    void set(const char* name, const glm::vec4& value);
    void set(const char* name, const glm::mat4& value);
    void setArray(const char* name, const glm::mat4* values, int count);

private:
    int location(const char* name);

    unsigned m_program = 0;
    std::unordered_map<std::string, int> m_locations;
};

} // namespace origon
