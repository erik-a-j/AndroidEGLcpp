#pragma once

#include <android/log.h>
#include "fmt.hpp"
#include <string>
#include <string_view>


namespace logger {

#ifndef PROGRAM_NAME
#define PROGRAM_NAME "EGL"
#endif

template <const char *NS>
struct logx {
    static constexpr const std::string TAG = std::string{PROGRAM_NAME} + "::" + NS;
    static int I(std::string_view msg) {
        return __android_log_print(ANDROID_LOG_INFO, TAG.c_str(), "%.*s",
                                   (int)msg.size(), msg.data());
    }
    static int E(std::string_view msg) {
        return __android_log_print(ANDROID_LOG_ERROR, TAG.c_str(), "%.*s",
                                   (int)msg.size(), msg.data());
    }
    
    // Compile-time checked formatting (C++20)
    template <class... Args>
    static int If(std::format_string<Args...> fmt, Args&&... args) {
        return I(Fmt::f(fmt, std::forward<Args>(args)...));
    }
    
    template <class... Args>
    static int Ef(std::format_string<Args...> fmt, Args&&... args) {
        return E(Fmt::f(fmt, std::forward<Args>(args)...));;
    }
};

} // namespace logx
