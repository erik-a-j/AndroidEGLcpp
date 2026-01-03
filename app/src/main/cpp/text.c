// main.c
// Minimal NativeActivity (android_native_app_glue) + EGL/GLES2 + HarfBuzz shaping + FreeType rasterization
// - Put a TTF in: app/src/main/assets/Roboto-Regular.ttf
// - Build with CMake linking freetype + harfbuzz (see CMake snippet after this file)

#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "HBFT", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "HBFT", __VA_ARGS__)

// Very small linear cache for demo.
#define GLYPH_CACHE_MAX 512
#define ATLAS_PAD 1

// Add these near the top (after LOGI/LOGE)
#include <errno.h>

static void gl_log_info(const char* label) {
    const char* vendor   = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version  = (const char*)glGetString(GL_VERSION);
    const char* slver    = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    LOGI("%s: GL_VENDOR=%s", label, vendor ? vendor : "(null)");
    LOGI("%s: GL_RENDERER=%s", label, renderer ? renderer : "(null)");
    LOGI("%s: GL_VERSION=%s", label, version ? version : "(null)");
    LOGI("%s: GLSL=%s", label, slver ? slver : "(null)");
}
static void gl_check(const char* where) {
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR) {
        LOGE("GL error at %s: 0x%x", where, (unsigned)e);
    }
}
static void egl_log_error(const char* where) {
    EGLint e = eglGetError();
    if (e != EGL_SUCCESS) LOGE("EGL error at %s: 0x%x", where, (unsigned)e);
}
// For spotting pathological glyph sizes
static void log_glyph_bitmap(uint32_t gid, int w, int h, int pitch) {
    LOGI("glyph gid=%u bitmap %dx%d pitch=%d", gid, w, h, pitch);
}

/* ---------------- EGL / App ---------------- */

typedef struct {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int32_t width, height;
    bool initialized;
} Renderer;

/* ---------------- Font / Text ---------------- */

typedef struct {
    FT_Library ft;
    FT_Face    face;
    hb_font_t* hb_font;
    int        px_size;
} FontCtx;
typedef struct {
    int atlas_w, atlas_h;
    unsigned char* pixels;   // A8
    int pen_x, pen_y, row_h;
    GLuint tex;
    bool uploaded;
} Atlas;
typedef struct {
    uint32_t gid;
    float u0,v0,u1,v1;
    int w,h;
    int bearing_x, bearing_y; // bitmap_left, bitmap_top
    bool valid;
} GlyphEntry;
typedef struct {
    float x,y;
    float u,v;
} Vtx;
typedef struct {
    Vtx* v;
    int vcount;     // number of vertices
    int vcap;
} VtxBuf;

static void vb_init(VtxBuf* b) { memset(b, 0, sizeof(*b)); }
static void vb_free(VtxBuf* b) { free(b->v); memset(b, 0, sizeof(*b)); }
static void vb_reserve(VtxBuf* b, int want) {
    if (want <= b->vcap) return;
    int ncap = b->vcap ? b->vcap * 2 : 1024;
    while (ncap < want) ncap *= 2;
    b->v = (Vtx*)realloc(b->v, (size_t)ncap * sizeof(Vtx));
    b->vcap = ncap;
}
static void vb_push(VtxBuf* b, Vtx v) {
    vb_reserve(b, b->vcount + 1);
    b->v[b->vcount++] = v;
}

typedef struct {
    float x, baseline_y;
    float r,g,b,a;
    char  text[256];

    VtxBuf mesh;          // CPU vertices for this text
    GLuint vbo;           // optional: per-text VBO (or use one shared VBO)
    bool cpu_dirty;       // string/pos changed -> rebuild mesh
    bool gpu_dirty;       // mesh changed -> upload to VBO
} TextObj;
typedef struct {
    FontCtx font;
    Atlas atlas;
    GlyphEntry glyphs[GLYPH_CACHE_MAX];

    TextObj* items;
    int count, cap;
} TextSystem;

static void textobj_init(TextObj* t) {
    memset(t, 0, sizeof(*t));
    vb_init(&t->mesh);
    glGenBuffers(1, &t->vbo);
    t->r=t->g=t->b=t->a=1.0f;
    t->cpu_dirty = true;
    t->gpu_dirty = true;
}
static void textobj_destroy(TextObj* t) {
    if (t->vbo) glDeleteBuffers(1, &t->vbo);
    vb_free(&t->mesh);
}

