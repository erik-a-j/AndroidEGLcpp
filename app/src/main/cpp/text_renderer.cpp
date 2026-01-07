// text_system.cpp
#include <GLES3/gl3.h>

#include "text_renderer.hpp"

#include <cstring>
#include <cstddef>
#include <algorithm>

#include <unistd.h>

#include "logging.hpp"
static constexpr char NS[] = "TextR";
using logx = logger::logx<NS>;

static void logShader(GLuint s, const char* label) {
    GLint len = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    if (len > 1) {
        std::vector<char> buf((size_t)len);
        glGetShaderInfoLog(s, len, nullptr, buf.data());
        logx::Ef("{} Text shader log:\n{}", label, buf.data());
    }
}
static void logProgram(GLuint p) {
    GLint len = 0;
    glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
    if (len > 1) {
        std::vector<char> buf((size_t)len);
        glGetProgramInfoLog(p, len, nullptr, buf.data());
        logx::Ef("Text program log:\n{}", buf.data());
    }
}

static inline FT_Fixed f2dot16(float v) {
    // FreeType uses 16.16 fixed-point for variation coordinates
    // Round to nearest
    double x = (double)v * 65536.0;
    if (x >= 0.0) x += 0.5;
    else         x -= 0.5;
    return (FT_Fixed)x;
}
static std::vector<uint32_t> buildUtf8Index(const char* utf8) {
    std::vector<uint32_t> out;
    for (uint32_t i = 0; utf8[i]; ) {
        out.push_back(i);
        unsigned char c = (unsigned char)utf8[i];
        i += (c < 0x80) ? 1 :
             ((c & 0xE0) == 0xC0) ? 2 :
             ((c & 0xF0) == 0xE0) ? 3 : 4;
    }
    out.push_back((uint32_t)strlen(utf8));
    return out;
}
static int utf8_codepoint_count_from_index(const std::vector<uint32_t>& cpByteOffsets) {
    return (cpByteOffsets.size() >= 1) ? (int)cpByteOffsets.size() - 1 : 0;
}
static int codepointIndexFromCluster(uint32_t clusterByte, const std::vector<uint32_t>& cpByteOffsets) {
    // find greatest i where cpByteOffsets[i] <= clusterByte
    auto it = std::upper_bound(cpByteOffsets.begin(), cpByteOffsets.end(), clusterByte);
    if (it == cpByteOffsets.begin()) return 0;
    return (int)std::distance(cpByteOffsets.begin(), it - 1);
}
static uint32_t utf8DecodeOne(const char* s, int& advBytes) {
    const unsigned char c0 = (unsigned char)s[0];
    if (c0 < 0x80) { advBytes = 1; return c0; }
    if ((c0 & 0xE0) == 0xC0) { advBytes = 2; return ((c0 & 0x1F) << 6) | (s[1] & 0x3F); }
    if ((c0 & 0xF0) == 0xE0) { advBytes = 3; return ((c0 & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
    advBytes = 4;
    return ((c0 & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
}
TextRenderer::GlyphMetrics TextRenderer::measureCodepoint(uint32_t cp) const {
    GlyphMetrics gm{};

    if (!m_face) return gm;

    const FT_UInt gid = FT_Get_Char_Index(m_face, cp);
    gm.gid = gid;
    if (gid == 0) return gm; // missing glyph

    // Load metrics (no render needed). You can keep your hinting policy here.
    if (FT_Load_Glyph(m_face, gid, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) != 0) return gm;

    const FT_GlyphSlot slot = m_face->glyph;

    // Advance in 26.6 fixed-point
    gm.advanceX = (float)slot->advance.x / 64.0f;
    gm.advanceY = (float)slot->advance.y / 64.0f;

    // If you want bitmap metrics exactly like your atlas rendering uses:
    // do a render load (costly but accurate for bitmap box)
    if (FT_Load_Glyph(m_face, gid, FT_LOAD_RENDER | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) == 0) {
        gm.bmpW = (int)slot->bitmap.width;
        gm.bmpH = (int)slot->bitmap.rows;
        gm.bearingX = slot->bitmap_left;
        gm.bearingY = slot->bitmap_top;
    }

    // Outline bbox (works when outline exists); in font units -> convert to pixels.
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_BBox bb;
        FT_Outline_Get_CBox(&slot->outline, &bb); // 26.6 units
        gm.bboxXMin = (float)bb.xMin / 64.0f;
        gm.bboxYMin = (float)bb.yMin / 64.0f;
        gm.bboxXMax = (float)bb.xMax / 64.0f;
        gm.bboxYMax = (float)bb.yMax / 64.0f;
    }

    gm.valid = true;
    return gm;
}
TextRenderer::GlyphMetrics TextRenderer::measureUtf8Glyph(const char* utf8, int byteOffset) const {
    if (!utf8) return GlyphMetrics{};
    int adv = 0;
    const uint32_t cp = utf8DecodeOne(utf8 + byteOffset, adv);
    return measureCodepoint(cp);
}
int TextRenderer::caretIndexFromLocalX(const TextObj& t, float localX) {
    if (t.caretX.empty()) return 0;

    if (localX <= t.caretX.front()) return 0;
    if (localX >= t.caretX.back())  return (int)t.caretX.size() - 1;

    auto it = std::lower_bound(t.caretX.begin(), t.caretX.end(), localX);
    int i = (int)std::distance(t.caretX.begin(), it);
    if (i <= 0) return 0;

    float a = t.caretX[(size_t)i - 1];
    float b = t.caretX[(size_t)i];
    return (localX - a) < (b - localX) ? (i - 1) : i;
}
static bool pointInRect(float px, float py, float x0, float y0, float x1, float y1) {
    return (px >= x0 && px <= x1 && py >= y0 && py <= y1);
}
static void setupTextVao(GLuint vao, GLuint vbo) {
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glEnableVertexAttribArray(0); // aPos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(TextVtx), (void*)offsetof(TextVtx, x));

    glEnableVertexAttribArray(1); // aUV
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(TextVtx), (void*)offsetof(TextVtx, u));
    
    glEnableVertexAttribArray(2); // aColor
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(TextVtx), (void*)offsetof(TextVtx, r));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

TextRenderer::~TextRenderer() { 
    shutdown();
    if (m_ft) FT_Done_FreeType(m_ft);
    m_ft = nullptr;
}

bool TextRenderer::init(const Assets::Manager& am, const std::string& font_name, int pixelSize, int atlasW, int atlasH) {
    shutdown();
    
    m_font = am.get_font(font_name);
    if (m_font.bytes.empty()) return false;
    
    // 1) shader program
    if (!initProgram(am)) { return false; }

    // 2) font + atlas
    if (!initFont(pixelSize)) { destroyProgram(); return false; }
    if (!initAtlas(atlasW, atlasH)) { destroyFont(); destroyProgram(); return false; }

    std::memset(m_glyphs, 0, sizeof(m_glyphs));
    return true;
}
void TextRenderer::shutdown() {
    // Text VBOs
    for (auto& t : m_items) {
        if (t.vao) glDeleteVertexArrays(1, &t.vao);
        t.vao = 0;
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
        logShader(s, (type == GL_VERTEX_SHADER) ? "VERT" : "FRAG");
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
    logx::I("linkProgram done");
    return p;
}
bool TextRenderer::initProgram(const Assets::Manager& am) {
    std::vector<char> vs = am.read("shaders/text.vert");
    std::vector<char> fs = am.read("shaders/text.frag");
    if (vs.empty() || fs.empty()) {
        logx::E("failed reading text shaders from storage");
        return false;
    }
    
    m_prog = linkProgram(reinterpret_cast<char*>(vs.data()), reinterpret_cast<char*>(fs.data()));
    if (!m_prog) return false;

    m_uMVP       = glGetUniformLocation(m_prog, "uMVP");
    m_uTex       = glGetUniformLocation(m_prog, "uTex");
    //m_uColor     = glGetUniformLocation(m_prog, "uColor");
    m_uTranslate = glGetUniformLocation(m_prog, "uTranslate");

    logx::I("initProgram done");
    return true;
}
void TextRenderer::destroyProgram() {
    if (m_prog) glDeleteProgram(m_prog);
    m_prog = 0;
    m_uMVP = m_uTex = m_uTranslate = -1;
}

/* ---------------- Font (FreeType + HarfBuzz) ---------------- */
bool TextRenderer::initFont(int pixelSize) {
    if (pixelSize <= 0) {
        logx::E("initFont: invalid pixelSize");
        return false;
    }
    
    m_pxSize = pixelSize;
    if (!m_ft) {
        if (FT_Init_FreeType(&m_ft) != 0) return false;
    }
    
    FT_Open_Args args{};
    args.flags = FT_OPEN_MEMORY;
    args.memory_base = reinterpret_cast<const FT_Byte*>(m_font.bytes.data());
    args.memory_size = static_cast<FT_Long>(m_font.bytes.size());
    
    if (FT_Open_Face(m_ft, &args, (FT_Long)m_font.collectionIndex, &m_face) != 0) {
        logx::E("FT_Open_Face failed (fd + collectionIndex)");
        return false;
    }
    
    if (!m_font.variationSettings.empty() && FT_HAS_MULTIPLE_MASTERS(m_face)) {
        FT_MM_Var* mm = nullptr;
        if (FT_Get_MM_Var(m_face, &mm) == 0 && mm) {
            std::vector<FT_Fixed> coords(mm->num_axis);

            // Start from defaults
            for (FT_UInt a = 0; a < mm->num_axis; ++a) {
                coords[a] = mm->axis[a].def;
            }

            // Map axis tag -> index, then override
            for (const auto& [tag, val] : m_font.variationSettings) {
                for (FT_UInt a = 0; a < mm->num_axis; ++a) {
                    // FreeType stores axis tag as FT_ULong (big-endian 4-char tag)
                    if ((uint32_t)mm->axis[a].tag == tag) {
                        coords[a] = f2dot16(val);
                        break;
                    }
                }
            }

            FT_Error err = FT_Set_Var_Design_Coordinates(m_face, (FT_UInt)coords.size(), coords.data());
            if (err) {
                logx::Ef("FT_Set_Var_Design_Coordinates returned FT_Error({})", err);
                return false;
            }
            FT_Done_MM_Var(m_ft, mm);
        }
    }
    
    if (FT_Set_Pixel_Sizes(m_face, 0, (FT_UInt)pixelSize) != 0) {
        logx::E("FT_Set_Pixel_Sizes failed");
        FT_Done_Face(m_face); m_face = nullptr;
        return false;
    }

    m_hbFont = hb_ft_font_create_referenced(m_face);
    if (!m_hbFont) {
        logx::E("hb_ft_font_create_referenced failed");
        FT_Done_Face(m_face); m_face = nullptr;
        return false;
    }
    hb_ft_font_set_funcs(m_hbFont);
    hb_font_set_scale(m_hbFont,
                      (int)m_face->size->metrics.x_ppem * 64,
                      (int)m_face->size->metrics.y_ppem * 64);
    
    auto& m = m_face->size->metrics;

    // In FreeType, ascent is positive, descent is negative (typically).
    float asc = (float)m.ascender / 64.0f;
    float desc = (float)(-m.descender) / 64.0f; // make it positive magnitude
    float gap = (float)(m.height - (m.ascender - m.descender)) / 64.0f; // optional

    m_lm.ascent  = asc;
    m_lm.descent = desc;
    m_lm.lineGap = std::max(0.0f, gap);
    
    logx::I("initFont done");
    return true;
}
void TextRenderer::destroyFont() {
    if (m_hbFont) hb_font_destroy(m_hbFont);
    if (m_face) FT_Done_Face(m_face);
    m_hbFont = nullptr;
    m_face = nullptr;
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
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 
                 m_atlasW, m_atlasH, 0, 
                 GL_RED, GL_UNSIGNED_BYTE, m_atlasPixels.data());
    
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
    hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    //hb_buffer_set_script(buf, hb_script_from_string("Latn", -1));
    //hb_buffer_set_language(buf, hb_language_from_string("en", -1));
    hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(m_hbFont, buf, nullptr, 0);
    return buf;
}
void TextRenderer::addGlyphQuad(std::vector<TextVtx>& vb,
                              float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1,
                              const RGBA& c) {
    vb.emplace_back(x0,y0,u0,v0,c);
    vb.emplace_back(x1,y0,u1,v0,c);
    vb.emplace_back(x1,y1,u1,v1,c);

    vb.emplace_back(x0,y0,u0,v0,c);
    vb.emplace_back(x1,y1,u1,v1,c);
    vb.emplace_back(x0,y1,u0,v1,c);
}

bool TextRenderer::buildMesh(TextObj& t) {
    t.mesh.clear();

    t.cpByteOffsets = buildUtf8Index(t.text.c_str());
    const int numCP = utf8_codepoint_count_from_index(t.cpByteOffsets);
    t.caretX.assign((size_t)numCP + 1, 0.0f);

    hb_buffer_t* buf = shapeUtf8(t.text.c_str());
    const unsigned int count = hb_buffer_get_length(buf);
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, nullptr);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, nullptr);

    float penX = 0.0f;
    float penY = 0.0f;

    t.caretX[0] = 0.0f;

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

        float xOff = (float)pos[i].x_offset  / 64.0f;
        float yOff = (float)pos[i].y_offset  / 64.0f;
        float xAdv = (float)pos[i].x_advance / 64.0f;
        float yAdv = (float)pos[i].y_advance / 64.0f;

        const int cpIdx = codepointIndexFromCluster(infos[i].cluster, t.cpByteOffsets);

        // Draw quad
        float gx = penX + xOff + (float)ge->bearingX;
        float gy = penY - yOff - (float)ge->bearingY;
        if (ge->w > 0 && ge->h > 0) {
            addGlyphQuad(t.mesh,
                         gx, gy, gx + (float)ge->w, gy + (float)ge->h,
                         ge->u0, ge->v0, ge->u1, ge->v1, t.c);
        }

        // Advance pen
        float nextPenX = penX + xAdv;
        float nextPenY = penY + yAdv;

        // Store caret for "after this character" (best-effort)
        // Clamp index into [0..numCP]
        int after = std::min(std::max(cpIdx + 1, 0), numCP);
        t.caretX[(size_t)after] = std::max(t.caretX[(size_t)after], nextPenX);

        penX = nextPenX;
        penY = nextPenY;
    }

    hb_buffer_destroy(buf);

    // Make caretX monotone and fill missing
    for (int k = 1; k <= numCP; k++) {
        t.caretX[(size_t)k] = std::max(t.caretX[(size_t)k], t.caretX[(size_t)k - 1]);
    }
    return true;
}

