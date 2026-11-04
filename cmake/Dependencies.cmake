# All third-party dependencies, pinned to exact tags and fetched at configure time.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# --- GLFW (window + input) ---------------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)

# --- glad 2 (OpenGL 4.5 core loader, generated at configure time) -------------
# Requires Python 3 with jinja2.
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake)

# --- glm (math) ---------------------------------------------------------------
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)

# --- nlohmann/json (serialization) --------------------------------------------
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)

# --- Jolt Physics -------------------------------------------------------------
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.4.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build)

# --- Dear ImGui (docking branch, tools UI and menus) ----------------------------
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b-docking
    GIT_SHALLOW TRUE)

# --- ImGuizmo (map builder transform gizmos) ------------------------------------
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 105e4fde45e4e74aae8973d98750820ef7aca7e2)

# --- stb (image writing for screenshots, image loading later) ------------------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG d14d46f6b44456eb88beac58cba833a4135d4c89)

# --- cgltf (glTF 2.0 loading for models from the Blender pipeline) ---------------------
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.15
    GIT_SHALLOW TRUE)

# --- ENet (reliable UDP networking for the listen server) --------------------------------
FetchContent_Declare(enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
    GIT_SHALLOW TRUE)

# --- miniaudio (audio playback, 3D spatialization) ---------------------------------
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.26
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(glfw glad glm nlohmann_json JoltPhysics imgui)
# Header-only style sources: fetched without running their own CMake scripts.
FetchContent_GetProperties(enet)
if(NOT enet_POPULATED)
    FetchContent_Populate(enet)
endif()
FetchContent_GetProperties(cgltf)
if(NOT cgltf_POPULATED)
    FetchContent_Populate(cgltf)
endif()
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()
FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
endif()
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

glad_add_library(glad_gl45 STATIC REPRODUCIBLE LOADER API gl:core=4.5)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC glfw)

add_library(imguizmo STATIC ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(imguizmo PUBLIC ${imguizmo_SOURCE_DIR}/src)
target_link_libraries(imguizmo PUBLIC imgui)

add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(miniaudio PUBLIC ${miniaudio_SOURCE_DIR})
# Decoders and devices we do not need yet are left enabled; sounds are synthesized in memory.
if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio PUBLIC dl pthread m)
endif()

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${cgltf_SOURCE_DIR})

add_library(enet STATIC
    ${enet_SOURCE_DIR}/callbacks.c ${enet_SOURCE_DIR}/compress.c ${enet_SOURCE_DIR}/host.c
    ${enet_SOURCE_DIR}/list.c ${enet_SOURCE_DIR}/packet.c ${enet_SOURCE_DIR}/peer.c
    ${enet_SOURCE_DIR}/protocol.c ${enet_SOURCE_DIR}/unix.c ${enet_SOURCE_DIR}/win32.c)
target_include_directories(enet PUBLIC ${enet_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(enet PUBLIC ws2_32 winmm)
else()
    target_compile_definitions(enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1
        HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1 HAS_SOCKLEN_T=1)
endif()
