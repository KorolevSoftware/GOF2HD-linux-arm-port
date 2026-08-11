/*
 * engine_bridge.c — dynamic loading and JNI boundary for the game engine.
 */
#include "engine_bridge.h"
#include "host_config.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static EngineBridge* g_active_bridge;

static void* resolve_required(void* handle, const char* name) {
    void* symbol = dlsym(handle, name);
    if (!symbol)
        fprintf(stderr, "[host] missing symbol %s: %s\n", name, dlerror());
    return symbol;
}

static jclass bridge_class(void) {
    static fake_jclass cls = {
        "net/fishlabs/gof2hdallandroid2012/ToJNI"
    };
    return (jclass)&cls;
}

int engine_bridge_load(EngineBridge* bridge) {
    void* handle;
    if (!bridge) return -1;
    memset(bridge, 0, sizeof(*bridge));

    if (!dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libgcc_s: %s\n", dlerror());
    if (!dlopen("libstdc++.so.6", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libstdc++: %s\n", dlerror());
    if (!dlopen("libfmodex.so", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libfmodex: %s\n", dlerror());
    if (!dlopen("libfmodevent.so", RTLD_NOW | RTLD_GLOBAL))
        fprintf(stderr, "[host] warn: libfmodevent: %s\n", dlerror());

    handle = dlopen("libgof2hdaa.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "[host] cannot load libgof2hdaa.so: %s\n", dlerror());
        return -1;
    }
    bridge->handle = handle;

#define SYM(field, name) \
    do { \
        bridge->field = (typeof(bridge->field))resolve_required(handle, name); \
        if (!bridge->field) return -1; \
    } while (0)
    SYM(jni_on_load, "JNI_OnLoad");
    SYM(set_zip_path, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_setZIPPath");
    SYM(set_directories, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_SetDirectories");
    SYM(set_apk_path, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_setAPKPath");
    SYM(set_environment_variables, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_setEnvironmentVariables");
    SYM(set_country_code, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_setCountryCodeOfDevice");
    SYM(initialize, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_initialize");
    SYM(resize, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_resize");
    SYM(renderstep, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_renderstep");
    SYM(get_exit_flag, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_getExitFlag");
    SYM(get_logo_shown, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_getLogoShown");
    SYM(is_in_main_menu, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_isInMainMenu");
    SYM(get_screenshot_flag, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_getScreenshotFlag");
    SYM(reset_screenshot_flag, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_resetScreenshotFlag");
    SYM(handle_touch_event, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_handleTouchEvent");
    SYM(handle_accelerometer, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_handleAccelerometer");
#undef SYM

    bridge->backbutton = (typeof(bridge->backbutton))dlsym(
        handle, "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_BackButtonPressed");
    bridge->set_origami_super_club = (typeof(bridge->set_origami_super_club))dlsym(
        handle, "Java_net_fishlabs_gof2hdallandroid2012_GOF2HD2012_SetOrigamiSuperClub");
    return 0;
}

int engine_bridge_configure(EngineBridge* bridge,
                            const char* apk_path,
                            const char* obb_path,
                            const char* data_dir) {
    static int fake_context;
    static char origami[] = "0123456789ABCDEF0123456789ABCDEF";
    jstring apk;
    jstring obb;
    jstring data;

    if (!bridge || !bridge->handle) return -1;
    bridge->env = &g_env;
    bridge->cls = bridge_class();
    g_active_bridge = bridge;

    printf("[host] JNI_OnLoad...\n");
    if (bridge->jni_on_load(&g_vm) != 0)
        fprintf(stderr, "[host] warn: JNI_OnLoad != 0\n");

    apk = mk_jstring(apk_path);
    obb = mk_jstring(obb_path);
    data = mk_jstring(data_dir);

    printf("[host] setZIPPath(%s)\n", obb_path);
    bridge->set_zip_path(bridge->env, bridge->cls, obb);
    printf("[host] SetDirectories(%s, obbdir)\n", data_dir);
    bridge->set_directories(bridge->env, bridge->cls, data, obb);
    printf("[host] setAPKPath(%s)\n", apk_path);
    bridge->set_apk_path(bridge->env, bridge->cls, apk);
    printf("[host] setEnvironmentVariables(context)\n");
    bridge->set_environment_variables(bridge->env, bridge->cls,
                                       (jobject)&fake_context);
    printf("[host] setCountryCodeOfDevice(0)\n");
    bridge->set_country_code(bridge->env, bridge->cls, 0);

    if (bridge->set_origami_super_club) {
        printf("[host] setOrigamiSuperClub(...)\n");
        bridge->set_origami_super_club(bridge->env, bridge->cls,
                                       mk_jstring(origami));
    }
    return 0;
}

int engine_bridge_initialize(EngineBridge* bridge, int width, int height) {
    if (!bridge || !bridge->initialize) return -1;
    printf("[host] initialize(%d, %d)\n", width, height);
    bridge->initialize(bridge->env, bridge->cls, width, height);
    printf("[host] resize(%d, %d)\n", width, height);
    bridge->resize(bridge->env, bridge->cls, width, height);
    return 0;
}

void engine_bridge_render(EngineBridge* bridge, jlong now_ms) {
    if (bridge && bridge->renderstep)
        bridge->renderstep(bridge->env, bridge->cls, now_ms);
}

int engine_bridge_exit_flag(EngineBridge* bridge) {
    return bridge ? bridge->get_exit_flag(bridge->env, bridge->cls) : -1;
}

int engine_bridge_logo_shown(EngineBridge* bridge) {
    return bridge ? bridge->get_logo_shown(bridge->env, bridge->cls) : 0;
}

int engine_bridge_in_main_menu(EngineBridge* bridge) {
    return bridge ? bridge->is_in_main_menu(bridge->env, bridge->cls) : 0;
}

void engine_bridge_send_input(const WrawInputEvent* event) {
    EngineBridge* bridge = g_active_bridge;
    if (!bridge || !event) return;

    switch (event->kind) {
    case WRAW_EV_ACCEL:
        bridge->handle_accelerometer(bridge->env, bridge->cls,
                                     event->u.accel.x, event->u.accel.y,
                                     event->u.accel.z);
        break;
    case WRAW_EV_TOUCH:
        bridge->handle_touch_event(bridge->env, bridge->cls,
                                   event->u.touch.pid, event->u.touch.action,
                                   event->u.touch.x, event->u.touch.y);
        break;
    case WRAW_EV_BACK:
        if (bridge->backbutton)
            bridge->backbutton(bridge->env, bridge->cls);
        break;
    }
}

void engine_bridge_send_touch(const HostTouchEvent* event, void* context) {
    EngineBridge* bridge = (EngineBridge*)context;
    if (!bridge || !event) return;
    if (getenv(GOF_ENV_VERBOSE_JNI))
        fprintf(stderr, "[host] touch pid=%d act=%d x=%d y=%d\n",
                event->pid, event->action, event->x, event->y);
    bridge->handle_touch_event(bridge->env, bridge->cls,
                               event->pid, event->action,
                               event->x, event->y);
}