/* ---------------- selection ---------------- */
TextRenderer::Handle TextRenderer::hitTest(float screenX, float screenY) const {
    // Iterate from end to start so last-created draws "on top" and wins.
    for (int i = (int)m_items.size() - 1; i >= 0; --i) {
        const auto& t = m_items[i];
        if (!t.alive || !t.selectable) continue;

        if (t.caretX.empty()) continue; // not shaped yet

        float x0 = t.x;
        float x1 = t.x + t.caretX.back();

        float y0 = t.baselineY - m_lm.ascent;
        float y1 = t.baselineY + m_lm.descent;

        if (pointInRect(screenX, screenY, x0, y0, x1, y1)) {
            return Handle{i};
        }
    }
    return Handle{-1};
}
int TextRenderer::caretFromPoint(Handle h, float screenX, float screenY) const {
    if (h.id < 0 || h.id >= (int)m_items.size()) return -1;
    const auto& t = m_items[h.id];
    if (!t.alive || !t.selectable) return -1;
    if (t.caretX.empty()) return -1;

    float localX = screenX - t.x;
    // For single-line, y only gates whether it's "on the line".
    float y0 = t.baselineY - m_lm.ascent;
    float y1 = t.baselineY + m_lm.descent;
    if (screenY < y0 || screenY > y1) return -1;

    return caretIndexFromLocalX(t, localX);
}
int TextRenderer::caretFromPointNoY(Handle h, float screenX) const {
    if (h.id < 0 || h.id >= (int)m_items.size()) return -1;
    const auto& t = m_items[h.id];
    if (!t.alive || !t.selectable) return -1;
    if (t.caretX.empty()) return -1;
    return caretIndexFromLocalX(t, screenX - t.x);
}
void TextRenderer::beginSelection(Handle h, float screenX, float screenY) {
    TextObj* t = get(h);
    if (!t || !t->selectable) return;

    int c = caretFromPoint(h, screenX, screenY);
    if (c < 0) return;

    t->selecting = true;
    t->selA = c;
    t->selB = c;
    t->caret = c;
}
void TextRenderer::updateSelection(Handle h, float screenX, float screenY) {
    TextObj* t = get(h);
    if (!t || !t->selectable || !t->selecting) return;

    int c = caretFromPointNoY(h, screenX);
    if (c < 0) return;

    t->selB = c;
    t->caret = c;
}
void TextRenderer::endSelection(Handle h) {
    TextObj* t = get(h);
    if (!t) return;
    t->selecting = false;
}
TextRenderer::SelectionInfo TextRenderer::getSelectionInfo(Handle h) const {
    SelectionInfo si;
    si.h = h;

    if (h.id < 0 || h.id >= (int)m_items.size()) return si;
    const auto& t = m_items[h.id];
    if (!t.alive) return si;

    si.valid = !t.caretX.empty();
    si.selectable = t.selectable;
    si.caret = t.caret;
    si.selA  = t.selA;
    si.selB  = t.selB;

    if (!si.valid) return si;

    // Full text bounds in screen space
    si.x0 = t.x;
    si.x1 = t.x + t.caretX.back();
    si.y0 = t.baselineY - m_lm.ascent;
    si.y1 = t.baselineY + m_lm.descent;

    // Selection bounds (if any)
    int s0 = std::min(t.selA, t.selB);
    int s1 = std::max(t.selA, t.selB);

    if (s1 > s0) {
        s0 = std::clamp(s0, 0, (int)t.caretX.size() - 1);
        s1 = std::clamp(s1, 0, (int)t.caretX.size() - 1);

        si.hasSelection = true;
        si.selX0 = t.x + t.caretX[(size_t)s0];
        si.selX1 = t.x + t.caretX[(size_t)s1];
        si.selY0 = si.y0;
        si.selY1 = si.y1;
    }
    return si;
}

