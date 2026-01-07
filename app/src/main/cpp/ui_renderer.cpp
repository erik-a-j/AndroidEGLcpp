// ui_renderer.cpp

#include "ui_renderer.hpp"

#include <bit>
#include <cstring>
#include <cstddef>
#include <algorithm>
#include <stdexcept>

#include "logging.hpp"
static constexpr char NS[] = "UiR";
using logx = logger::logx<NS>;

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

static inline void chooseDims(int texels, int maxSize, int& w, int& h) {
    // start with something cache-friendly; grow if needed
    w = std::min(maxSize, 1024);
    if (w < 1) w = 1;

    h = (texels + w - 1) / w;

    // if too tall, increase width up to maxSize
    if (h > maxSize) {
        w = maxSize;
        h = (texels + w - 1) / w;
    }
}
static void ensureTex(GLuint& t) { if (!t) glGenTextures(1, &t); }
static void destroyTex(GLuint& t) { if (t) { glDeleteTextures(1, &t); t = 0; } }
static void uploadInstF(const std::vector<UiRectInst>& inst,
                        GLuint& texF, int& wF, int& hF,
                        GLenum internalFmt)
{
    const int texels = (int)inst.size() * 2;
    if (texels <= 0) { wF = hF = 0; return; }

    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    chooseDims(texels, maxTex, wF, hF);

    std::vector<float> buf((size_t)wF * (size_t)hF * 4, 0.0f);

    for (int i = 0; i < (int)inst.size(); ++i) {
        const UiRectInst& in = inst[(size_t)i];
        const int base = i * 2;

        int t0 = base + 0;
        int x0 = t0 % wF, y0 = t0 / wF;
        float* p0 = &buf[((size_t)y0 * (size_t)wF + (size_t)x0) * 4];
        p0[0]=in.cx; p0[1]=in.cy; p0[2]=in.hx; p0[3]=in.hy;

        int t1 = base + 1;
        int x1 = t1 % wF, y1 = t1 / wF;
        float* p1 = &buf[((size_t)y1 * (size_t)wF + (size_t)x1) * 4];
        p1[0]=in.radius; p1[1]=in.feather; p1[2]=0.f; p1[3]=0.f;
    }

    if (!texF) glGenTextures(1, &texF);
    glBindTexture(GL_TEXTURE_2D, texF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internalFmt, wF, hF, 0, GL_RGBA, GL_FLOAT, buf.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}
static void uploadInstU(const std::vector<UiRectInst>& inst,
                        GLuint& texU, int& wU, int& hU)
{
    const int texels = (int)inst.size() * 4;
    if (texels <= 0) { wU = hU = 0; return; }

    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    chooseDims(texels, maxTex, wU, hU);

    std::vector<uint8_t> buf((size_t)wU * (size_t)hU * 4, 0);

    auto writeRGBA = [&](int texelIndex, uint32_t packedRGBA) {
        int x = texelIndex % wU;
        int y = texelIndex / wU;
        uint8_t* p = &buf[((size_t)y * (size_t)wU + (size_t)x) * 4];
        p[0] = (uint8_t)((packedRGBA >> 0)  & 255u);
        p[1] = (uint8_t)((packedRGBA >> 8)  & 255u);
        p[2] = (uint8_t)((packedRGBA >> 16) & 255u);
        p[3] = (uint8_t)((packedRGBA >> 24) & 255u);
    };

    for (int i = 0; i < (int)inst.size(); ++i) {
        const UiRectInst& in = inst[(size_t)i];
        const int base = i * 4;
        writeRGBA(base + 0, in.tl);
        writeRGBA(base + 1, in.tr);
        writeRGBA(base + 2, in.br);
        writeRGBA(base + 3, in.bl);
    }

    if (!texU) glGenTextures(1, &texU);
    glBindTexture(GL_TEXTURE_2D, texU);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, wU, hU, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, buf.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}
static inline void pushRectInst(std::vector<UiRectInst>& dst, const UiQuad& qv) {
    float x0 = std::min(qv.x0, qv.x1);
    float x1 = std::max(qv.x0, qv.x1);
    float y0 = std::min(qv.y0, qv.y1);
    float y1 = std::max(qv.y0, qv.y1);

    const float cx = 0.5f*(x0 + x1);
    const float cy = 0.5f*(y0 + y1);
    const float hx = 0.5f*(x1 - x0);
    const float hy = 0.5f*(y1 - y0);

    UiRectInst inst{};
    inst.cx = cx; inst.cy = cy;
    inst.hx = hx; inst.hy = hy;
    inst.radius  = qv.radius;
    inst.feather = qv.feather;
    
    inst.tl = qv.cc.tl.pack();
    inst.tr = qv.cc.tr.pack();
    inst.br = qv.cc.br.pack();
    inst.bl = qv.cc.bl.pack();

    dst.push_back(inst);
}


UiRenderer::~UiRenderer() { shutdown(); }

bool UiRenderer::init(const Assets::Manager& am) {
    return initProgram(am);
}
void UiRenderer::shutdown() {
    if (m_quadVao) { glDeleteVertexArrays(1, &m_quadVao); m_quadVao = 0; }
    if (m_quadVbo) { glDeleteBuffers(1, &m_quadVbo); m_quadVbo = 0; }
    if (m_quadEbo) { glDeleteBuffers(1, &m_quadEbo); m_quadEbo = 0; }
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

    m_prog = linkProgram(reinterpret_cast<char*>(vs.data()),
                         reinterpret_cast<char*>(fs.data()));
    if (!m_prog) {
        logx::E("failed linking program");
        return false;
    }

    m_uMVP     = glGetUniformLocation(m_prog, "uMVP");
    m_uInstF   = glGetUniformLocation(m_prog, "uInstF");
    m_uInstU   = glGetUniformLocation(m_prog, "uInstU");
    m_uInstF_W = glGetUniformLocation(m_prog, "uInstF_W");
    m_uInstU_W = glGetUniformLocation(m_prog, "uInstU_W");

    if (m_uMVP < 0) return false;

    // --- unit quad geometry ---
    static constexpr float kQuadCorners[8] = {
        -1.f, -1.f,
         1.f, -1.f,
         1.f,  1.f,
        -1.f,  1.f
    };
    static constexpr uint16_t kQuadIdx[6] = { 0, 1, 2, 0, 2, 3 };

    glGenVertexArrays(1, &m_quadVao);
    glBindVertexArray(m_quadVao);

    glGenBuffers(1, &m_quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadCorners), kQuadCorners, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // aCorner
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glGenBuffers(1, &m_quadEbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIdx), kQuadIdx, GL_STATIC_DRAW);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // NOTE: do NOT unbind GL_ELEMENT_ARRAY_BUFFER while VAO is bound
    // (it is VAO state).

    return true;
}
void UiRenderer::destroyProgram() {
    if (m_prog) {
        glDeleteProgram(m_prog);
        m_prog = 0;
    }
    m_uMVP = -1;
    m_uInstF = m_uInstU = m_uInstF_W = m_uInstU_W = -1;
}

