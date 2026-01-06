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
     
    /*explicit RGBA(float fre, float fgr, float fbl, float falpha) 
    : r(fromf(fre)), g(fromf(fgr)), b(fromf(fbl)), a(fromf(falpha)) {}
    explicit RGBA(float fre, float fgr, float fbl) 
    : r(fromf(fre)), g(fromf(fgr)), b(fromf(fbl)) {}
    
    explicit RGBA(uint8_t re, uint8_t gr, uint8_t bl, float falpha)
     : r(re), g(gr), b(bl), a(fromf(falpha)) {}
    */
    
  private:
    static uint8_t fromf(float f) {
        float clamped = std::clamp(f, 0.0f, 1.0f);
        return static_cast<uint8_t>(std::lroundf(255.0f * clamped));
    }
};