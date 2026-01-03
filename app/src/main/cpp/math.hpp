// math.hpp - minimal, explicit column-major 4x4 + basic vectors
#pragma once
#include <cmath>
#include <cstddef>

// ---------------- Vectors ----------------

struct Vec2 {
    float x=0, y=0;
    constexpr Vec2() = default;
    constexpr Vec2(float X, float Y) : x(X), y(Y) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x+o.x, y+o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x-o.x, y-o.y}; }
    constexpr Vec2 operator*(float s) const { return {x*s, y*s}; }
};

struct Vec3 {
    float x=0, y=0, z=0;
    constexpr Vec3() = default;
    constexpr Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

    constexpr Vec3 operator+(Vec3 o) const { return {x+o.x, y+o.y, z+o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x-o.x, y-o.y, z-o.z}; }
    constexpr Vec3 operator*(float s) const { return {x*s, y*s, z*s}; }
};

struct Vec4 {
    float x=0, y=0, z=0, w=0;
    constexpr Vec4() = default;
    constexpr Vec4(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) {}
};

inline float dot(Vec2 a, Vec2 b) { return a.x*b.x + a.y*b.y; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

inline float length(Vec2 v) { return std::sqrt(dot(v,v)); }
inline float length(Vec3 v) { return std::sqrt(dot(v,v)); }

inline Vec2 normalize(Vec2 v) {
    float L = length(v);
    return (L > 0.0f) ? v*(1.0f/L) : Vec2{};
}
inline Vec3 normalize(Vec3 v) {
    float L = length(v);
    return (L > 0.0f) ? v*(1.0f/L) : Vec3{};
}

inline Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    };
}

// ---------------- Mat4 (column-major) ----------------
//
// Storage: m[col*4 + row]
// Matches OpenGL expectations: glUniformMatrix4fv(..., GL_FALSE, m)
//

struct Mat4 {
    float m[16]{};

    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 translate(float tx, float ty, float tz=0.0f) {
        Mat4 r = identity();
        r.m[12] = tx;
        r.m[13] = ty;
        r.m[14] = tz;
        return r;
    }

    static Mat4 scale(float sx, float sy, float sz=1.0f) {
        Mat4 r{};
        r.m[0]  = sx;
        r.m[5]  = sy;
        r.m[10] = sz;
        r.m[15] = 1.0f;
        return r;
    }

    // 2D UI-friendly ortho:
    // If you want top-left origin with y down:
    //   ortho(0, width, height, 0, -1, 1)
    static Mat4 ortho(float l, float r, float b, float t, float n=-1.0f, float f=1.0f) {
        Mat4 M{};
        const float rl = r - l;
        const float tb = t - b;
        const float fn = f - n;

        M.m[0]  =  2.0f / rl;
        M.m[5]  =  2.0f / tb;
        M.m[10] = -2.0f / fn;
        M.m[15] =  1.0f;

        M.m[12] = -(r + l) / rl;
        M.m[13] = -(t + b) / tb;
        M.m[14] = -(f + n) / fn;
        return M;
    }

    static Mat4 rotateZ(float radians) {
        Mat4 r = identity();
        float c = std::cos(radians);
        float s = std::sin(radians);

        // column-major
        r.m[0] =  c; r.m[4] = -s;
        r.m[1] =  s; r.m[5] =  c;
        return r;
    }

    const float* data() const { return m; }
    float* data() { return m; }
};

inline Mat4 mul(const Mat4& A, const Mat4& B) {
    // C = A * B (column-major)
    Mat4 C{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            C.m[col*4 + row] =
                A.m[0*4 + row] * B.m[col*4 + 0] +
                A.m[1*4 + row] * B.m[col*4 + 1] +
                A.m[2*4 + row] * B.m[col*4 + 2] +
                A.m[3*4 + row] * B.m[col*4 + 3];
        }
    }
    return C;
}

inline Vec4 mul(const Mat4& M, const Vec4& v) {
    // column-major: result = M * v
    return {
        M.m[0]*v.x + M.m[4]*v.y + M.m[8]*v.z  + M.m[12]*v.w,
        M.m[1]*v.x + M.m[5]*v.y + M.m[9]*v.z  + M.m[13]*v.w,
        M.m[2]*v.x + M.m[6]*v.y + M.m[10]*v.z + M.m[14]*v.w,
        M.m[3]*v.x + M.m[7]*v.y + M.m[11]*v.z + M.m[15]*v.w
    };
}