static void textobj_set_text(TextObj* t, const char* s) {
    strncpy(t->text, s, sizeof(t->text)-1);
    t->text[sizeof(t->text)-1] = 0;
    t->cpu_dirty = true;
}
static void textobj_set_pos(TextObj* t, float x, float baseline_y) {
    t->x = x;
    t->baseline_y = baseline_y;
    //t->cpu_dirty = true; // because vertices depend on x/y
}
static void textobj_set_color(TextObj* t, float r,float g,float b,float a) {
    t->r=r; t->g=g; t->b=b; t->a=a;
}

static void textsystem_init_empty(TextSystem* ts) {
    memset(ts, 0, sizeof(*ts));
}
static TextObj* textsystem_add(TextSystem* ts) {
    if (ts->count == ts->cap) {
        int ncap = ts->cap ? ts->cap * 2 : 4;
        ts->items = (TextObj*)realloc(ts->items, (size_t)ncap * sizeof(TextObj));
        memset(ts->items + ts->cap, 0, (size_t)(ncap - ts->cap) * sizeof(TextObj));
        ts->cap = ncap;
    }
    TextObj* t = &ts->items[ts->count++];
    textobj_init(t);
    return t;
}

static void atlas_destroy(Atlas* a);
static void font_destroy(FontCtx* f);
static void textsystem_destroy(TextSystem* ts) {
    for (int i = 0; i < ts->count; i++) textobj_destroy(&ts->items[i]);
    free(ts->items);
    ts->items = NULL;
    ts->count = ts->cap = 0;

    if (ts->atlas.pixels) atlas_destroy(&ts->atlas);
    font_destroy(&ts->font);
    memset(ts->glyphs, 0, sizeof(ts->glyphs));
}

/* ---------------- GLES2 minimal shader ---------------- */

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, (GLsizei)sizeof(log), NULL, log);
        LOGE("shader compile fail: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}
static GLuint link_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);

    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aUV");

    glLinkProgram(p);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, (GLsizei)sizeof(log), NULL, log);
        LOGE("program link fail: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}
static void mat4_ortho(float* m, float l, float r, float b, float t) {
    // Column-major
    memset(m, 0, 16 * sizeof(float));
    m[0]  =  2.0f / (r - l);
    m[5]  =  2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] =  1.0f;
}

/* ---------------- Asset helper: copy asset TTF to internal storage ---------------- */

static int copy_asset_to_file(struct android_app* app,
                              const char* asset_name,
                              char* out_path,
                              size_t out_path_cap)
{
    const char* dir = app->activity->internalDataPath;
    LOGI("ASSET: internalDataPath=%s", dir ? dir : "(null)");
    if (!dir) return 0;

    const char* base = strrchr(asset_name, '/');
    base = base ? base + 1 : asset_name;

    snprintf(out_path, out_path_cap, "%s/%s", dir, base);
    LOGI("ASSET: target path=%s", out_path);

    FILE* ftest = fopen(out_path, "rb");
    if (ftest) {
        fclose(ftest);
        LOGI("ASSET: already exists, reuse");
        return 1;
    }

    AAssetManager* am = app->activity->assetManager;
    if (!am) { LOGE("ASSET: assetManager is null"); return 0; }

    LOGI("ASSET: opening %s", asset_name);
    AAsset* a = AAssetManager_open(am, asset_name, AASSET_MODE_STREAMING);
    if (!a) {
        LOGE("ASSET: not found: %s", asset_name);
        return 0;
    }

    FILE* f = fopen(out_path, "wb");
    if (!f) {
        LOGE("ASSET: fopen(wb) failed: %s (errno=%d)", out_path, errno);
        AAsset_close(a);
        return 0;
    }

    char buf[16 * 1024];
    int r;
    size_t total = 0;
    while ((r = AAsset_read(a, buf, (int)sizeof(buf))) > 0) {
        if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            LOGE("ASSET: fwrite failed (errno=%d)", errno);
            fclose(f);
            AAsset_close(a);
            return 0;
        }
        total += (size_t)r;
    }

    fclose(f);
    AAsset_close(a);
    LOGI("ASSET: copied %s -> %s (%zu bytes)", asset_name, out_path, total);
    return 1;
}

