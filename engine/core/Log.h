#pragma once

#include <cstdio>
#include <format>
#include <string_view>

namespace origon::log {

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    std::fputs(("[info] " + std::format(fmt, std::forward<Args>(args)...) + "\n").c_str(), stdout);
    std::fflush(stdout);
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    std::fputs(("[warn] " + std::format(fmt, std::forward<Args>(args)...) + "\n").c_str(), stderr);
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    std::fputs(("[error] " + std::format(fmt, std::forward<Args>(args)...) + "\n").c_str(), stderr);
}

} // namespace origon::log
