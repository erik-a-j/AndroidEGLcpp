// main.c

#include "math.hpp"
#include "assets.hpp"
#include "javahack.hpp"

#define TAG_NAMESPACE "Main"
#include "logging.hpp"

#include <jni.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstddef>
//#include <cmath>

#include "ui_renderer.hpp"
#include "text_renderer.hpp"

// Very small linear cache for demo.
#define GLYPH_CACHE_MAX 512
#define ATLAS_PAD 1

// Add these near the top (after logx::If/logx::Ef)
#include <cerrno>

static void gl_log_info(const char* label) {
    const char* vendor   = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version  = (const char*)glGetString(GL_VERSION);
    const char* slver    = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    logx::If("{}: GL_VENDOR={}", label, vendor ? vendor : "(null)");
    logx::If("{}: GL_RENDERER={}", label, renderer ? renderer : "(null)");
    logx::If("{}: GL_VERSION={}", label, version ? version : "(null)");
    logx::If("{}: GLSL={}", label, slver ? slver : "(null)");
}
static void gl_check(const char* where) {
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR) {
        logx::Ef("GL error at {}: 0x{}", where, (unsigned)e);
    }
}
static void egl_log_error(const char* where) {
    EGLint e = eglGetError();
    if (e != EGL_SUCCESS) logx::Ef("EGL error at {}: 0x{}", where, (unsigned)e);
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
    Assets::Manager asset_mgr;
    Renderer r;
    // Ui
    UiRenderer ui;
    bool ui_ready = false;
    // Font/Text
    TextRenderer text;
    TextRenderer::Handle activeText{-1};
    TextRenderer::Handle t0{}, t1{}, t2{}, t3{}, t4{};
    bool text_ready = false;
    
    App(android_app* app) : asset_mgr(app) {}
};

