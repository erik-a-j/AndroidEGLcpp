// text_system.hpp (updated to own shader program like UiRenderer)
#pragma once

#include <GLES3/gl3.h>

#include <hb.h>
#include <hb-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "assets.hpp"
#include "types.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

struct TextVtx {
    float x, y;
    float u, v;
    uint8_t r, g, b, a;
    TextVtx(float px, float py, float u, float v, const RGBA& c)
     : x(px), y(py), u(u), v(v), r(c.r), g(c.g), b(c.b), a(c.a) {}
};

class TextRenderer {
public:
    
    TextRenderer() = default;
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Must be called after EGL context is current.
    // Creates FreeType/HarfBuzz + atlas + also compiles/links the text shader program.
    bool init(const Assets::Manager& am,
              const std::string& font_name,
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
    void setColor(Handle h, const RGBA& c);

    // Call once per frame (or only when you know something changed).
    void update();

    // Draw using internal program.
    void draw(const float* mvp4x4);

    // Optional: expose program/atlas (useful for debugging)
    GLuint program() const { return m_prog; }
    GLuint atlasTexture() const { return m_atlasTex; }
    
    // Returns handle of topmost hit text object, or {-1} if none.
    Handle hitTest(float screenX, float screenY) const;

    // Convert touch to caret index for a given text object (clamped).
    // Returns -1 if handle invalid or not hittable.
    int caretFromPoint(Handle h, float screenX, float screenY) const;
    int caretFromPointNoY(Handle h, float screenX) const;
    // Convenience: set selection/caret from touch (anchor or drag)
    void beginSelection(Handle h, float screenX, float screenY);
    void updateSelection(Handle h, float screenX, float screenY);
    void endSelection(Handle h);
    
    // Caller queries geometry/state for drawing:
    struct SelectionInfo {
        Handle h{-1};
    
        bool  valid = false;       // object exists + shaped
        bool  selectable = false;
    
        // text rect in screen space (single-line for now)
        float x0=0, y0=0, x1=0, y1=0;
    
        // caret + selection indices (codepoint indices)
        int caret = 0;
        int selA  = 0;
        int selB  = 0;
    
        // selection rect in screen space (empty if no selection)
        bool  hasSelection = false;
        float selX0=0, selY0=0, selX1=0, selY1=0;
    };
    SelectionInfo getSelectionInfo(Handle h) const;
private:
    // ----- Text objects -----
    struct TextObj {
        float x=0, baselineY=0;
        RGBA c;
        //float r=1, g=1, b=1, a=1;
        std::string text;

        std::vector<TextVtx> mesh;
        GLuint vbo = 0;
        GLuint vao = 0;

        // --- shaping / selection support ---
        std::vector<uint32_t> cpByteOffsets; // codepoint index -> utf8 byte offset (size = N+1)
        std::vector<float>    caretX;        // caret positions in local text space (size = N+1)
    
        // --- selection state ---
        bool selectable = true;
        bool selecting = false;        // drag active
        int  selA = 0;                 // anchor (char index)
        int  selB = 0;                 // active end (char index)
        int  caret = 0;              // caret index (codepoint)
        
        bool cpuDirty = true;
        bool gpuDirty = true;
        bool alive = true;
    };
    struct LineMetrics {
        float ascent;   // +down or +up depends on your convention; below assumes y+down screen space
        float descent;
        float lineGap;
        float height() const { return ascent + descent + lineGap; }
    };
    
    // ----- Program -----
    bool initProgram(const Assets::Manager& am);
    void destroyProgram();
    static GLuint compileShader(GLenum type, const char* src);
    static GLuint linkProgram(const char* vs, const char* fs);

    // ----- Font (FreeType + HarfBuzz) -----
    bool initFont(const std::string& fontFilePath, int pixelSize);
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
                      float u0, float v0, float u1, float v1,
                      const RGBA& c);
    bool buildMesh(TextObj& t);
    
    static int caretIndexFromLocalX(const TextObj& t, float localX);
    
    TextObj* get(Handle h);

private:
    // Program state
    GLuint m_prog = 0;
    GLint  m_uMVP = -1;
    GLint  m_uTex = -1;
    //GLint  m_uColor = -1;
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
    LineMetrics m_lm{};

    // Text objects
    std::vector<TextObj> m_items;
};