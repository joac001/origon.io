add_library(origon_engine STATIC
    core/Window.cpp
    core/Input.cpp
    core/FileSystem.cpp
    render/Shader.cpp
    render/Mesh.cpp
    render/Renderer.cpp
    physics/PhysicsWorld.cpp
    audio/AudioSystem.cpp
    audio/Synth.cpp
    render/Screenshot.cpp
    render/Texture.cpp
    render/Model.cpp
    render/SkinnedModel.cpp)

target_include_directories(origon_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(origon_engine
    PUBLIC glfw glad_gl45 glm::glm Jolt nlohmann_json::nlohmann_json
    PRIVATE miniaudio stb cgltf origon_warnings)

target_compile_definitions(origon_engine PUBLIC
    GLFW_INCLUDE_NONE
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_DEPTH_ZERO_TO_ONE=0
    ORIGON_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
