// ui_renderer.cpp

#include "config.h"
#if GLES_VERSION == 3
#include <GLES3/gl3.h>
#else
#include <GLES2/gl2.h>
#endif

#include "ui_renderer.hpp"
#include <cstring>
#include <cstddef>

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(s);
        return 0;
    }
    return s;
}
static GLuint linkProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;

    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);

#if GLES_VERSION != 3
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aColor");
#endif

    glLinkProgram(p);

    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

UiRenderer::~UiRenderer() { shutdown(); }

bool UiRenderer::init() {
    return initProgram();
}
void UiRenderer::shutdown() {
    for (auto& o : m_objs) {
        if (o.alive) destroyObj(o);
    }
    m_objs.clear();
    destroyObj(m_frame);
    m_frame = UiObj{};
    destroyProgram();
}
bool UiRenderer::initProgram() {
#if GLES_VERSION == 3
    const char* vs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform mat4 uMVP;\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec4 aColor;\n"
        "out vec4 vColor;\n"
        "void main(){\n"
        "  vColor = aColor;\n"
        "  gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    const char* fs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec4 vColor;\n"
        "out vec4 fragColor;\n"
        "void main(){ fragColor = vColor; }\n";
#else
    const char* vs =
        "uniform mat4 uMVP;\n"
        "attribute vec2 aPos;\n"
        "attribute vec4 aColor;\n"
        "varying vec4 vColor;\n"
        "void main(){\n"
        "  vColor = aColor;\n"
        "  gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    const char* fs =
        "precision mediump float;\n"
        "varying vec4 vColor;\n"
        "void main(){ gl_FragColor = vColor; }\n";
#endif

    m_prog = linkProgram(vs, fs);
    if (!m_prog) return false;

    m_uMVP = glGetUniformLocation(m_prog, "uMVP");
    return (m_uMVP >= 0);
}
void UiRenderer::destroyProgram() {
    if (m_prog) {
        glDeleteProgram(m_prog);
        m_prog = 0;
    }
    m_uMVP = -1;
}

void UiRenderer::ensureObjBuffers(UiObj& o) {
    if (!o.vbo) glGenBuffers(1, &o.vbo);
#if GLES_VERSION == 3
    if (!o.vao) {
        glGenVertexArrays(1, &o.vao);

        glBindVertexArray(o.vao);
        glBindBuffer(GL_ARRAY_BUFFER, o.vbo);

        glEnableVertexAttribArray(0); // aPos
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVtx),
                              (void*)offsetof(UiVtx, x));

        glEnableVertexAttribArray(1); // aColor
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(UiVtx),
                              (void*)offsetof(UiVtx, r));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
#endif
}
void UiRenderer::uploadObj(UiObj& o, GLenum usage) {
    if (!o.gpuDirty) return;
    o.gpuDirty = false;

    ensureObjBuffers(o);

    glBindBuffer(GL_ARRAY_BUFFER, o.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(o.verts.size() * sizeof(UiVtx)),
                 o.verts.empty() ? nullptr : o.verts.data(),
                 usage);
    o.drawCount = (GLsizei)o.verts.size();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void UiRenderer::drawObj(const UiObj& o) {
    if (!o.alive) return;
    if (!o.drawCount) return;

#if GLES_VERSION == 3
    glBindVertexArray(o.vao);
    glDrawArrays(GL_TRIANGLES, 0, o.drawCount);
#else
    glBindBuffer(GL_ARRAY_BUFFER, o.vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVtx),
                          (void*)offsetof(UiVtx, x));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(UiVtx),
                          (void*)offsetof(UiVtx, r));
    glDrawArrays(GL_TRIANGLES, 0, o.drawCount);
#endif
}
void UiRenderer::updateObjects() {
    for (auto& o : m_objs) {
        if (!o.alive) continue;
        uploadObj(o, GL_STATIC_DRAW);
    }
}
void UiRenderer::drawObjects(const float* mvp4x4) {
    if (!m_prog || m_uMVP < 0) return;

    // make sure dirty objects are uploaded
    updateObjects();

    glUseProgram(m_prog);
    glUniformMatrix4fv(m_uMVP, 1, GL_FALSE, mvp4x4);

    for (const auto& o : m_objs) drawObj(o);

#if GLES_VERSION == 3
    glBindVertexArray(0);
#endif
}

