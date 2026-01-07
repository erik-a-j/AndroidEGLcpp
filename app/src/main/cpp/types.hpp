// types.hpp
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

struct RGBA {
    uint8_t r{0}, g{0}, b{0}, a{255};
    
    RGBA() = default;
    RGBA(uint8_t re, uint8_t gr, uint8_t bl, uint8_t alpha)
     : r(re), g(gr), b(bl), a(alpha) {}
    RGBA(uint8_t re, uint8_t gr, uint8_t bl)
     : r(re), g(gr), b(bl) {}
    
    uint32_t pack() const {
        return (uint32_t(r)      ) |
               (uint32_t(g) <<  8) |
               (uint32_t(b) << 16) |
               (uint32_t(a) << 24);
    }
};