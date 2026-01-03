// text_system.cpp (updated to own shader program like UiRenderer)
#include "config.h"
#if GLES_VERSION == 3
#include <GLES3/gl3.h>
#else
#include <GLES2/gl2.h>
#endif

#include "text_renderer.hpp"
#include <cstring>
#include <cstddef>
#include <algorithm>

#if GLES_VERSION == 3
static void setupTextVao(GLuint vao, GLuint vbo) {
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glEnableVertexAttribArray(0); // aPos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(TextVtx), (void*)offsetof(TextVtx, x));

    glEnableVertexAttribArray(1); // aUV
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(TextVtx), (void*)offsetof(TextVtx, u));
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}
#endif

TextRenderer::~TextRenderer() { shutdown(); }

bool TextRenderer::init(const char* fontFilePath, int pixelSize, int atlasW, int atlasH) {
    shutdown();

    // 1) shader program
    if (!initProgram()) return false;

    // 2) font + atlas
    if (!initFont(fontFilePath, pixelSize)) { destroyProgram(); return false; }
    if (!initAtlas(atlasW, atlasH)) { destroyFont(); destroyProgram(); return false; }

    std::memset(m_glyphs, 0, sizeof(m_glyphs));
    return true;
}

void TextRenderer::shutdown() {
    // Text VBOs
    for (auto& t : m_items) {
#if GLES_VERSION == 3
        if (t.vao) glDeleteVertexArrays(1, &t.vao);
        t.vao = 0;
#endif
        if (t.vbo) glDeleteBuffers(1, &t.vbo);
        t.vbo = 0;
    }
    m_items.clear();

    // Atlas + font
    destroyAtlas();
    destroyFont();
    std::memset(m_glyphs, 0, sizeof(m_glyphs));

    // Program last (safe either way, but keep consistent)
    destroyProgram();
}

/* ---------------- Program ---------------- */

GLuint TextRenderer::compileShader(GLenum type, const char* src) {
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

GLuint TextRenderer::linkProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);

    #if GLES_VERSION != 3
    // match your vertex format
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aUV");
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

bool TextRenderer::initProgram() {
    // Same shader pair you used in main.cpp
    #if GLES_VERSION == 3
    const char* vs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform mat4 uMVP;\n"
        "uniform vec2 uTranslate;\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
            "vUV = aUV;\n"
            "vec2 p = aPos + uTranslate;\n"
            "gl_Position = uMVP * vec4(p, 0.0, 1.0);\n"
        "}\n";
    const char* fs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler2D uTex;\n"
        "uniform vec4 uColor;\n"
        "in vec2 vUV;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
            "float a = texture(uTex, vUV).r;\n"
            "fragColor = vec4(uColor.rgb, uColor.a * a);\n"
        "}\n";
    #else
    const char* vs =
        "uniform mat4 uMVP;\n"
        "uniform vec2 uTranslate;\n"
        "attribute vec2 aPos;\n"
        "attribute vec2 aUV;\n"
        "varying vec2 vUV;\n"
        "void main(){\n"
        "  vUV = aUV;\n"
        "  vec2 p = aPos + uTranslate;\n"
        "  gl_Position = uMVP * vec4(p,0.0,1.0);\n"
        "}\n";
    const char* fs =
        "precision mediump float;\n"
        "uniform sampler2D uTex;\n"
        "uniform vec4 uColor;\n"
        "varying vec2 vUV;\n"
        "void main(){\n"
        "  float a = texture2D(uTex, vUV).a;\n"
        "  gl_FragColor = vec4(uColor.rgb, uColor.a * a);\n"
        "}\n";
    #endif

    m_prog = linkProgram(vs, fs);
    if (!m_prog) return false;

    m_uMVP       = glGetUniformLocation(m_prog, "uMVP");
    m_uTex       = glGetUniformLocation(m_prog, "uTex");
    m_uColor     = glGetUniformLocation(m_prog, "uColor");
    m_uTranslate = glGetUniformLocation(m_prog, "uTranslate");

    return true;
}

void TextRenderer::destroyProgram() {
    if (m_prog) glDeleteProgram(m_prog);
    m_prog = 0;
    m_uMVP = m_uTex = m_uColor = m_uTranslate = -1;
}

/* ---------------- Font (FreeType + HarfBuzz) ---------------- */

bool TextRenderer::initFont(const char* fontFilePath, int pixelSize) {
    m_pxSize = pixelSize;
    if (FT_Init_FreeType(&m_ft) != 0) return false;
    if (FT_New_Face(m_ft, fontFilePath, 0, &m_face) != 0) return false;
    if (FT_Set_Char_Size(m_face, 0, pixelSize * 64, 96, 96) != 0) return false;

    m_hbFont = hb_ft_font_create_referenced(m_face);
    if (!m_hbFont) return false;

    hb_font_set_scale(m_hbFont,
                      (int)m_face->size->metrics.x_ppem * 64,
                      (int)m_face->size->metrics.y_ppem * 64);
    return true;
}