/* ---------------- Text objects ---------------- */
TextRenderer::Handle TextRenderer::createText() {
    for (int i = 0; i < (int)m_items.size(); i++) {
        if (!m_items[i].alive) {
            auto& t = m_items[i];
            t = TextObj{};
            glGenBuffers(1, &t.vbo);
            glGenVertexArrays(1, &t.vao);
            setupTextVao(t.vao, t.vbo);
            t.alive = true;
            t.cpuDirty = true;
            t.gpuDirty = true;
            return Handle{i};
        }
    }

    TextObj t;
    glGenBuffers(1, &t.vbo);
    glGenVertexArrays(1, &t.vao);
    setupTextVao(t.vao, t.vbo);
    
    m_items.push_back(std::move(t));
    return Handle{(int)m_items.size() - 1};
}
void TextRenderer::destroyText(Handle h) {
    TextObj* t = get(h);
    if (!t) return;
    
    if (t->vao) glDeleteVertexArrays(1, &t->vao);
    t->vao = 0;
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
void TextRenderer::setColor(Handle h, const RGBA& c) {
    TextObj* t = get(h);
    if (!t) return;
    t->c = c;
}
void TextRenderer::update() {
    for (auto& t : m_items) {
        if (!t.alive) continue;

        if (t.cpuDirty) {
            t.cpuDirty = false;
            if (!buildMesh(t)) {
                logx::E("buildMesh failed");
                t.mesh.clear();
            } else {
                logx::If("mesh verts: {}", t.mesh.size());
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

        //glUniform4f(m_uColor, t.r, t.g, t.b, t.a);
        glUniform2f(m_uTranslate, t.x, t.baselineY);
        glBindVertexArray(t.vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)t.mesh.size());
    }
    glBindVertexArray(0);
}
