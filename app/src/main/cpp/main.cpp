// main.c
// Minimal NativeActivity (android_native_app_glue) + EGL/GLES2 + HarfBuzz shaping + FreeType rasterization
// - Put a TTF in: app/src/main/assets/Roboto-Regular.ttf
// - Build with CMake linking freetype + harfbuzz (see CMake snippet after this file)

#include "config.h"

#include "math.hpp"
#include "javahack.hpp"
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <EGL/egl.h>
#if GLES_VERSION == 3
#include <GLES3/gl3.h>
#else
#include <GLES2/gl2.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstddef>
//#include <cmath>

#include "ui_renderer.hpp"
#include "text_renderer.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "HBFT", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "HBFT", __VA_ARGS__)

// Very small linear cache for demo.
#define GLYPH_CACHE_MAX 512
#define ATLAS_PAD 1

// Add these near the top (after LOGI/LOGE)
#include <cerrno>

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

/* ---------------- EGL / App ---------------- */

struct Renderer {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int32_t width = 0, height = 0;
    struct Insets {
        int32_t status_bar_height = 0;
    } insets;
    bool initialized = false;
};

struct App {
    Renderer r;
    // Ui
    UiRenderer ui;
    bool ui_ready = false;
    // Font/Text
    TextRenderer text;
    TextRenderer::Handle t0{}, t1{}, t2{}, t3{}, t4{};
    bool text_ready = false;
};

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

/* ---------------- EGL init/destroy ---------------- */

static bool init_egl(Renderer* r, ANativeWindow* window) {
    LOGI("EGL: init begin");

    r->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (r->display == EGL_NO_DISPLAY) { LOGE("EGL: eglGetDisplay failed"); egl_log_error("eglGetDisplay"); return false; }

    if (!eglInitialize(r->display, NULL, NULL)) { LOGE("EGL: eglInitialize failed"); egl_log_error("eglInitialize"); return false; }

    const EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, 
        #if GLES_VERSION == 3
        EGL_OPENGL_ES3_BIT,
        #else
        EGL_OPENGL_ES2_BIT,
        #endif
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

    const EGLint ctx_attr[] = { 
        EGL_CONTEXT_CLIENT_VERSION,
        #if GLES_VERSION == 3
        3,
        #else
        2,
        #endif
        EGL_NONE,
    };
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

static bool init_ui(struct android_app* app) {
    App* a = (App*)app->userData;
    if (!a->ui.init()) { 
        LOGE("ui.init failed"); 
        return false; 
    }
    JavaHack::SBarInsets sbar_i = JavaHack::get_SBarInsets(app);
    LOGI("left: %d, top: %d, right: %d, bottom: %d", sbar_i.left, sbar_i.top, sbar_i.right, sbar_i.bottom);
    int32_t sbar_h = sbar_i.top;
    a->r.insets.status_bar_height = sbar_h;
    
    UiRenderer::Handle sbar = a->ui.createObj();
    a->ui.objClear(sbar);
    a->ui.objRectFilled(sbar, 0, 0, a->r.width, sbar_h, {0.1f, 0.12f, 0.1f, 1});
    //a->ui.objLine(sbar, 0, sbar_h, a->r.width, sbar_h, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    //a->ui.objLine(sbar, 0, sbar_h, a->r.width, sbar_h, 3.0f, {1, 0, 0, 1});
    
    a->ui_ready = true;
    return true;
}
static bool init_text(struct android_app* app) {
    App* a = (App*)app->userData;
    char font_path[512];
    if (!copy_asset_to_file(app, "Roboto-Regular.ttf", font_path, sizeof(font_path))) {
        LOGE("copy_asset_to_file failed");
        return false;
    }
    if (!a->text.init(font_path, 48, 2048, 2048)) {
        LOGE("a->text.init failed");
        return false;
    }
    /*a->t0 = a->text.createText();
    a->text.setPos(a->t0, 24.0f, 120.0f);
    a->text.setColor(a->t0, 1,1,1,1);
    a->text.setText(a->t0, "HarfBuzz + FreeType (GLES2)");

    a->t1 = a->text.createText();
    a->text.setPos(a->t1, 24.0f, 200.0f);
    a->text.setColor(a->t1, 0.7f, 0.9f, 1.0f, 1);
    a->text.setText(a->t1, "Second label");
*/
    // ... t2/t3/t4

    a->text_ready = true;
    return true;
}

/* ---------------- Render ---------------- */

static void render(App* a) {
    glClearColor(0.08f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    Mat4 mvp = Mat4::ortho(0.0f, (float)a->r.width, (float)a->r.height, 0.0f);
    
    if (a->ui_ready) {
        a->ui.begin();
        
        a->ui.rectFilledVGrad(0, 0, a->r.width, a->r.height,
                              {0.16f, 0.18f, 0.16f, 1},
                              {0.1f, 0.12f, 0.1f, 1});
        /*a->ui.rectFilled4Grad(0, 0, a->r.width, a->r.height, 
                              {0.12f, 0.14f, 1, 1},
                              {0.12f, 1, 0.12f, 1},
                              {0.5f, 0.5f, 0, 1},
                              {1, 0.14f, 0.12f, 1});*/
        //a->ui.rectOutline(16, 80, 600, 420, 2.0f, 1,1,1,0.25f);
        a->ui.end();
        a->ui.draw(mvp.data());
        a->ui.drawObjects(mvp.data());
    }

    // Text
    if (a->text_ready) {
        a->text.update();
        a->text.draw(mvp.data());
    }

    gl_check("render end");
}

/* ---------------- App commands ---------------- */

void destroy_app(App* a) {
    a->ui.shutdown();
    a->ui_ready = false;
    a->text.shutdown();
    a->text_ready = false;
    destroy_egl(&a->r);
}

static void handle_cmd(struct android_app* app, int32_t cmd) {
    App* a = (App*)app->userData;

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window && !a->r.initialized) {
                if (!init_egl(&a->r, app->window)) {
                    LOGE("init_egl failed");
                    return;
                }
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                
                if (!init_ui(app)) {
                    LOGE("init_ui failed");
                    return;
                }
                
                if (!init_text(app)) {
                    LOGE("init_text failed");
                    return;
                }
                
                //LOGI("status-bar: %d", a->r.insets.status_bar_height);
                LOGI("Ready");
            }
            break;

        case APP_CMD_TERM_WINDOW:
            // Window is going away; destroy GL/EGL + font resources tied to GL context
            destroy_app(a);
            break;

        default:
            break;
    }
}

/* ---------------- Entry ---------------- */

void android_main(struct android_app* app) {
    App a{};
    app->userData = &a;
    app->onAppCmd = handle_cmd;

    for (;;) {
        int events;
        struct android_poll_source* source;

        int timeout_ms = a.r.initialized ? 0 : -1;

        while (ALooper_pollOnce(timeout_ms, NULL, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                if (a.r.initialized) {
                    destroy_app(&a);
                }
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