/* ---------------- FreeType + HarfBuzz init ---------------- */

static int font_init(FontCtx* f, const char* ttf_path, int pixel_size) {
    memset(f, 0, sizeof(*f));
    f->px_size = pixel_size;

    if (FT_Init_FreeType(&f->ft) != 0) { LOGE("FT_Init_FreeType fail"); return 0; }
    if (FT_New_Face(f->ft, ttf_path, 0, &f->face) != 0) { LOGE("FT_New_Face fail"); return 0; }

    if (FT_Set_Char_Size(f->face, 0, pixel_size * 64, 96, 96) != 0) {
        LOGE("FT_Set_Char_Size fail");
        return 0;
    }
    LOGI("FT: opened face: family=%s style=%s units_per_EM=%d num_glyphs=%ld",
     f->face->family_name ? f->face->family_name : "(null)",
     f->face->style_name ? f->face->style_name : "(null)",
     (int)f->face->units_per_EM,
     (long)f->face->num_glyphs);
    //LOGI("FT: set pixel size=%d", pixel_size);
    LOGI("FT: metrics: ascender=%ld descender=%ld height=%ld",
     (long)f->face->size->metrics.ascender,
     (long)f->face->size->metrics.descender,
     (long)f->face->size->metrics.height);

    f->hb_font = hb_ft_font_create_referenced(f->face);
    if (!f->hb_font) { LOGE("hb_ft_font_create fail"); return 0; }
    LOGI("HB: hb_font created");
    
    // Ensure consistent scale for hb positions
    hb_font_set_scale(f->hb_font,
                  (int)f->face->size->metrics.x_ppem * 64,
                  (int)f->face->size->metrics.y_ppem * 64);
    
    return 1;
}
static void font_destroy(FontCtx* f) {
    if (f->hb_font) hb_font_destroy(f->hb_font);
    if (f->face) FT_Done_Face(f->face);
    if (f->ft) FT_Done_FreeType(f->ft);
    memset(f, 0, sizeof(*f));
}

/* ---------------- Atlas + glyph cache (very small demo cache) ---------------- */

static void atlas_init(Atlas* a, int w, int h) {
    memset(a, 0, sizeof(*a));
    a->atlas_w = w; a->atlas_h = h;
    a->pixels = (unsigned char*)calloc((size_t)w * (size_t)h, 1);
    a->pen_x = 0; a->pen_y = 0; a->row_h = 0;
    a->tex = 0;
    a->uploaded = false;
    LOGI("ATLAS: init %dx%d (%zu bytes)", w, h, (size_t)w*(size_t)h);
}
static void atlas_destroy(Atlas* a) {
    if (a->tex) glDeleteTextures(1, &a->tex);
    free(a->pixels);
    memset(a, 0, sizeof(*a));
}
static int atlas_alloc(Atlas* a, int w, int h, int* out_x, int* out_y) {
    if (w <= 0 || h <= 0) return 0;
    if (w > a->atlas_w || h > a->atlas_h) {
        LOGE("ATLAS: request too big: %dx%d (atlas %dx%d)", w, h, a->atlas_w, a->atlas_h);
        return 0;
    }

    if (a->pen_x + w > a->atlas_w) {
        a->pen_x = 0;
        a->pen_y += a->row_h;
        a->row_h = 0;
    }
    if (a->pen_y + h > a->atlas_h) {
        LOGE("ATLAS: full at pen_y=%d row_h=%d request=%dx%d (atlas %dx%d)",
             a->pen_y, a->row_h, w, h, a->atlas_w, a->atlas_h);
        return 0;
    }

    *out_x = a->pen_x;
    *out_y = a->pen_y;

    a->pen_x += w;
    if (h > a->row_h) a->row_h = h;

    return 1;
}