void TextRenderer::destroyFont() {
    if (m_hbFont) hb_font_destroy(m_hbFont);
    if (m_face) FT_Done_Face(m_face);
    if (m_ft) FT_Done_FreeType(m_ft);
    m_hbFont = nullptr;
    m_face = nullptr;
    m_ft = nullptr;
    m_pxSize = 0;
}

/* ---------------- Atlas ---------------- */

bool TextRenderer::initAtlas(int w, int h) {
    m_atlasW = w; m_atlasH = h;
    m_atlasPixels.assign((size_t)w * (size_t)h, 0);
    m_penX = m_penY = m_rowH = 0;
    m_atlasTex = 0;
    m_atlasUploaded = false;
    return true;
}

void TextRenderer::destroyAtlas() {
    if (m_atlasTex) glDeleteTextures(1, &m_atlasTex);
    m_atlasTex = 0;
    m_atlasPixels.clear();
    m_atlasW = m_atlasH = 0;
    m_penX = m_penY = m_rowH = 0;
    m_atlasUploaded = false;
}

bool TextRenderer::atlasAlloc(int w, int h, int& outX, int& outY) {
    if (w <= 0 || h <= 0) return false;
    if (w > m_atlasW || h > m_atlasH) return false;

    if (m_penX + w > m_atlasW) {
        m_penX = 0;
        m_penY += m_rowH;
        m_rowH = 0;
    }
    if (m_penY + h > m_atlasH) return false;

    outX = m_penX;
    outY = m_penY;

    m_penX += w;
    m_rowH = std::max(m_rowH, h);
    return true;
}

void TextRenderer::uploadAtlasIfNeeded() {
    if (m_atlasUploaded) return;

    if (!m_atlasTex) glGenTextures(1, &m_atlasTex);
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    #if GLES_VERSION == 3
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 
                 m_atlasW, m_atlasH, 0, 
                 GL_RED, GL_UNSIGNED_BYTE, m_atlasPixels.data());
    #else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA,
                 m_atlasW, m_atlasH, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, m_atlasPixels.data());
    #endif
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_atlasUploaded = true;
}

/* ---------------- Glyph cache / rasterize ---------------- */

TextRenderer::GlyphEntry* TextRenderer::findGlyph(uint32_t gid) {
    for (auto& g : m_glyphs) if (g.valid && g.gid == gid) return &g;
    return nullptr;
}

TextRenderer::GlyphEntry* TextRenderer::insertGlyph(uint32_t gid) {
    for (auto& g : m_glyphs) {
        if (!g.valid) { g.valid = true; g.gid = gid; return &g; }
    }
    return nullptr;
}

bool TextRenderer::rasterizeGlyph(GlyphEntry& out, uint32_t gid) {
    if (FT_Load_Glyph(m_face, gid,
                      FT_LOAD_RENDER | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
        return false;
    }

    FT_GlyphSlot gs = m_face->glyph;
    FT_Bitmap* bm = &gs->bitmap;
    const int w = (int)bm->width;
    const int h = (int)bm->rows;

    out.bearingX = gs->bitmap_left;
    out.bearingY = gs->bitmap_top;
    out.w = w;
    out.h = h;

    if (w == 0 || h == 0) {
        out.u0 = out.v0 = out.u1 = out.v1 = 0.0f;
        return true;
    }

    const int aw = w + 2 * kAtlasPad;
    const int ah = h + 2 * kAtlasPad;

    int x, y;
    if (!atlasAlloc(aw, ah, x, y)) return false;

    const int dstX = x + kAtlasPad;
    const int dstY = y + kAtlasPad;

    for (int row = 0; row < h; row++) {
        uint8_t* dst = m_atlasPixels.data() + (size_t)(dstY + row) * (size_t)m_atlasW + (size_t)dstX;
        const uint8_t* src = bm->buffer + (size_t)row * (size_t)bm->pitch;
        std::memcpy(dst, src, (size_t)w);
    }

    out.u0 = (float)dstX / (float)m_atlasW;
    out.v0 = (float)dstY / (float)m_atlasH;
    out.u1 = (float)(dstX + w) / (float)m_atlasW;
    out.v1 = (float)(dstY + h) / (float)m_atlasH;
    return true;
}

/* ---------------- Shaping / mesh ---------------- */

hb_buffer_t* TextRenderer::shapeUtf8(const char* utf8) {
    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, hb_script_from_string("Latn", -1));
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));
    hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
    hb_shape(m_hbFont, buf, nullptr, 0);
    return buf;
}

void TextRenderer::addGlyphQuad(std::vector<TextVtx>& vb,
                              float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1) {
    vb.push_back({x0,y0,u0,v0});
    vb.push_back({x1,y0,u1,v0});
    vb.push_back({x1,y1,u1,v1});

    vb.push_back({x0,y0,u0,v0});
    vb.push_back({x1,y1,u1,v1});
    vb.push_back({x0,y1,u0,v1});
}

