// ui_renderer.hpp
#pragma once

#include <GLES3/gl31.h>

#include "assets.hpp"
#include "types.hpp"
#include "bitmask.hpp"

#include <cstdint>
#include <vector>
#include <variant>

/* --- opts --- */
enum class UiO : uint32_t {
    None = 0,
    ColorTL = 1u << 0,
    ColorTR = 1u << 1,
    ColorBR = 1u << 2,
    ColorBL = 1u << 3,
    Color = ColorTL | ColorTR | ColorBR | ColorBL,
    All = Color
};
template <>
struct bitmask::bitmask_traits<UiO> {
    static constexpr bool enable = true;
    static constexpr UiO mask = UiO::All;
};

struct alignas(16) UiRectInst {
    float cx, cy, hx, hy;      // vec4 c_half
    float radius, feather;     // rad_fea.x/y
    float _pad0, _pad1;        // pad to 16 bytes
    uint32_t tl, tr, br, bl;   // uvec4 packed RGBA8
};
static_assert(sizeof(UiRectInst) == 48);
static_assert(alignof(UiRectInst) == 16);

struct UiColors {
    RGBA tl, tr, br, bl;
    UiColors() = default;
    UiColors(const RGBA& ctl, const RGBA& ctr, 
             const RGBA& cbr, const RGBA& cbl)
     : tl(ctl), tr(ctr), br(cbr), bl(cbl) {}
    UiColors(const RGBA& c)
     : tl(c), tr(c), br(c), bl(c) {}
    
    struct Packed {
        uint32_t tl, tr, br, bl;
        explicit Packed(const UiColors& cc)
         : tl(cc.tl.pack()), tr(cc.tr.pack()), br(cc.br.pack()), bl(cc.bl.pack()) {}
    };
    Packed pack() const { return Packed(*this); }
};
struct UiQuad {
    float x0, y0, x1, y1;
    UiColors cc;
    float radius{0.0f};
    float feather{1.0f};
    UiQuad() = default;
    UiQuad(float x0, float y0, float x1, float y1, 
           const RGBA& c, float rad = 0.0f, float fea = 1.0f) 
     : x0(x0), y0(y0), x1(x1), y1(y1), 
       cc(c), radius(rad), feather(fea) {}
    UiQuad(float x0, float y0, float x1, float y1, 
           const UiColors& cc, float rad = 0.0f, float fea = 1.0f) 
     : x0(x0), y0(y0), x1(x1), y1(y1), 
       cc(cc), radius(rad), feather(fea) {}
};

class UiRenderer {
public:
    UiRenderer() = default;
    ~UiRenderer();

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    /*Must be called after EGL context is current.
    If you prefer to manage shaders outside, you can skip initProgram()
    and provide your own program + uniform locations to draw().*/
    bool init(const Assets::Manager& am);
    void shutdown();

    // Record UI geometry for this frame
    void begin(); // clears internal vertex list
    void rectFilled(float x, float y, float w, float h, const UiColors& cc, float radius = 0.0f, float feather = 1.0f);
    void rectOutline(float x, float y, float w, float h, float t, const UiColors& cc);
    void line(float x0, float y0, float x1, float y1, float thickness, const UiColors& cc);
    void end();   // uploads VBO (or you can upload in draw)

    // Draw all recorded UI. Requires an orthographic MVP like your text uses.
    // If you use UiRenderer’s internal program, pass program=0 and uMVP=-1 to use internal.
    void draw(const float* mvp4x4);

    struct Handle { int id = -1; };
    Handle createObj();
    void destroyObj(Handle h);
    void objClear(Handle h);
    void objRectFilled(Handle hdl, float x, float y, float w, float h, const UiColors& cc, float radius = 0.0f, float feather = 1.0f);
    void objRectOutline(Handle hdl, float x, float y, float w, float h, float t, const UiColors& cc);
    void objLine(Handle hdl, float x0, float y0, float x1, float y1, float thickness, const UiColors& cc);

    using optarg_t = std::variant<
        std::monostate,
        RGBA,
        UiColors
    >;
    void objRectOpts(Handle h, UiO opts, optarg_t arg = {});
    template<class T> requires std::constructible_from<optarg_t, T>
    void objRectOpts(Handle h, UiO opts, T&& arg) { objRectOpts(h, opts, optarg_t{std::forward<T>(arg)}); }
    
    void updateObjects();
    void drawObjects(const float* mvp4x4);

    // Optional: use internal program (created in init()).
    GLuint program() const { return m_prog; }
    GLint  uMVP() const { return m_uMVP; }

    int vertexCount() const { return (int)m_frame.inst.size(); }
private:
    struct UiObj {
        bool alive = true;
        bool gpuDirty = true;

        std::vector<UiRectInst> inst;
        GLsizei instanceCount = 0;
    
        GLuint texF = 0; // RGBA16F/32F
        GLuint texU = 0; // RGBA8UI
    
        int wF = 0, hF = 0;
        int wU = 0, hU = 0;
    };

    UiObj* get(Handle h);
    void destroyObj(UiObj& o);
    void objClear(UiObj& o);
    void objRectFilled(UiObj& o, float x, float y, float w, float h, const UiColors& cc, float radius = 0.0f, float feather = 1.0f);
    void objRectOutline(UiObj& o, float x, float y, float w, float h, float t, const UiColors& cc);
    void objLine(UiObj& o, float x0, float y0, float x1, float y1, float thickness, const UiColors& cc);

    void objSetUiColors(UiObj& o, UiO opts, const UiColors& cc);
    void objRectOpts(UiObj& o, UiO opts, optarg_t arg);
    
    bool initProgram(const Assets::Manager& am);
    void destroyProgram();
    void uploadObj(UiObj& o, GLenum usage);
    void drawObj(const UiObj& o);

private:
    UiObj m_frame;
    std::vector<UiObj> m_objs;

    // Internal shader (optional)
    GLuint m_prog = 0;
    GLint  m_uMVP = -1;
    GLuint m_quadVao = 0;
    GLuint m_quadVbo = 0;
    GLuint m_quadEbo = 0;
    GLint m_uInstF  = -1;
    GLint m_uInstU  = -1;
    GLint m_uInstF_W = -1;
    GLint m_uInstU_W = -1;
};