static GlyphEntry* glyph_cache_find(GlyphEntry* cache, uint32_t gid) {
    for (int i = 0; i < GLYPH_CACHE_MAX; i++) {
        if (cache[i].valid && cache[i].gid == gid) return &cache[i];
    }
    return NULL;
}
static GlyphEntry* glyph_cache_insert(GlyphEntry* cache, uint32_t gid) {
    for (int i = 0; i < GLYPH_CACHE_MAX; i++) {
        if (!cache[i].valid) {
            cache[i].valid = true;
            cache[i].gid = gid;
            return &cache[i];
        }
    }
    return NULL;
}
static int rasterize_glyph(FontCtx* f, Atlas* a, GlyphEntry* out, uint32_t gid) {
    static int dbg_glyph_count = 0;

    if (FT_Load_Glyph(
            f->face,
            gid,
            FT_LOAD_RENDER |
            FT_LOAD_NO_HINTING |
            FT_LOAD_NO_BITMAP
        ) != 0) {
        LOGE("FT: FT_Load_Glyph failed (gid=%u)", gid);
        return 0;
    }

    FT_GlyphSlot g = f->face->glyph;
    FT_Bitmap* bm = &g->bitmap;

    int w = (int)bm->width;
    int h = (int)bm->rows;

    dbg_glyph_count++;
    if (dbg_glyph_count <= 40 || w > 256 || h > 256) {
        log_glyph_bitmap(gid, w, h, (int)bm->pitch);
    }

    if (w > a->atlas_w || h > a->atlas_h) {
        LOGE("FT: glyph too large for atlas gid=%u bitmap=%dx%d atlas=%dx%d",
             gid, w, h, a->atlas_w, a->atlas_h);
        return 0;
    }

    out->bearing_x = g->bitmap_left;
    out->bearing_y = g->bitmap_top;
    out->w = w;
    out->h = h;

    if (w == 0 || h == 0) {
        out->u0 = out->v0 = out->u1 = out->v1 = 0.0f;
        return 1;
    }
    if (w > 512 || h > 512) {
        LOGE("FT: pathological glyph gid=%u bitmap=%dx%d, skipping", gid, w, h);
        out->u0 = out->v0 = out->u1 = out->v1 = 0.0f;
        out->w = out->h = 0;
        return 1;
    }
    
    int aw = w + 2*ATLAS_PAD;
    int ah = h + 2*ATLAS_PAD;

    int x, y;
    if (!atlas_alloc(a, aw, ah, &x, &y)) {
        LOGE("ATLAS: full gid=%u bitmap=%dx%d pen=(%d,%d) row_h=%d atlas=%dx%d",
             gid, aw, ah, a->pen_x, a->pen_y, a->row_h, a->atlas_w, a->atlas_h);
        return 0;
    }
    
    int dst_x = x + ATLAS_PAD;
    int dst_y = y + ATLAS_PAD;

    for (int row = 0; row < h; row++) {
        unsigned char* dst = a->pixels + (size_t)(dst_y + row) * (size_t)a->atlas_w + (size_t)dst_x;
        unsigned char* src = bm->buffer + (size_t)row * (size_t)bm->pitch;
        memcpy(dst, src, (size_t)w);
    }

    out->u0 = (float)dst_x / (float)a->atlas_w;
    out->v0 = (float)dst_y / (float)a->atlas_h;
    out->u1 = (float)(dst_x + w) / (float)a->atlas_w;
    out->v1 = (float)(dst_y + h) / (float)a->atlas_h;

    return 1;
}
static void atlas_upload_if_needed(Atlas* a) {
    if (a->uploaded) return;

    if (!a->tex) glGenTextures(1, &a->tex);
    glBindTexture(GL_TEXTURE_2D, a->tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA,
                 a->atlas_w, a->atlas_h, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, a->pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    a->uploaded = true;
    gl_check("atlas_upload_if_needed");
}

/* ---------------- Shaping + quad generation ---------------- */

static hb_buffer_t* shape(FontCtx* f, const char* utf8,
                          hb_direction_t dir,
                          hb_script_t script,
                          hb_language_t lang)
{
    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_set_direction(buf, dir);
    hb_buffer_set_script(buf, script);
    hb_buffer_set_language(buf, lang);

    hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
    hb_shape(f->hb_font, buf, NULL, 0);
    return buf;
}
static void add_glyph_quad(VtxBuf* vb,
                           float x0, float y0, float x1, float y1,
                           float u0, float v0, float u1, float v1)
{
    // 2 triangles (CCW) in screen pixel space
    vb_push(vb, (Vtx){x0,y0, u0,v0});
    vb_push(vb, (Vtx){x1,y0, u1,v0});
    vb_push(vb, (Vtx){x1,y1, u1,v1});

    vb_push(vb, (Vtx){x0,y0, u0,v0});
    vb_push(vb, (Vtx){x1,y1, u1,v1});
    vb_push(vb, (Vtx){x0,y1, u0,v1});
}
static int build_text_mesh(FontCtx* f, Atlas* a, GlyphEntry* cache,
                           const char* utf8,
                           float origin_x, float baseline_y,
                           VtxBuf* out_vb)
{
    out_vb->vcount = 0;

    hb_buffer_t* buf = shape(f, utf8,
                             HB_DIRECTION_LTR,
                             hb_script_from_string("Latn", -1),
                             hb_language_from_string("en", -1));

    unsigned int count = hb_buffer_get_length(buf);
    LOGI("HB: shaped glyph count=%u", count);
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, NULL);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, NULL);

    float pen_x = origin_x;
    float pen_y = baseline_y;

    for (unsigned int i = 0; i < count; i++) {
        uint32_t gid = infos[i].codepoint;

        GlyphEntry* ge = glyph_cache_find(cache, gid);
        if (!ge) {
            ge = glyph_cache_insert(cache, gid);
            if (!ge) { hb_buffer_destroy(buf); return 0; }
            *ge = (GlyphEntry){0};
            ge->valid = true;
            ge->gid = gid;
            if (!rasterize_glyph(f, a, ge, gid)) { hb_buffer_destroy(buf); return 0; }
            a->uploaded = false; // atlas changed
        }

        float x_off = (float)pos[i].x_offset / 64.0f;
        float y_off = (float)pos[i].y_offset / 64.0f;
        float x_adv = (float)pos[i].x_advance / 64.0f;
        float y_adv = (float)pos[i].y_advance / 64.0f;

        // Place bitmap relative to baseline.
        // Coordinate convention: y grows downward on screen (we'll use ortho with top=0, bottom=height).
        float gx = pen_x + x_off + (float)ge->bearing_x;
        float gy = pen_y - y_off - (float)ge->bearing_y; // top-left of bitmap

        if (ge->w > 0 && ge->h > 0) {
            add_glyph_quad(out_vb,
                           gx, gy,
                           gx + (float)ge->w, gy + (float)ge->h,
                           ge->u0, ge->v0, ge->u1, ge->v1);
        }

        pen_x += x_adv;
        pen_y += y_adv;
    }

    hb_buffer_destroy(buf);
    return 1;
}