static inline void pushTri(std::vector<UiVtx>& dst, const UiVtx& a, const UiVtx& b, const UiVtx& c) {
    dst.push_back(a); dst.push_back(b); dst.push_back(c);
}
static inline void pushQuad(std::vector<UiVtx>& dst, float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    UiVtx v0{ x0, y0, {r,g,b,a} };
    UiVtx v1{ x1, y0, {r,g,b,a} };
    UiVtx v2{ x1, y1, {r,g,b,a} };
    UiVtx v3{ x0, y1, {r,g,b,a} };
    pushTri(dst, v0, v1, v2);
    pushTri(dst, v0, v2, v3);
}
static inline void pushQuad(std::vector<UiVtx>& dst, float x0, float y0, float x1, float y1, const UiColor& c) {
    UiVtx v0{ x0, y0, c};
    UiVtx v1{ x1, y0, c};
    UiVtx v2{ x1, y1, c};
    UiVtx v3{ x0, y1, c};
    pushTri(dst, v0, v1, v2);
    pushTri(dst, v0, v2, v3);
}
static inline void pushQuad4(std::vector<UiVtx>& dst, float x0,float y0,float x1,float y1,
                             const UiColor& c0, const UiColor& c1, const UiColor& c2, const UiColor& c3)
{
    // v0..v3 already have colors set; only override positions here (or construct them outside)
    UiVtx a{x0, y0, c0};
    UiVtx b{x1, y0, c1};
    UiVtx c{x1, y1, c2};
    UiVtx d{x0, y1, c3};
    pushTri(dst, a,b,c);
    pushTri(dst, a,c,d);
}

void UiRenderer::destroyObj(UiObj& o) {
#if GLES_VERSION == 3
    if (o.vao) { glDeleteVertexArrays(1, &o.vao); o.vao = 0; }
#endif
    if (o.vbo) { glDeleteBuffers(1, &o.vbo); o.vbo = 0; }

    o.verts.clear();
    o.drawCount = 0;
    o.alive = false;
}
void UiRenderer::objClear(UiObj& o) {
    o.verts.clear();
    o.drawCount = 0;
    o.gpuDirty = true; // will upload empty
}

void UiRenderer::objRectFilled(UiObj& o, float x, float y, float w, float h, const UiColor& c) {
    pushQuad(o.verts, x, y, x + w, y + h, c);
    o.gpuDirty = true;
}
void UiRenderer::objRectFilledHGrad(UiObj& o, float x, float y, float w, float h, const UiColor& lc, const UiColor& rc) {
    pushQuad4(o.verts, x, y, x + w, y + h, lc, rc, rc, lc);
    o.gpuDirty = true;
}
void UiRenderer::objRectFilledVGrad(UiObj& o, float x, float y, float w, float h, const UiColor& tc, const UiColor& bc) {
    pushQuad4(o.verts, x, y, x + w, y + h, tc, tc, bc, bc);
    o.gpuDirty = true;
}
void UiRenderer::objRectFilled4Grad(UiObj& o, float x, float y, float w, float h, const UiColor& tlc, const UiColor& trc, const UiColor& brc, const UiColor& blc) {
    pushQuad4(o.verts, x, y, x + w, y + h, tlc, trc, brc, blc);
    o.gpuDirty = true;
}
    
void UiRenderer::objRectOutline(UiObj& o, float x, float y, float w, float h, float t, const UiColor& c) {
    // top
    objRectFilled(o, x, y, w, t, c);
    // bottom
    objRectFilled(o, x, y + h - t, w, t, c);
    // left
    objRectFilled(o, x, y + t, t, h - 2*t, c);
    // right
    objRectFilled(o, x + w - t, y + t, t, h - 2*t, c);
}
void UiRenderer::objLine(UiObj& o, float x0, float y0, float x1, float y1, float thickness, const UiColor& c) {
    // Represent as a rectangle oriented along the line.
    // Minimal implementation: axis-aligned only (if you want full rotated lines, you need a little math).
    // Here: if not axis-aligned, fall back to bounding-box thickness.
    float minx = (x0 < x1) ? x0 : x1;
    float maxx = (x0 > x1) ? x0 : x1;
    float miny = (y0 < y1) ? y0 : y1;
    float maxy = (y0 > y1) ? y0 : y1;

    float w = (maxx - minx);
    float hh = (maxy - miny);

    if (w < 1e-4f) {
        // vertical
        objRectFilled(o, minx - thickness * 0.5f, miny, thickness, hh, c);
    } else if (hh < 1e-4f) {
        // horizontal
        objRectFilled(o, minx, miny - thickness * 0.5f, w, thickness, c);
    } else {
        // diagonal fallback (not ideal)
        objRectFilled(o, minx, miny, w, hh, c);
    }
}