/* ---------------- EGL init/destroy ---------------- */
static bool init_egl(Renderer* r, ANativeWindow* window) {
    logx::I("EGL: init begin");

    r->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (r->display == EGL_NO_DISPLAY) { logx::E("EGL: eglGetDisplay failed"); egl_log_error("eglGetDisplay"); return false; }

    if (!eglInitialize(r->display, NULL, NULL)) { logx::E("EGL: eglInitialize failed"); egl_log_error("eglInitialize"); return false; }

    const EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, 
        EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(r->display, cfg_attr, &cfg, 1, &n) || n < 1) {
        logx::Ef("EGL: eglChooseConfig failed (n={})", (int)n);
        egl_log_error("eglChooseConfig");
        return false;
    }
    logx::I("EGL: chose config");

    EGLint vid = 0;
    eglGetConfigAttrib(r->display, cfg, EGL_NATIVE_VISUAL_ID, &vid);
    logx::If("EGL: native visual id={}", (int)vid);

    ANativeWindow_setBuffersGeometry(window, 0, 0, vid);

    r->surface = eglCreateWindowSurface(r->display, cfg, window, NULL);
    if (r->surface == EGL_NO_SURFACE) { logx::E("EGL: eglCreateWindowSurface failed"); egl_log_error("eglCreateWindowSurface"); return false; }

    const EGLint ctx_attr[] = { 
        EGL_CONTEXT_CLIENT_VERSION,
        3,
        EGL_NONE,
    };
    r->context = eglCreateContext(r->display, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (r->context == EGL_NO_CONTEXT) { logx::E("EGL: eglCreateContext failed"); egl_log_error("eglCreateContext"); return false; }

    if (!eglMakeCurrent(r->display, r->surface, r->surface, r->context)) {
        logx::E("EGL: eglMakeCurrent failed");
        egl_log_error("eglMakeCurrent");
        return false;
    }

    eglQuerySurface(r->display, r->surface, EGL_WIDTH,  (EGLint*)&r->width);
    eglQuerySurface(r->display, r->surface, EGL_HEIGHT, (EGLint*)&r->height);
    logx::If("EGL: surface {}x{}", r->width, r->height);

    glViewport(0, 0, r->width, r->height);
    gl_check("glViewport");
    gl_log_info("EGL");

    r->initialized = true;
    logx::I("EGL: init ok");
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
    if (!a->ui.init(a->asset_mgr)) { 
        logx::E("ui.init failed"); 
        return false; 
    }
    JavaHack::SBarInsets sbar_i = JavaHack::get_SBarInsets(app);
    logx::If("left: {}, top: {}, right: {}, bottom: {}", sbar_i.left, sbar_i.top, sbar_i.right, sbar_i.bottom);
    int32_t sbar_h = sbar_i.top;
    a->r.insets.status_bar_height = sbar_h;
    
    UiRenderer::Handle sbar = a->ui.createObj();
    a->ui.objClear(sbar);
    a->ui.objRectFilled(sbar, 0, 0, a->r.width, sbar_h, {0x1a, 0x1f, 0x1a, 0xff}, 8.0f, 1.0f);
    //a->ui.objLine(sbar, 0, sbar_h, a->r.width, sbar_h, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    //a->ui.objLine(sbar, 0, sbar_h, a->r.width, sbar_h, 3.0f, {1, 0, 0, 1});
    UiRenderer::Handle test = a->ui.createObj();
    a->ui.objClear(test);
    a->ui.objRectFilled(test, a->r.width / 2, a->r.height / 2, 500, 500, {0x1a, 0x1f, 0xff, 0xff}, 20.0f, 1.0f);
    
    a->ui_ready = true;
    return true;
}
static bool init_text(struct android_app* app) {
    App* a = (App*)app->userData;
    if (!a->text.init(a->asset_mgr, "Roboto-Regular.ttf", 48, 2048, 2048)) {
        logx::E("a->text.init failed");
        return false;
    }
    a->t0 = a->text.createText();
    a->text.setPos(a->t0, 500.0f, 1500.0f);
    a->text.setColor(a->t0, {255,255,255,255});
    a->text.setText(a->t0, "Elin luktar bajs");

    /*a->t1 = a->text.createText();
    a->text.setPos(a->t1, 24.0f, 200.0f);
    a->text.setColor(a->t1, {100, 150, 255, 255});
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
                              {0x1f, 0x2a, 0x1f, 0xff},
                              {0x1a, 0x1f, 0x1a, 0xff});
        
        if (a->text_ready && a->activeText.id != -1) {
            auto si = a->text.getSelectionInfo(a->activeText);
            if (si.valid && si.hasSelection) {
                // Draw selection behind text
                a->ui.rectFilled(si.selX0, si.selY0,
                                 si.selX1 - si.selX0, si.selY1 - si.selY0,
                                 {0x30, 0x80, 0xff, 0x80}, 4.0f, 1.0f);
            }
    
            // Optional caret drawing (thin rect)
            if (si.valid) {
                float cx = a->text.getSelectionInfo(a->activeText).x0; // not needed, see below
            }
        }

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
                    logx::E("init_egl failed");
                    return;
                }
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                
                if (!init_ui(app)) {
                    logx::E("init_ui failed");
                    return;
                }
                
                if (!init_text(app)) {
                    logx::E("init_text failed");
                    return;
                }
                
                //logx::If("status-bar: %d", a->r.insets.status_bar_height);
                logx::I("Ready");
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
static int32_t handle_input(android_app* app, AInputEvent* event) {
    App* a = (App*)app->userData;

    int type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);
    
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            a->activeText = a->text.hitTest(x, y);
            if (a->activeText.id != -1) {
                a->text.beginSelection(a->activeText, x, y);
                return 1;
            }
        } else if (action == AMOTION_EVENT_ACTION_MOVE) {
            if (a->activeText.id != -1) {
                a->text.updateSelection(a->activeText, x, y);
                return 1;
            }
        } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
            if (a->activeText.id != -1) {
                a->text.endSelection(a->activeText);
                // keep activeText if you want caret to remain focused; or clear it:
                // a->activeText = {-1};
                return 1;
            }
        }
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int key = AKeyEvent_getKeyCode(event);
        int action = AKeyEvent_getAction(event);

        // Handle keyboard here
        return 1;
    }

    return 0; // not consumed
}

/* ---------------- Entry ---------------- */
void android_main(struct android_app* app) {
    App a{app};
    app->userData = &a;
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

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