void UiRenderer::uploadObj(UiObj& o, GLenum /*usage*/) {
    if (!o.gpuDirty) return;
    o.gpuDirty = false;

    o.instanceCount = (GLsizei)o.inst.size();
    if (!o.instanceCount) return;

    // Prefer GL_RGBA16F for broad ES3 support
    uploadInstF(o.inst, o.texF, o.wF, o.hF, GL_RGBA16F);
    uploadInstU(o.inst, o.texU, o.wU, o.hU);
}
void UiRenderer::drawObj(const UiObj& o) {
    if (!o.alive) return;
    if (!o.instanceCount) return;

    glBindVertexArray(m_quadVao);

    // bind float instance texture to unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, o.texF);

    // bind uint instance texture to unit 1
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, o.texU);

    glUniform1i(m_uInstF, 0);
    glUniform1i(m_uInstU, 1);
    glUniform1i(m_uInstF_W, o.wF);
    glUniform1i(m_uInstU_W, o.wU);

    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void*)0, o.instanceCount);

    // optional hygiene
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
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

void UiRenderer::destroyObj(UiObj& o) {
    destroyTex(o.texF);
    destroyTex(o.texU);
    o.inst.clear();
    o.instanceCount = 0;
    o.alive = false;
}
void UiRenderer::objClear(UiObj& o) {
    o.inst.clear();
    o.instanceCount = 0;
    o.gpuDirty = true;
}
void UiRenderer::objRectFilled(UiObj& o, float x, float y, float w, float h, const UiColors& cc, float radius, float feather) {
    pushRectInst(o.inst, UiQuad{x, y, x + w, y + h, cc, radius, feather});
    o.gpuDirty = true;
}
void UiRenderer::objRectOutline(UiObj& o, float x, float y, float w, float h, float t, const UiColors& cc) {
    // top
    objRectFilled(o, x, y, w, t, cc);
    // bottom
    objRectFilled(o, x, y + h - t, w, t, cc);
    // left
    objRectFilled(o, x, y + t, t, h - 2*t, cc);
    // right
    objRectFilled(o, x + w - t, y + t, t, h - 2*t, cc);
}
void UiRenderer::objLine(UiObj& o, float x0, float y0, float x1, float y1, float thickness, const UiColors& cc) {
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
        objRectFilled(o, minx - thickness * 0.5f, miny, thickness, hh, cc);
    } else if (hh < 1e-4f) {
        // horizontal
        objRectFilled(o, minx, miny - thickness * 0.5f, w, thickness, cc);
    } else {
        // diagonal fallback (not ideal)
        objRectFilled(o, minx, miny, w, hh, cc);
    }
}

