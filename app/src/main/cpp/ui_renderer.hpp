// ui_renderer.hpp
#pragma once

#include <GLES3/gl3.h>

#include "assets.hpp"
#include "types.hpp"

#include <cstdint>
#include <vector>

struct UiVtx {
    float x, y;      // screen-space (pixels)
    uint8_t r, g, b, a; // per-vertex RGBA
    float cx, cy;             // rect center (pixels)
    float hx, hy;             // rect half-size (pixels)
    float radius;             // corner radius (pixels)
    float feather;            // AA width (pixels)
    
    UiVtx(float px, float py, const RGBA& c,
          float rcx, float rcy, float rhx, float rhy, 
          float rad, float fea)
      : x(px), y(py), r(c.r), g(c.g), b(c.b), a(c.a),
        cx(rcx), cy(rcy), hx(rhx), hy(rhy), radius(rad), feather(fea) {}
    /*UiVtx(const UiVtx& o)
     : x(o.x), y(o.y), r(o.r), g(o.g), b(o.b), a(o.a),
       cx(o.cx), cy(o.cy), hx(o.hx), hy(o.hy), radius(o.radius), feather(o.feather) {}
    UiVtx(const UiVtx& o, float rcx, float rcy, float rhx, float rhy)
     : UiVtx(o) { cx = rcx; cy = rcy; hx = rhx; hy = rhy; }*/
};
struct UiQuad {
    float x0, y0, x1, y1;
    RGBA c0, c1, c2, c3;
    float radius{0.0f};
    float feather{1.0f};
    UiQuad() = default;
    UiQuad(float x0, float y0, float x1, float y1, 
           const RGBA& c, float rad = 0.0f, float fea = 1.0f) 
     : x0(x0), y0(y0), x1(x1), y1(y1), 
       c0(c), c1(c), c2(c), c3(c), radius(rad), feather(fea) {}
    UiQuad(float x0, float y0, float x1, float y1, 
           const RGBA& c_x0, const RGBA& c_y0, const RGBA& c_x1, const RGBA& c_y1,
           float rad = 0.0f, float fea = 1.0f) 
     : x0(x0), y0(y0), x1(x1), y1(y1), 
       c0(c_x0), c1(c_y0), c2(c_x1), c3(c_y1), radius(rad), feather(fea) {}
};

class UiRenderer {
public:
    UiRenderer() = default;
    ~UiRenderer();

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    // Must be called after EGL context is current.
    // If you prefer to manage shaders outside, you can skip initProgram()
    // and provide your own program + uniform locations to draw().
    bool init(const Assets::Manager& am);
    void shutdown();

    // Record UI geometry for this frame
    void begin(); // clears internal vertex list
    void rectFilled(float x, float y, float w, float h, const RGBA& c, float radius = 0.0f, float feather = 1.0f);
    void rectFilledHGrad(float x, float y, float w, float h, const RGBA& lc, const RGBA& rc);
    void rectFilledVGrad(float x, float y, float w, float h, const RGBA& tc, const RGBA& bc);
    void rectFilled4Grad(float x, float y, float w, float h, const RGBA& tlc, const RGBA& trc, const RGBA& brc, const RGBA& blc);

    void rectOutline(float x, float y, float w, float h, float t, const RGBA& c);
    void line(float x0, float y0, float x1, float y1, float thickness, const RGBA& c);
    void end();   // uploads VBO (or you can upload in draw)

    // Draw all recorded UI. Requires an orthographic MVP like your text uses.
    // If you use UiRenderer’s internal program, pass program=0 and uMVP=-1 to use internal.
    void draw(const float* mvp4x4);

    struct Handle { int id = -1; };
    Handle createObj();
    void   destroyObj(Handle h);

    // Build geometry for an object (you can add more commands later)
    void objClear(Handle h);
    
    void objRectFilled(Handle hdl, float x, float y, float w, float h, const RGBA& c, float radius = 0.0f, float feather = 1.0f);
    void objRectFilledHGrad(Handle hdl, float x, float y, float w, float h, const RGBA& lc, const RGBA& rc);
    void objRectFilledVGrad(Handle hdl, float x, float y, float w, float h, const RGBA& tc, const RGBA& bc);
    void objRectFilled4Grad(Handle hdl, float x, float y, float w, float h, const RGBA& tlc, const RGBA& trc, const RGBA& brc, const RGBA& blc);
    
    void objRectOutline(Handle hdl, float x, float y, float w, float h, float t, const RGBA& c);
    void objLine(Handle hdl, float x0, float y0, float x1, float y1, float thickness, const RGBA& c);

    // Upload dirty cached objects
    void updateObjects();
    // Draw cached objects (optionally also draw immediate batch)
    void drawObjects(const float* mvp4x4);

    // Optional: use internal program (created in init()).
    GLuint program() const { return m_prog; }
    GLint  uMVP() const { return m_uMVP; }

    // Stats
    int vertexCount() const { return (int)m_frame.verts.size(); }
private:
    struct UiObj {
        bool alive = true;
        bool gpuDirty = true;

        std::vector<UiVtx> verts;
        GLsizei drawCount = 0;
        
        GLuint vbo = 0;
        GLuint vao = 0;
    };

    UiObj* get(Handle h);
    void destroyObj(UiObj& o);

    // Build geometry for an object (you can add more commands later)
    void objClear(UiObj& o);
    
    void objRectFilled(UiObj& o, float x, float y, float w, float h, const RGBA& c, float radius = 0.0f, float feather = 1.0f);
    //void objRectFilledRound(UiObj& o, float x, float y, float w, float h, const RGBA& c);
    void objRectFilledHGrad(UiObj& o, float x, float y, float w, float h, const RGBA& lc, const RGBA& rc);
    void objRectFilledVGrad(UiObj& o, float x, float y, float w, float h, const RGBA& tc, const RGBA& bc);
    void objRectFilled4Grad(UiObj& o, float x, float y, float w, float h, const RGBA& tlc, const RGBA& trc, const RGBA& brc, const RGBA& blc);
    
    void objRectOutline(UiObj& o, float x, float y, float w, float h, float t, const RGBA& c);
    void objLine(UiObj& o, float x0, float y0, float x1, float y1, float thickness, const RGBA& c);

    bool initProgram(const Assets::Manager& am);
    void destroyProgram();
    void ensureObjBuffers(UiObj& o);
    void uploadObj(UiObj& o, GLenum usage);
    void drawObj(const UiObj& o);

private:
    UiObj m_frame;
    std::vector<UiObj> m_objs;

    // Internal shader (optional)
    GLuint m_prog = 0;
    GLint  m_uMVP = -1;
};