bool TextRenderer::buildMesh(const char* utf8, std::vector<TextVtx>& out) {
    out.clear();

    hb_buffer_t* buf = shapeUtf8(utf8);
    const unsigned int count = hb_buffer_get_length(buf);

    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, nullptr);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, nullptr);

    float penX = 0.0f;
    float penY = 0.0f;

    for (unsigned int i = 0; i < count; i++) {
        uint32_t gid = infos[i].codepoint;

        GlyphEntry* ge = findGlyph(gid);
        if (!ge) {
            ge = insertGlyph(gid);
            if (!ge) { hb_buffer_destroy(buf); return false; }
            *ge = GlyphEntry{};
            ge->valid = true;
            ge->gid = gid;
            if (!rasterizeGlyph(*ge, gid)) { hb_buffer_destroy(buf); return false; }
            m_atlasUploaded = false;
        }

        float xOff = (float)pos[i].x_offset / 64.0f;
        float yOff = (float)pos[i].y_offset / 64.0f;
        float xAdv = (float)pos[i].x_advance / 64.0f;
        float yAdv = (float)pos[i].y_advance / 64.0f;

        float gx = penX + xOff + (float)ge->bearingX;
        float gy = penY - yOff - (float)ge->bearingY;

        if (ge->w > 0 && ge->h > 0) {
            addGlyphQuad(out,
                         gx, gy,
                         gx + (float)ge->w, gy + (float)ge->h,
                         ge->u0, ge->v0, ge->u1, ge->v1);
        }

        penX += xAdv;
        penY += yAdv;
    }

    hb_buffer_destroy(buf);
    return true;
}

/* ---------------- Text objects ---------------- */

TextRenderer::Handle TextRenderer::createText() {
    for (int i = 0; i < (int)m_items.size(); i++) {
        if (!m_items[i].alive) {
            auto& t = m_items[i];
            t = TextObj{};
            glGenBuffers(1, &t.vbo);
#if GLES_VERSION == 3
            glGenVertexArrays(1, &t.vao);
            setupTextVao(t.vao, t.vbo);
#endif
            t.alive = true;
            t.cpuDirty = true;
            t.gpuDirty = true;
            return Handle{i};
        }
    }

    TextObj t;
    glGenBuffers(1, &t.vbo);
#if GLES_VERSION == 3
    glGenVertexArrays(1, &t.vao);
    setupTextVao(t.vao, t.vbo);
#endif
    m_items.push_back(std::move(t));
    return Handle{(int)m_items.size() - 1};
}

void TextRenderer::destroyText(Handle h) {
    TextObj* t = get(h);
    if (!t) return;
#if GLES_VERSION == 3
    if (t->vao) glDeleteVertexArrays(1, &t->vao);
    t->vao = 0;
#endif
    if (t->vbo) glDeleteBuffers(1, &t->vbo);
    t->vbo = 0;
    t->alive = false;
}

TextRenderer::TextObj* TextRenderer::get(Handle h) {
    if (h.id < 0 || h.id >= (int)m_items.size()) return nullptr;
    if (!m_items[h.id].alive) return nullptr;
    return &m_items[h.id];
}

void TextRenderer::setText(Handle h, const char* utf8) {
    TextObj* t = get(h);
    if (!t) return;
    t->text = utf8 ? utf8 : "";
    t->cpuDirty = true;
}

void TextRenderer::setPos(Handle h, float x, float baselineY) {
    TextObj* t = get(h);
    if (!t) return;
    t->x = x;
    t->baselineY = baselineY;
}

void TextRenderer::setColor(Handle h, float r, float g, float b, float a) {
    TextObj* t = get(h);
    if (!t) return;
    t->r=r; t->g=g; t->b=b; t->a=a;
}

void TextRenderer::update() {
    for (auto& t : m_items) {
        if (!t.alive) continue;

        if (t.cpuDirty) {
            t.cpuDirty = false;
            if (!buildMesh(t.text.c_str(), t.mesh)) {
                t.mesh.clear();
            } else {
                t.gpuDirty = true;
            }
        }

        if (t.gpuDirty) {
            t.gpuDirty = false;
            glBindBuffer(GL_ARRAY_BUFFER, t.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(t.mesh.size() * sizeof(TextVtx)),
                         t.mesh.data(),
                         GL_DYNAMIC_DRAW);
        }
    }

    uploadAtlasIfNeeded();
}

void TextRenderer::draw(const float* mvp4x4) {
    if (!m_prog) return;

    glUseProgram(m_prog);
    glUniformMatrix4fv(m_uMVP, 1, GL_FALSE, mvp4x4);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);
    glUniform1i(m_uTex, 0);

    for (auto& t : m_items) {
        if (!t.alive) continue;

        glUniform4f(m_uColor, t.r, t.g, t.b, t.a);
        glUniform2f(m_uTranslate, t.x, t.baselineY);
#if GLES_VERSION == 3
        glBindVertexArray(t.vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)t.mesh.size());
#else
        glBindBuffer(GL_ARRAY_BUFFER, t.vbo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVtx), (void*)offsetof(TextVtx, x));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVtx), (void*)offsetof(TextVtx, u));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)t.mesh.size());
#endif
    }
#if GLES_VERSION == 3
    glBindVertexArray(0);
#endif
}