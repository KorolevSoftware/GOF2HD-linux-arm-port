/*
 * Engine bridge.
 *
 * This is the only host module that knows the exported Android JNI entry
 * points of libgof2hdaa.so and the softfp accelerometer calling convention.
 */
#ifndef GOF2HD_ENGINE_BRIDGE_H
#define GOF2HD_ENGINE_BRIDGE_H

#include "jni.h"
#include "touch_fifo.h"
#include "wrap_overlay.h"

typedef struct EngineBridge {
    void* handle;
    JNIEnv* env;
    jclass cls;

    jint (*jni_on_load)(JavaVM_* vm);
    void (*set_zip_path)(JNIEnv*, jclass, jstring);
    void (*set_directories)(JNIEnv*, jclass, jstring, jstring);
    void (*set_apk_path)(JNIEnv*, jclass, jstring);
    void (*set_environment_variables)(JNIEnv*, jclass, jobject);
    void (*set_country_code)(JNIEnv*, jclass, jint);
    void (*initialize)(JNIEnv*, jclass, jint, jint);
    void (*resize)(JNIEnv*, jclass, jint, jint);
    void (*renderstep)(JNIEnv*, jclass, jlong);
    jint (*get_exit_flag)(JNIEnv*, jclass);
    jint (*get_logo_shown)(JNIEnv*, jclass);
    jint (*is_in_main_menu)(JNIEnv*, jclass);
    jint (*get_screenshot_flag)(JNIEnv*, jclass);
    void (*reset_screenshot_flag)(JNIEnv*, jclass);
    void (*handle_touch_event)(JNIEnv*, jclass, jint, jint, jint, jint);
    void (__attribute__((pcs("aapcs"))) *handle_accelerometer)
        (JNIEnv*, jclass, jfloat, jfloat, jfloat);
    void (*set_origami_super_club)(JNIEnv*, jclass, jstring);
    void (*backbutton)(JNIEnv*, jclass);
} EngineBridge;

int engine_bridge_load(EngineBridge* bridge);
int engine_bridge_configure(EngineBridge* bridge,
                            const char* apk_path,
                            const char* obb_path,
                            const char* data_dir);
int engine_bridge_initialize(EngineBridge* bridge, int width, int height);
void engine_bridge_render(EngineBridge* bridge, jlong now_ms);
int engine_bridge_exit_flag(EngineBridge* bridge);
int engine_bridge_logo_shown(EngineBridge* bridge);
int engine_bridge_in_main_menu(EngineBridge* bridge);

void engine_bridge_send_input(const WrawInputEvent* event);
void engine_bridge_send_touch(const HostTouchEvent* event, void* context);

#endif
