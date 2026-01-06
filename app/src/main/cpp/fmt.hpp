#pragma once

#include <format>
#include <string>

namespace Fmt {

template <class... Args>
inline std::string f(std::format_string<Args...> fmt, Args&&... args) {
    return std::format(fmt, std::forward<Args>(args)...);
}

}