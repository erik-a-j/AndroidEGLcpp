// ui_renderer.cpp

#include <GLES3/gl3.h>

#define TAG_NAMESPACE "UiR"
#include "logging.hpp"

#include "ui_renderer.hpp"
#include <cstring>
#include <cstddef>
#include <algorithm>

static void logShader(GLuint s, const char* label) {
    GLint len = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    if (len > 1) {
        std::vector<char> buf((size_t)len);
        glGetShaderInfoLog(s, len, nullptr, buf.data());
        logx::Ef("{} Ui shader log:\n{}", label, buf.data());
    }
}
static void logProgram(GLuint p) {
    GLint len = 0;
    glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
    if (len > 1) {
        std::vector<char> buf((size_t)len);
        glGetProgramInfoLog(p, len, nullptr, buf.data());
        logx::Ef("Ui program log:\n{}", buf.data());
    }
}


static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        logShader(s, (type == GL_VERTEX_SHADER) ? "VERT" : "FRAG");
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

    glLinkProgram(p);

    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        logProgram(p);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

UiRenderer::~UiRenderer() { shutdown(); }

bool UiRenderer::init(const Assets::Manager& am) {
    return initProgram(am);
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
bool UiRenderer::initProgram(const Assets::Manager& am) {
    std::vector<char> vs = am.read("shaders/ui.vert");
    std::vector<char> fs = am.read("shaders/ui.frag");
    if (vs.empty() || fs.empty()) {
        logx::E("failed reading ui shaders from storage");
        return false;
    }
    
    m_prog = linkProgram(reinterpret_cast<char*>(vs.data()), reinterpret_cast<char*>(fs.data()));
    if (!m_prog) {
        logx::E("failed linking program");
        return false;
    }

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
    if (!o.vao) {
        glGenVertexArrays(1, &o.vao);

        glBindVertexArray(o.vao);
        glBindBuffer(GL_ARRAY_BUFFER, o.vbo);

        glEnableVertexAttribArray(0); // aPos
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVtx),
                              (void*)offsetof(UiVtx, x));

        glEnableVertexAttribArray(1); // aColor
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVtx),
                              (void*)offsetof(UiVtx, r));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(UiVtx), (void*)offsetof(UiVtx, cx));
        
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(UiVtx), (void*)offsetof(UiVtx, hx));
        
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(UiVtx), (void*)offsetof(UiVtx, radius));
        
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(UiVtx), (void*)offsetof(UiVtx, feather));
        
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
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
    
    glBindVertexArray(o.vao);
    glDrawArrays(GL_TRIANGLES, 0, o.drawCount);
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

    glBindVertexArray(0);
}

