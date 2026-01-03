#include "javahack.hpp"
namespace JavaHack {
int get_status_bar_inset_top_px(struct android_app* app) {
    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);

    jobject activity = app->activity->clazz;

    // activity.getWindow()
    jclass activityCls = env->GetObjectClass(activity);
    jmethodID midGetWindow = env->GetMethodID(activityCls, "getWindow", "()Landroid/view/Window;");
    jobject windowObj = env->CallObjectMethod(activity, midGetWindow);
    if (!windowObj) return 0;

    // window.getDecorView()
    jclass windowCls = env->GetObjectClass(windowObj);
    jmethodID midGetDecorView = env->GetMethodID(windowCls, "getDecorView", "()Landroid/view/View;");
    jobject decorView = env->CallObjectMethod(windowObj, midGetDecorView);
    if (!decorView) return 0;

    // decorView.getRootWindowInsets()
    jclass viewCls = env->GetObjectClass(decorView);
    jmethodID midGetRootInsets = env->GetMethodID(viewCls, "getRootWindowInsets", "()Landroid/view/WindowInsets;");
    jobject insetsObj = env->CallObjectMethod(decorView, midGetRootInsets);
    if (!insetsObj) return 0;

    jclass insetsCls = env->GetObjectClass(insetsObj);

    // WindowInsets.Type.statusBars()
    jclass typeCls = env->FindClass("android/view/WindowInsets$Type");
    jmethodID midStatusBars = env->GetStaticMethodID(typeCls, "statusBars", "()I");
    jint statusMask = env->CallStaticIntMethod(typeCls, midStatusBars);

    // insets.getInsets(mask) -> Insets
    jmethodID midGetInsets = env->GetMethodID(insetsCls, "getInsets", "(I)Landroid/graphics/Insets;");
    jobject insetVals = env->CallObjectMethod(insetsObj, midGetInsets, statusMask);
    if (!insetVals) return 0;

    // Insets.top
    jclass insetValsCls = env->GetObjectClass(insetVals);
    jfieldID fidTop = env->GetFieldID(insetValsCls, "top", "I");
    jint topPx = env->GetIntField(insetVals, fidTop);

    return (int)topPx;
}
SBarInsets get_SBarInsets(struct android_app* app) {
    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);

    jobject activity = app->activity->clazz;

    // activity.getWindow()
    jclass activityCls = env->GetObjectClass(activity);
    jmethodID midGetWindow = env->GetMethodID(activityCls, "getWindow", "()Landroid/view/Window;");
    jobject windowObj = env->CallObjectMethod(activity, midGetWindow);
    if (!windowObj) return SBarInsets{};

    // window.getDecorView()
    jclass windowCls = env->GetObjectClass(windowObj);
    jmethodID midGetDecorView = env->GetMethodID(windowCls, "getDecorView", "()Landroid/view/View;");
    jobject decorView = env->CallObjectMethod(windowObj, midGetDecorView);
    if (!decorView) return SBarInsets{};

    // decorView.getRootWindowInsets()
    jclass viewCls = env->GetObjectClass(decorView);
    jmethodID midGetRootInsets = env->GetMethodID(viewCls, "getRootWindowInsets", "()Landroid/view/WindowInsets;");
    jobject insetsObj = env->CallObjectMethod(decorView, midGetRootInsets);
    if (!insetsObj) return SBarInsets{};

    jclass insetsCls = env->GetObjectClass(insetsObj);

    // WindowInsets.Type.statusBars()
    jclass typeCls = env->FindClass("android/view/WindowInsets$Type");
    jmethodID midStatusBars = env->GetStaticMethodID(typeCls, "statusBars", "()I");
    jint statusMask = env->CallStaticIntMethod(typeCls, midStatusBars);

    // insets.getInsets(mask) -> Insets
    jmethodID midGetInsets = env->GetMethodID(insetsCls, "getInsets", "(I)Landroid/graphics/Insets;");
    jobject insetVals = env->CallObjectMethod(insetsObj, midGetInsets, statusMask);
    if (!insetVals) return SBarInsets{};

    // Insets.top
    jclass insetValsCls = env->GetObjectClass(insetVals);
    jfieldID fidLeft = env->GetFieldID(insetValsCls, "left", "I");
    jfieldID fidTop = env->GetFieldID(insetValsCls, "top", "I");
    jfieldID fidRight = env->GetFieldID(insetValsCls, "right", "I");
    jfieldID fidBottom = env->GetFieldID(insetValsCls, "bottom", "I");
    SBarInsets i;
    i.left = static_cast<int32_t>(env->GetIntField(insetVals, fidLeft));
    i.top = static_cast<int32_t>(env->GetIntField(insetVals, fidTop));
    i.right = static_cast<int32_t>(env->GetIntField(insetVals, fidRight));
    i.bottom = static_cast<int32_t>(env->GetIntField(insetVals, fidBottom));
    env->DeleteLocalRef(insetValsCls);
    return i;
}

}
