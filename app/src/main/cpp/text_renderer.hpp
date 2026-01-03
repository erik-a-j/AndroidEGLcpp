// text_system.hpp (updated to own shader program like UiRenderer)
#pragma once

#include "config.h"
#if GLES_VERSION == 3
#include <GLES3/gl3.h>
#else
#include <GLES2/gl2.h>
#endif

#include <hb.h>
#include <hb-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <vector>
#include <string>

struct TextVtx {
    float x, y;
    float u, v;
};

class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Must be called after EGL context is current.
    // Creates FreeType/HarfBuzz + atlas + also compiles/links the text shader program.
    bool init(const char* fontFilePath,
              int pixelSize,
              int atlasW = 2048,
              int atlasH = 2048);

    // Must be called before EGL context is destroyed (or while context is current).
    void shutdown();

    struct Handle { int id = -1; };

    Handle createText();
    void destroyText(Handle h);

    void setText(Handle h, const char* utf8);
    void setPos(Handle h, float x, float baselineY);
    void setColor(Handle h, float r, float g, float b, float a);

    // Call once per frame (or only when you know something changed).
    void update();

    // Draw using internal program.
    void draw(const float* mvp4x4);

    // Optional: expose program/atlas (useful for debugging)
    GLuint program() const { return m_prog; }
    GLuint atlasTexture() const { return m_atlasTex; }

private:
    // ----- Program -----
    bool initProgram();
    void destroyProgram();
    static GLuint compileShader(GLenum type, const char* src);
    static GLuint linkProgram(const char* vs, const char* fs);

    // ----- Font (FreeType + HarfBuzz) -----
    bool initFont(const char* fontFilePath, int pixelSize);
    void destroyFont();

    // ----- Atlas -----
    bool initAtlas(int w, int h);
    void destroyAtlas();
    bool atlasAlloc(int w, int h, int& outX, int& outY);
    void uploadAtlasIfNeeded();

    // ----- Glyph cache / rasterize -----
    struct GlyphEntry {
        uint32_t gid = 0;
        float u0=0, v0=0, u1=0, v1=0;
        int w=0, h=0;
        int bearingX=0, bearingY=0;
        bool valid=false;
    };

    GlyphEntry* findGlyph(uint32_t gid);
    GlyphEntry* insertGlyph(uint32_t gid);
    bool rasterizeGlyph(GlyphEntry& out, uint32_t gid);

    // ----- Shaping / mesh -----
    hb_buffer_t* shapeUtf8(const char* utf8);
    void addGlyphQuad(std::vector<TextVtx>& vb,
                      float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1);
    bool buildMesh(const char* utf8, std::vector<TextVtx>& out);

    // ----- Text objects -----
    struct TextObj {
        float x=0, baselineY=0;
        float r=1, g=1, b=1, a=1;
        std::string text;

        std::vector<TextVtx> mesh;
        GLuint vbo = 0;
#if GLES_VERSION == 3
        GLuint vao = 0;
#endif
        bool cpuDirty = true;
        bool gpuDirty = true;
        bool alive = true;
    };

    TextObj* get(Handle h);

private:
    // Program state
    GLuint m_prog = 0;
    GLint  m_uMVP = -1;
    GLint  m_uTex = -1;
    GLint  m_uColor = -1;
    GLint  m_uTranslate = -1;

    // Font state
    FT_Library m_ft = nullptr;
    FT_Face    m_face = nullptr;
    hb_font_t* m_hbFont = nullptr;
    int        m_pxSize = 0;

    // Atlas state
    int m_atlasW=0, m_atlasH=0;
    std::vector<uint8_t> m_atlasPixels; // A8
    int m_penX=0, m_penY=0, m_rowH=0;
    GLuint m_atlasTex = 0;
    bool m_atlasUploaded = false;

    static constexpr int kGlyphCacheMax = 512;
    static constexpr int kAtlasPad = 1;
    GlyphEntry m_glyphs[kGlyphCacheMax]{};

    // Text objects
    std::vector<TextObj> m_items;
};