UiRenderer::UiObj* UiRenderer::get(Handle h) {
    if (h.id < 0 || h.id >= (int)m_objs.size()) return nullptr;
    if (!m_objs[h.id].alive) return nullptr;
    return &m_objs[h.id];
}
UiRenderer::Handle UiRenderer::createObj() {
    // reuse dead slots
    for (int i = 0; i < (int)m_objs.size(); i++) {
        if (!m_objs[i].alive) {
            m_objs[i] = UiObj{};
            m_objs[i].alive = true;
            m_objs[i].gpuDirty = true;
            ensureObjBuffers(m_objs[i]);
            return Handle{i};
        }
    }

    m_objs.emplace_back();
    UiObj& o = m_objs.back();
    ensureObjBuffers(o);
    return Handle{(int)m_objs.size() - 1};
}
void UiRenderer::destroyObj(Handle h) {
    UiObj* o = get(h);
    if (o) destroyObj(*o);
}
void UiRenderer::objClear(Handle h) { 
    UiObj* o = get(h);
    if (o) objClear(*o);
}

void UiRenderer::objRectFilled(Handle hdl, float x, float y, float w, float h, const UiColor& c) {
    UiObj* o = get(hdl);
    if (o) objRectFilled(*o, x, y, w, h, c);
}
void UiRenderer::objRectFilledHGrad(Handle hdl, float x, float y, float w, float h, const UiColor& lc, const UiColor& rc) {
    UiObj* o = get(hdl);
    if (o) objRectFilledHGrad(*o, x, y, w, h, lc, rc);
}
void UiRenderer::objRectFilledVGrad(Handle hdl, float x, float y, float w, float h, const UiColor& tc, const UiColor& bc) {
    UiObj* o = get(hdl);
    if (o) objRectFilledVGrad(*o, x, y, w, h, tc, bc);
}
void UiRenderer::objRectFilled4Grad(Handle hdl, float x, float y, float w, float h, const UiColor& tlc, const UiColor& trc, const UiColor& brc, const UiColor& blc) {
    UiObj* o = get(hdl);
    if (o) objRectFilled4Grad(*o, x, y, w, h, tlc, trc, brc, blc);
}

void UiRenderer::objRectOutline(Handle hdl, float x, float y, float w, float h, float t, const UiColor& c) {
    UiObj* o = get(hdl);
    if (o) objRectOutline(*o, x, y, w, h, t, c);
}
void UiRenderer::objLine(Handle hdl, float x0, float y0, float x1, float y1, float thickness, const UiColor& c) {
    UiObj* o = get(hdl);
    if (o) objLine(*o, x0, y0, x1, y1, thickness, c);
}

void UiRenderer::rectFilled(float x, float y, float w, float h, const UiColor& c) {
    objRectFilled(m_frame, x, y, w, h, c);
}
void UiRenderer::rectFilledHGrad(float x, float y, float w, float h, const UiColor& lc, const UiColor& rc) {
    objRectFilledHGrad(m_frame, x, y, w, h, lc, rc);
}
void UiRenderer::rectFilledVGrad(float x, float y, float w, float h, const UiColor& tc, const UiColor& bc) {
    objRectFilledVGrad(m_frame, x, y, w, h, tc, bc);
}
void UiRenderer::rectFilled4Grad(float x, float y, float w, float h, const UiColor& tlc, const UiColor& trc, const UiColor& brc, const UiColor& blc) {
    objRectFilled4Grad(m_frame, x, y, w, h, tlc, trc, brc, blc);
}


void UiRenderer::rectOutline(float x, float y, float w, float h, float t, const UiColor& c) {
    objRectOutline(m_frame, x, y, w, h, t, c);
}
void UiRenderer::line(float x0, float y0, float x1, float y1, float thickness, const UiColor& c) {
    objLine(m_frame, x0, y0, x1, y1, thickness, c);
}
void UiRenderer::begin() {
    objClear(m_frame);
}
void UiRenderer::end() {
    uploadObj(m_frame, GL_DYNAMIC_DRAW);
}
void UiRenderer::draw(const float* mvp4x4) {
   if (!m_prog || m_uMVP < 0) return;

    // If you allow calling draw() without end()
    uploadObj(m_frame, GL_DYNAMIC_DRAW);

    glUseProgram(m_prog);
    glUniformMatrix4fv(m_uMVP, 1, GL_FALSE, mvp4x4);

    drawObj(m_frame);

#if GLES_VERSION == 3
    glBindVertexArray(0);
#endif
}