static inline void pushTri(std::vector<UiVtx>& dst, const UiVtx& a, const UiVtx& b, const UiVtx& c) {
    dst.push_back(a); dst.push_back(b); dst.push_back(c);
}
/*static inline void pushQuad(std::vector<UiVtx>& dst, float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    UiVtx v0{ x0, y0, {r,g,b,a} };
    UiVtx v1{ x1, y0, {r,g,b,a} };
    UiVtx v2{ x1, y1, {r,g,b,a} };
    UiVtx v3{ x0, y1, {r,g,b,a} };
    pushTri(dst, v0, v1, v2);
    pushTri(dst, v0, v2, v3);
}*/
static inline void pushQuad(std::vector<UiVtx>& dst,const UiQuad& qv) {
    float x0 = std::min(qv.x0, qv.x1);
    float x1 = std::max(qv.x0, qv.x1);
    float y0 = std::min(qv.y0, qv.y1);
    float y1 = std::max(qv.y0, qv.y1);
    float cx = 0.5f*(x0 + x1);
    float cy = 0.5f*(y0 + y1);
    float hx = 0.5f*(x1 - x0);
    float hy = 0.5f*(y1 - y0);
    
    UiVtx v0{qv.x0,qv.y0,qv.c0, cx,cy,hx,hy, qv.radius, qv.feather};
    UiVtx v1{qv.x1,qv.y0,qv.c1, cx,cy,hx,hy, qv.radius, qv.feather};
    UiVtx v2{qv.x1,qv.y1,qv.c2, cx,cy,hx,hy, qv.radius, qv.feather};
    UiVtx v3{qv.x0,qv.y1,qv.c3, cx,cy,hx,hy, qv.radius, qv.feather};
    
    pushTri(dst, v0,v1,v2);
    pushTri(dst, v0,v2,v3);
}
/*static inline void pushQuad(std::vector<UiVtx>& dst, float x0,float y0,float x1,float y1,
                             const RGBA& c0, const RGBA& c1, const RGBA& c2, const RGBA& c3,
                             float radius, float feather)
{
    float cx = 0.5f*(x0+x1);
    float cy = 0.5f*(y0+y1);
    float hx = 0.5f*(x1-x0);
    float hy = 0.5f*(y1-y0);
    UiVtx v0{x0,y0,c0, cx,cy,hx,hy, radius, feather};
    UiVtx v1{x1,y0,c1, cx,cy,hx,hy, radius, feather};
    UiVtx v2{x1,y1,c2, cx,cy,hx,hy, radius, feather};
    UiVtx v3{x0,y1,c3, cx,cy,hx,hy, radius, feather};
    pushTri(dst, v0,v1,v2);
    pushTri(dst, v0,v2,v3);
}
static inline void pushQuad(std::vector<UiVtx>& dst, float x0, float y0, float x1, float y1, const RGBA& c, float radius, float feather) {
    float cx = 0.5f*(x0+x1);
    float cy = 0.5f*(y0+y1);
    float hx = 0.5f*(x1-x0);
    float hy = 0.5f*(y1-y0);
    UiVtx v0{x0,y0,c, cx,cy,hx,hy, radius,feather};
    UiVtx v1{x1,y0,c, cx,cy,hx,hy, radius,feather};
    UiVtx v2{x1,y1,c, cx,cy,hx,hy, radius,feather};
    UiVtx v3{x0,y1,c, cx,cy,hx,hy, radius,feather};
    pushTri(dst, v0, v1, v2);
    pushTri(dst, v0, v2, v3);
}
*/
void UiRenderer::destroyObj(UiObj& o) {
    if (o.vao) { glDeleteVertexArrays(1, &o.vao); o.vao = 0; }
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

void UiRenderer::objRectFilled(UiObj& o, float x, float y, float w, float h, const RGBA& c, float radius, float feather) {
    pushQuad(o.verts, {x, y, x + w, y + h, c, radius, feather});
    o.gpuDirty = true;
}
void UiRenderer::objRectFilledHGrad(UiObj& o, float x, float y, float w, float h, const RGBA& lc, const RGBA& rc) {
    pushQuad(o.verts, {x, y, x + w, y + h, lc, rc, rc, lc});
    o.gpuDirty = true;
}
void UiRenderer::objRectFilledVGrad(UiObj& o, float x, float y, float w, float h, const RGBA& tc, const RGBA& bc) {
    pushQuad(o.verts, {x, y, x + w, y + h, tc, tc, bc, bc});
    o.gpuDirty = true;
}
void UiRenderer::objRectFilled4Grad(UiObj& o, float x, float y, float w, float h, const RGBA& tlc, const RGBA& trc, const RGBA& brc, const RGBA& blc) {
    pushQuad(o.verts, {x, y, x + w, y + h, tlc, trc, brc, blc});
    o.gpuDirty = true;
}
    
void UiRenderer::objRectOutline(UiObj& o, float x, float y, float w, float h, float t, const RGBA& c) {
    // top
    objRectFilled(o, x, y, w, t, c);
    // bottom
    objRectFilled(o, x, y + h - t, w, t, c);
    // left
    objRectFilled(o, x, y + t, t, h - 2*t, c);
    // right
    objRectFilled(o, x + w - t, y + t, t, h - 2*t, c);
}
void UiRenderer::objLine(UiObj& o, float x0, float y0, float x1, float y1, float thickness, const RGBA& c) {
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

void UiRenderer::objRectFilled(Handle hdl, float x, float y, float w, float h, const RGBA& c, float radius, float feather) {
    UiObj* o = get(hdl);
    if (o) objRectFilled(*o, x, y, w, h, c, radius, feather);
}
void UiRenderer::objRectFilledHGrad(Handle hdl, float x, float y, float w, float h, const RGBA& lc, const RGBA& rc) {
    UiObj* o = get(hdl);
    if (o) objRectFilledHGrad(*o, x, y, w, h, lc, rc);
}
void UiRenderer::objRectFilledVGrad(Handle hdl, float x, float y, float w, float h, const RGBA& tc, const RGBA& bc) {
    UiObj* o = get(hdl);
    if (o) objRectFilledVGrad(*o, x, y, w, h, tc, bc);
}
void UiRenderer::objRectFilled4Grad(Handle hdl, float x, float y, float w, float h, const RGBA& tlc, const RGBA& trc, const RGBA& brc, const RGBA& blc) {
    UiObj* o = get(hdl);
    if (o) objRectFilled4Grad(*o, x, y, w, h, tlc, trc, brc, blc);
}

void UiRenderer::objRectOutline(Handle hdl, float x, float y, float w, float h, float t, const RGBA& c) {
    UiObj* o = get(hdl);
    if (o) objRectOutline(*o, x, y, w, h, t, c);
}
void UiRenderer::objLine(Handle hdl, float x0, float y0, float x1, float y1, float thickness, const RGBA& c) {
    UiObj* o = get(hdl);
    if (o) objLine(*o, x0, y0, x1, y1, thickness, c);
}

void UiRenderer::rectFilled(float x, float y, float w, float h, const RGBA& c, float radius, float feather) {
    objRectFilled(m_frame, x, y, w, h, c, radius, feather);
}
void UiRenderer::rectFilledHGrad(float x, float y, float w, float h, const RGBA& lc, const RGBA& rc) {
    objRectFilledHGrad(m_frame, x, y, w, h, lc, rc);
}
void UiRenderer::rectFilledVGrad(float x, float y, float w, float h, const RGBA& tc, const RGBA& bc) {
    objRectFilledVGrad(m_frame, x, y, w, h, tc, bc);
}
void UiRenderer::rectFilled4Grad(float x, float y, float w, float h, const RGBA& tlc, const RGBA& trc, const RGBA& brc, const RGBA& blc) {
    objRectFilled4Grad(m_frame, x, y, w, h, tlc, trc, brc, blc);
}


void UiRenderer::rectOutline(float x, float y, float w, float h, float t, const RGBA& c) {
    objRectOutline(m_frame, x, y, w, h, t, c);
}
void UiRenderer::line(float x0, float y0, float x1, float y1, float thickness, const RGBA& c) {
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

    glBindVertexArray(0);
}