/* ---------------- EGL init/destroy ---------------- */

static bool init_egl(Renderer* r, ANativeWindow* window) {
    LOGI("EGL: init begin");

    r->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (r->display == EGL_NO_DISPLAY) { LOGE("EGL: eglGetDisplay failed"); egl_log_error("eglGetDisplay"); return false; }

    if (!eglInitialize(r->display, NULL, NULL)) { LOGE("EGL: eglInitialize failed"); egl_log_error("eglInitialize"); return false; }

    const EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(r->display, cfg_attr, &cfg, 1, &n) || n < 1) {
        LOGE("EGL: eglChooseConfig failed (n=%d)", (int)n);
        egl_log_error("eglChooseConfig");
        return false;
    }
    LOGI("EGL: chose config");

    EGLint vid = 0;
    eglGetConfigAttrib(r->display, cfg, EGL_NATIVE_VISUAL_ID, &vid);
    LOGI("EGL: native visual id=%d", (int)vid);

    ANativeWindow_setBuffersGeometry(window, 0, 0, vid);

    r->surface = eglCreateWindowSurface(r->display, cfg, window, NULL);
    if (r->surface == EGL_NO_SURFACE) { LOGE("EGL: eglCreateWindowSurface failed"); egl_log_error("eglCreateWindowSurface"); return false; }

    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    r->context = eglCreateContext(r->display, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (r->context == EGL_NO_CONTEXT) { LOGE("EGL: eglCreateContext failed"); egl_log_error("eglCreateContext"); return false; }

    if (!eglMakeCurrent(r->display, r->surface, r->surface, r->context)) {
        LOGE("EGL: eglMakeCurrent failed");
        egl_log_error("eglMakeCurrent");
        return false;
    }

    eglQuerySurface(r->display, r->surface, EGL_WIDTH,  (EGLint*)&r->width);
    eglQuerySurface(r->display, r->surface, EGL_HEIGHT, (EGLint*)&r->height);
    LOGI("EGL: surface %dx%d", r->width, r->height);

    glViewport(0, 0, r->width, r->height);
    gl_check("glViewport");
    gl_log_info("EGL");

    r->initialized = true;
    LOGI("EGL: init ok");
    return true;
}
static void destroy_egl(Renderer* r) {
    if (r->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(r->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (r->context != EGL_NO_CONTEXT) eglDestroyContext(r->display, r->context);
        if (r->surface != EGL_NO_SURFACE) eglDestroySurface(r->display, r->surface);
        eglTerminate(r->display);
    }
    r->display = EGL_NO_DISPLAY;
    r->surface = EGL_NO_SURFACE;
    r->context = EGL_NO_CONTEXT;
    r->initialized = false;
}

/* ---------------- App state holding everything ---------------- */

typedef struct {
    Renderer r;

    // GL
    GLuint prog;
    GLint uMVP, uTex, uColor, uTranslate;

    // Font/Text
    bool font_ready;
    TextSystem ts;
    bool text_ready;
} App;

/* ---------------- GL init for text ---------------- */

static int gl_text_init(App* a) {
    const char* vs =
        "uniform mat4 uMVP;\n"
        "uniform vec2 uTranslate;\n"
        "attribute vec2 aPos;\n"
        "attribute vec2 aUV;\n"
        "varying vec2 vUV;\n"
        "void main(){\n"
            "vUV = aUV;\n"
            "vec2 p = aPos + uTranslate;\n"
            "gl_Position = uMVP * vec4(p,0.0,1.0);\n"
        "}\n";

    const char* fs =
        "precision mediump float;\n"
        "uniform sampler2D uTex;\n"
        "uniform vec4 uColor;\n"
        "varying vec2 vUV;\n"
        "void main(){ float a = texture2D(uTex, vUV).a; gl_FragColor = vec4(uColor.rgb, uColor.a*a); }\n";

    a->prog = link_program(vs, fs);
    if (!a->prog) return 0;

    a->uMVP   = glGetUniformLocation(a->prog, "uMVP");
    a->uTex   = glGetUniformLocation(a->prog, "uTex");
    a->uColor = glGetUniformLocation(a->prog, "uColor");
    a->uTranslate = glGetUniformLocation(a->prog, "uTranslate");
    //glGenBuffers(1, &a->vbo);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    //LOGI("GL: text program=%u vbo=%u", a->prog, a->vbo);
    gl_check("gl_text_init");
    return 1;
}

/* ---------------- Build everything needed for text ---------------- */
static int text_system_init(struct android_app* app, App* a) {
    LOGI("INIT: text_system_init begin");

    textsystem_init_empty(&a->ts);

    // 1) Copy font asset
    char font_path[512];
    LOGI("INIT: copy font asset...");
    if (!copy_asset_to_file(app, "Roboto-Regular.ttf", font_path, sizeof(font_path))) {
        LOGE("INIT: copy font failed");
        return 0;
    }

    // 2) Init font
    LOGI("INIT: font_init...");
    if (!font_init(&a->ts.font, font_path, 48)) {
        LOGE("INIT: font_init failed");
        return 0;
    }

    // 3) Init atlas + glyph cache
    LOGI("INIT: atlas_init...");
    atlas_init(&a->ts.atlas, 2048, 2048);
    memset(a->ts.glyphs, 0, sizeof(a->ts.glyphs));

    // 4) Create a few independent text objects
    TextObj* t0 = textsystem_add(&a->ts);
    textobj_set_pos(t0, 24.0f, 120.0f);
    textobj_set_color(t0, 1, 1, 1, 1);
    textobj_set_text(t0, "HarfBuzz + FreeType (GLES2)");

    TextObj* t1 = textsystem_add(&a->ts);
    textobj_set_pos(t1, 24.0f, 200.0f);
    textobj_set_color(t1, 0.7f, 0.9f, 1.0f, 1);
    textobj_set_text(t1, "Second label");

    TextObj* t2 = textsystem_add(&a->ts);
    textobj_set_pos(t2, 24.0f, 280.0f);
    textobj_set_color(t2, 1.0f, 0.8f, 0.6f, 1);
    textobj_set_text(t2, "Third label");
    
    TextObj* t3 = textsystem_add(&a->ts);
    textobj_set_pos(t3, 24.0f, 360.0f);
    textobj_set_color(t3, 0.9f, 1.0f, 0.7f, 1.0f);
    textobj_set_text(t3, "Fourth label");
    
    TextObj* t4 = textsystem_add(&a->ts);
    textobj_set_pos(t4, 24.0f, 440.0f);
    textobj_set_color(t4, 1.0f, 0.6f, 0.9f, 1.0f);
    textobj_set_text(t4, "Fifth label");

    a->text_ready = true;
    LOGI("INIT: text_system_init ok (texts=%d)", a->ts.count);
    return 1;
}
static void text_system_destroy(App* a) {
    if (a->text_ready) {
        textsystem_destroy(&a->ts);
        a->text_ready = false;
    }
}
static void textsystem_update(TextSystem* ts) {
    for (int i = 0; i < ts->count; i++) {
        TextObj* t = &ts->items[i];
        if (t->cpu_dirty) {
            t->cpu_dirty = false;
            if (!build_text_mesh(&ts->font, &ts->atlas, ts->glyphs,
                                 t->text, 0.0f, 0.0f, &t->mesh)) {
                LOGE("build_text_mesh failed for text[%d]", i);
            } else {
                t->gpu_dirty = true;
            }
        }

        if (t->gpu_dirty) {
            t->gpu_dirty = false;
            glBindBuffer(GL_ARRAY_BUFFER, t->vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(t->mesh.vcount * (int)sizeof(Vtx)),
                         t->mesh.v,
                         GL_DYNAMIC_DRAW);
        }
    }
}

/* ---------------- Render ---------------- */

static void render(App* a) {
    glClearColor(0.08f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!a->text_ready) return;

    // Rebuild meshes / upload per-text VBOs if dirty
    textsystem_update(&a->ts);

    // Upload atlas once if needed
    atlas_upload_if_needed(&a->ts.atlas);

    glUseProgram(a->prog);

    float mvp[16];
    mat4_ortho(mvp, 0.0f, (float)a->r.width, (float)a->r.height, 0.0f);
    glUniformMatrix4fv(a->uMVP, 1, GL_FALSE, mvp);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, a->ts.atlas.tex);
    glUniform1i(a->uTex, 0);

    for (int i = 0; i < a->ts.count; i++) {
        TextObj* t = &a->ts.items[i];

        glUniform4f(a->uColor, t->r, t->g, t->b, t->a);
        glUniform2f(a->uTranslate, t->x, t->baseline_y);
        
        glBindBuffer(GL_ARRAY_BUFFER, t->vbo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)offsetof(Vtx, x));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)offsetof(Vtx, u));

        glDrawArrays(GL_TRIANGLES, 0, t->mesh.vcount);
    }

    gl_check("render end");
}

