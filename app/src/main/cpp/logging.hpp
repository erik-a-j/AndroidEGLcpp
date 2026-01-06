#pragma once

#include <android/log.h>
#include "fmt.hpp"
#include <string_view>


namespace logx {

#ifndef PROGRAM_NAME
#define PROGRAM_NAME "EGL"
//#error "must define PROGRAM_NAME"
#endif
#ifdef LOG_NAMESPACE
constexpr const char *TAG = PROGRAM_NAME "::" LOG_NAMESPACE;
#else
constexpr const char *TAG = PROGRAM_NAME;
#endif

// Plain message
inline int I(std::string_view msg) {
    return __android_log_print(ANDROID_LOG_INFO, TAG, "%.*s",
                               (int)msg.size(), msg.data());
}
inline int E(std::string_view msg) {
    return __android_log_print(ANDROID_LOG_ERROR, TAG, "%.*s",
                               (int)msg.size(), msg.data());
}

// Compile-time checked formatting (C++20)
template <class... Args>
inline int If(std::format_string<Args...> fmt, Args&&... args) {
    return I(Fmt::f(fmt, std::forward<Args>(args)...));
}

template <class... Args>
inline int Ef(std::format_string<Args...> fmt, Args&&... args) {
    return E(Fmt::f(fmt, std::forward<Args>(args)...));;
}

} // namespace logx

// Optional macros
//#define LOGI(...) ::logx::If(__VA_ARGS__)
//#define LOGE(...) ::logx::Ef(__VA_ARGS__)

// If you also want message-only macros:
//#define LOGI_MSG(msg) ::logx::I((msg))
//#define LOGE_MSG(msg) ::logx::E((msg))