void UiRenderer::objSetUiColors(UiObj& o, UiO opts, const UiColors& cc) {
    using namespace bitmask;
    
    const auto mask = opts & UiO::Color;
    if (to_underlying(mask) == 0) return;

    const auto pcc = cc.pack(); // {tl,tr,br,bl} packed uint32_t

    const bool tl = has(mask, UiO::ColorTL);
    const bool tr = has(mask, UiO::ColorTR);
    const bool br = has(mask, UiO::ColorBR);
    const bool bl = has(mask, UiO::ColorBL);

    for (auto& inst : o.inst) {
        if (tl) inst.tl = pcc.tl;
        if (tr) inst.tr = pcc.tr;
        if (br) inst.br = pcc.br;
        if (bl) inst.bl = pcc.bl;
    }
    o.gpuDirty = true;
}
void UiRenderer::objRectOpts(UiObj& o, UiO opts, optarg_t arg) {
    using namespace bitmask;
    std::visit([&](auto&& a) {
        using A = std::decay_t<decltype(a)>;

        if constexpr (std::is_same_v<A, std::monostate>) {
            return;
        } else if constexpr (std::is_same_v<A, RGBA>) {
            objSetUiColors(o, opts, UiColors{a});
        } else if constexpr (std::is_same_v<A, UiColors>) {
            objSetUiColors(o, opts, a);
        }
    }, arg);
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
            return Handle{i};
        }
    }

    m_objs.emplace_back();
    UiObj& o = m_objs.back();
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
void UiRenderer::objRectFilled(Handle hdl, float x, float y, float w, float h, const UiColors& cc, float radius, float feather) {
    UiObj* o = get(hdl);
    if (o) objRectFilled(*o, x, y, w, h, cc, radius, feather);
}
void UiRenderer::objRectOutline(Handle hdl, float x, float y, float w, float h, float t, const UiColors& cc) {
    UiObj* o = get(hdl);
    if (o) objRectOutline(*o, x, y, w, h, t, cc);
}
void UiRenderer::objLine(Handle hdl, float x0, float y0, float x1, float y1, float thickness, const UiColors& cc) {
    UiObj* o = get(hdl);
    if (o) objLine(*o, x0, y0, x1, y1, thickness, cc);
}

void UiRenderer::objRectOpts(Handle h, UiO opts, optarg_t arg) {
    UiObj* o = get(h);
    if (o) objRectOpts(*o, opts, arg);
}

void UiRenderer::rectFilled(float x, float y, float w, float h, const UiColors& cc, float radius, float feather) {
    objRectFilled(m_frame, x, y, w, h, cc, radius, feather);
}
void UiRenderer::rectOutline(float x, float y, float w, float h, float t, const UiColors& cc) {
    objRectOutline(m_frame, x, y, w, h, t, cc);
}
void UiRenderer::line(float x0, float y0, float x1, float y1, float thickness, const UiColors& cc) {
    objLine(m_frame, x0, y0, x1, y1, thickness, cc);
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
