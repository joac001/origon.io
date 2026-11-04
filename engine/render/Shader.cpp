#include "render/Shader.h"

#include "core/FileSystem.h"
#include "core/Log.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include <utility>

namespace origon {
namespace {

bool splitStages(const std::string& source, std::string& vertex, std::string& fragment) {
    const std::string vertexTag = "#pragma stage vertex";
    const std::string fragmentTag = "#pragma stage fragment";
    const size_t vertexPos = source.find(vertexTag);
    const size_t fragmentPos = source.find(fragmentTag);
    if (vertexPos == std::string::npos || fragmentPos == std::string::npos || fragmentPos < vertexPos) {
        return false;
    }
    const std::string common = source.substr(0, vertexPos);
    vertex = common + source.substr(vertexPos + vertexTag.size(), fragmentPos - vertexPos - vertexTag.size());
    fragment = common + source.substr(fragmentPos + fragmentTag.size());
    return true;
}

GLuint compile(GLenum stage, const std::string& source, const std::string& name) {
    const GLuint shader = glCreateShader(stage);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char infoLog[4096];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        log::error("Shader compile failed ({} {}):\n{}", name, stage == GL_VERTEX_SHADER ? "vertex" : "fragment",
                   infoLog);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

Shader::~Shader() {
    if (m_program) {
        glDeleteProgram(m_program);
    }
}

Shader::Shader(Shader&& other) noexcept
    : m_program(std::exchange(other.m_program, 0)), m_locations(std::move(other.m_locations)) {}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_program) {
            glDeleteProgram(m_program);
        }
        m_program = std::exchange(other.m_program, 0);
        m_locations = std::move(other.m_locations);
    }
    return *this;
}

namespace {

// Expands lines of the form: #include "relative/path.glsl" (relative to assets/shaders).
std::string expandIncludes(const std::string& source) {
    std::string out;
    size_t start = 0;
    while (start < source.size()) {
        size_t end = source.find('\n', start);
        if (end == std::string::npos) end = source.size();
        const std::string line = source.substr(start, end - start);
        const std::string tag = "#include \"";
        if (line.rfind(tag, 0) == 0) {
            const size_t close = line.find('"', tag.size());
            const std::string name = line.substr(tag.size(), close - tag.size());
            const auto included = fs::readText(fs::resolve(std::filesystem::path("assets/shaders") / name));
            if (included) {
                out += *included;
            } else {
                log::error("Shader include not found: {}", name);
            }
        } else {
            out += line;
        }
        out += '\n';
        start = end + 1;
    }
    return out;
}

} // namespace

bool Shader::loadFile(const std::filesystem::path& path, const std::string& defines) {
    auto source = fs::readText(fs::resolve(path));
    if (!source) {
        return false;
    }
    source = expandIncludes(*source);
    std::string vertexBody;
    std::string fragmentBody;
    if (!splitStages(*source, vertexBody, fragmentBody)) {
        log::error("Shader {} is missing stage markers", path.string());
        return false;
    }
    const std::string header = "#version 450 core\n" + defines + "\n#line 1\n";
    const GLuint vertex = compile(GL_VERTEX_SHADER, header + vertexBody, path.string());
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, header + fragmentBody, path.string());
    if (!vertex || !fragment) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return false;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char infoLog[4096];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        log::error("Shader link failed ({}):\n{}", path.string(), infoLog);
        glDeleteProgram(program);
        return false;
    }

    if (m_program) {
        glDeleteProgram(m_program);
    }
    m_program = program;
    m_locations.clear();
    return true;
}

void Shader::bind() const { glUseProgram(m_program); }

int Shader::location(const char* name) {
    auto it = m_locations.find(name);
    if (it != m_locations.end()) {
        return it->second;
    }
    const int loc = glGetUniformLocation(m_program, name);
    m_locations.emplace(name, loc);
    return loc;
}

void Shader::set(const char* name, int value) { glProgramUniform1i(m_program, location(name), value); }

void Shader::set(const char* name, unsigned value) { glProgramUniform1ui(m_program, location(name), value); }

void Shader::set(const char* name, float value) { glProgramUniform1f(m_program, location(name), value); }

void Shader::set(const char* name, const glm::vec2& value) {
    glProgramUniform2fv(m_program, location(name), 1, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::vec3& value) {
    glProgramUniform3fv(m_program, location(name), 1, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::vec4& value) {
    glProgramUniform4fv(m_program, location(name), 1, glm::value_ptr(value));
}

void Shader::setArray(const char* name, const glm::mat4* values, int count) {
    glProgramUniformMatrix4fv(m_program, location(name), count, GL_FALSE, glm::value_ptr(values[0]));
}

void Shader::set(const char* name, const glm::mat4& value) {
    glProgramUniformMatrix4fv(m_program, location(name), 1, GL_FALSE, glm::value_ptr(value));
}

} // namespace origon
