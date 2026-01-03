#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "MinimalEGL", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MinimalEGL", __VA_ARGS__)

typedef struct {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int32_t width;
    int32_t height;
    bool initialized;
} Renderer;

static bool init_egl(Renderer* r, ANativeWindow* window) {
    r->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (r->display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }

    if (!eglInitialize(r->display, NULL, NULL)) {
        LOGE("eglInitialize failed");
        return false;
    }

    // Request an ES2-compatible config
    const EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        8,
        EGL_DEPTH_SIZE,
        0,
        EGL_STENCIL_SIZE,
        0,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs = 0;
    if (!eglChooseConfig(r->display, config_attribs, &config, 1, &num_configs) || num_configs < 1) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    // Match the window buffer format to EGL config
    EGLint native_visual_id = 0;
    eglGetConfigAttrib(r->display, config, EGL_NATIVE_VISUAL_ID, &native_visual_id);
    ANativeWindow_setBuffersGeometry(window, 0, 0, native_visual_id);

    r->surface = eglCreateWindowSurface(r->display, config, window, NULL);
    if (r->surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed");
        return false;
    }

    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION,
        2,
        EGL_NONE
    };
    r->context = eglCreateContext(r->display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (r->context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed");
        return false;
    }

    if (!eglMakeCurrent(r->display, r->surface, r->surface, r->context)) {
        LOGE("eglMakeCurrent failed");
        return false;
    }

    eglQuerySurface(r->display, r->surface, EGL_WIDTH, (EGLint*)&r->width);
    eglQuerySurface(r->display, r->surface, EGL_HEIGHT, (EGLint*)&r->height);

    glViewport(0, 0, r->width, r->height);

    r->initialized = true;
    LOGI("EGL initialized: %dx%d", r->width, r->height);
    return true;
}
static void destroy_egl(Renderer* r) {
    if (r->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(r->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (r->context != EGL_NO_CONTEXT) {
            eglDestroyContext(r->display, r->context);
            r->context = EGL_NO_CONTEXT;
        }
        if (r->surface != EGL_NO_SURFACE) {
            eglDestroySurface(r->display, r->surface);
            r->surface = EGL_NO_SURFACE;
        }
        eglTerminate(r->display);
        r->display = EGL_NO_DISPLAY;
    }
    r->initialized = false;
}

static void render_frame(Renderer* r, float t) {
    // Simple animated clear color
    float g = 0.5f + 0.5f * (float) (0.5f * (1.0f + sinf(t))); // needs math
    glClearColor(0.1f, g, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    eglSwapBuffers(r->display, r->surface);
}

// Minimal event handler: init on window, destroy on terminate
static void handle_cmd(struct android_app* app, int32_t cmd) {
    Renderer* r = (Renderer*)app->userData;

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
        if (app->window != NULL && !r->initialized) {
            init_egl(r, app->window);
        }
        break;

        case APP_CMD_TERM_WINDOW:
        // Window is going away (e.g., app in background)
        destroy_egl(r);
        break;

        default:
        break;
    }
}


void android_main(struct android_app* app) {
    // Required by native_app_glue to set up JNI/env
    //app_dummy();
    
    Renderer r;
    memset(&r, 0, sizeof(r));
    r.display = EGL_NO_DISPLAY;
    r.surface = EGL_NO_SURFACE;
    r.context = EGL_NO_CONTEXT;

    app->userData = &r;
    app->onAppCmd = handle_cmd;

    // Main loop
    float t = 0.0f;
    for (;;) {
        int events;
        struct android_poll_source* source;

        // If not initialized, block for events until we get a window.
        // If initialized, poll with timeout=0 to render continuously.
        int timeout_ms = r.initialized ? 0: -1;

        while (ALooper_pollOnce(timeout_ms, NULL, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);

            if (app->destroyRequested) {
                destroy_egl(&r);
                return;
            }

            // After the first poll in this frame, don't block again.
            timeout_ms = 0;
        }

        if (r.initialized) {
            render_frame(&r, t);
            t += 0.02f;
        }
    }
}
