// ui_renderer.hpp
#pragma once

#include "config.h"
#if GLES_VERSION == 3
#include <GLES3/gl3.h>
#else
#include <GLES2/gl2.h>
#endif

#include <cstdint>
#include <vector>

struct UiColor {
    float r, g, b, a;
};
struct UiVtx {
    float x, y;      // screen-space (pixels)
    float r, g, b, a; // per-vertex color
    
    UiVtx() = default;
    UiVtx(float x, float y, const UiColor& c)
     : x(x), y(y), r(c.r), g(c.g), b(c.b), a(c.a) {}
    UiVtx(const UiColor& c)
     : x(0), y(0), r(c.r), g(c.g), b(c.b), a(c.a) {} 
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
    bool init();
    void shutdown();

    // Record UI geometry for this frame
    void begin(); // clears internal vertex list
    void rectFilled(float x, float y, float w, float h, const UiColor& c);
    void rectFilledHGrad(float x, float y, float w, float h, const UiColor& lc, const UiColor& rc);
    void rectFilledVGrad(float x, float y, float w, float h, const UiColor& tc, const UiColor& bc);
    void rectFilled4Grad(float x, float y, float w, float h, const UiColor& tlc, const UiColor& trc, const UiColor& brc, const UiColor& blc);

    void rectOutline(float x, float y, float w, float h, float t, const UiColor& c);
    void line(float x0, float y0, float x1, float y1, float thickness, const UiColor& c);
    void end();   // uploads VBO (or you can upload in draw)

    // Draw all recorded UI. Requires an orthographic MVP like your text uses.
    // If you use UiRenderer’s internal program, pass program=0 and uMVP=-1 to use internal.
    void draw(const float* mvp4x4);

    struct Handle { int id = -1; };
    Handle createObj();
    void   destroyObj(Handle h);

    // Build geometry for an object (you can add more commands later)
    void objClear(Handle h);
    
    void objRectFilled(Handle hdl, float x, float y, float w, float h, const UiColor& c);
    void objRectFilledHGrad(Handle hdl, float x, float y, float w, float h, const UiColor& lc, const UiColor& rc);
    void objRectFilledVGrad(Handle hdl, float x, float y, float w, float h, const UiColor& tc, const UiColor& bc);
    void objRectFilled4Grad(Handle hdl, float x, float y, float w, float h, const UiColor& tlc, const UiColor& trc, const UiColor& brc, const UiColor& blc);
    
    void objRectOutline(Handle hdl, float x, float y, float w, float h, float t, const UiColor& c);
    void objLine(Handle hdl, float x0, float y0, float x1, float y1, float thickness, const UiColor& c);

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
#if GLES_VERSION == 3
        GLuint vao = 0;
#endif
    };

    UiObj* get(Handle h);
    void destroyObj(UiObj& o);

    // Build geometry for an object (you can add more commands later)
    void objClear(UiObj& o);
    
    void objRectFilled(UiObj& o, float x, float y, float w, float h, const UiColor& c);
    void objRectFilledHGrad(UiObj& o, float x, float y, float w, float h, const UiColor& lc, const UiColor& rc);
    void objRectFilledVGrad(UiObj& o, float x, float y, float w, float h, const UiColor& tc, const UiColor& bc);
    void objRectFilled4Grad(UiObj& o, float x, float y, float w, float h, const UiColor& tlc, const UiColor& trc, const UiColor& brc, const UiColor& blc);
    
    void objRectOutline(UiObj& o, float x, float y, float w, float h, float t, const UiColor& c);
    void objLine(UiObj& o, float x0, float y0, float x1, float y1, float thickness, const UiColor& c);

    bool initProgram();
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