
#include <android_native_app_glue.h>

#include <cstdint>

namespace JavaHack {

struct SBarInsets {
    int32_t left {};
    int32_t top {};
    int32_t right {};
    int32_t bottom {};
};
SBarInsets get_SBarInsets(struct android_app* app);
int get_status_bar_inset_top_px(struct android_app* app);

}