/* ---------------- App commands ---------------- */

static void handle_cmd(struct android_app* app, int32_t cmd) {
    App* a = (App*)app->userData;

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window && !a->r.initialized) {
                if (!init_egl(&a->r, app->window)) {
                    LOGE("EGL init failed");
                    return;
                }
                if (!gl_text_init(a)) {
                    LOGE("GL text init failed");
                    return;
                }
                if (!text_system_init(app, a)) {
                    LOGE("Text system init failed (is Roboto-Regular.ttf in assets?)");
                    return;
                }
                LOGI("Ready");
            }
            break;

        case APP_CMD_TERM_WINDOW:
            // Window is going away; destroy GL/EGL + font resources tied to GL context
            text_system_destroy(a);
            if (a->prog) { glDeleteProgram(a->prog); a->prog = 0; }
            destroy_egl(&a->r);
            break;

        default:
            break;
    }
}

/* ---------------- Entry ---------------- */

void android_main(struct android_app* app) {
    App a;
    memset(&a, 0, sizeof(a));
    a.r.display = EGL_NO_DISPLAY;
    a.r.surface = EGL_NO_SURFACE;
    a.r.context = EGL_NO_CONTEXT;

    app->userData = &a;
    app->onAppCmd = handle_cmd;

    for (;;) {
        int events;
        struct android_poll_source* source;

        int timeout_ms = a.r.initialized ? 0 : -1;

        while (ALooper_pollOnce(timeout_ms, NULL, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                text_system_destroy(&a);
                if (a.prog) glDeleteProgram(a.prog);
                destroy_egl(&a.r);
                return;
            }
            timeout_ms = 0;
        }

        if (a.r.initialized) {
            render(&a);
            eglSwapBuffers(a.r.display, a.r.surface);
        }
    }
}