# Simulation and world data: no window or GL context needed (also used by tests and,
# later, by a dedicated server).
file(GLOB ORIGON_SIM_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/sim/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/world/*.cpp)
add_library(origon_sim STATIC ${ORIGON_SIM_SOURCES})
target_include_directories(origon_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(origon_sim PUBLIC origon_engine PRIVATE origon_warnings)

file(GLOB ORIGON_GAME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fx/*.cpp)
add_executable(origon ${ORIGON_GAME_SOURCES})
target_link_libraries(origon PRIVATE origon_sim imgui imguizmo origon_warnings)
