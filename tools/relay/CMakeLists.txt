# Relay ("servidor intermedio"): lets players behind NAT host games over the internet.
# Headless: depends only on ENet and the header-only parts of the game protocol.
add_executable(origon_relay main.cpp)
target_include_directories(origon_relay PRIVATE ${CMAKE_SOURCE_DIR}/game)
target_link_libraries(origon_relay PRIVATE origon_engine enet origon_warnings)
