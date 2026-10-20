/* Purchase restoration needs the main menu and must not repeat callbacks. */
#include "../host/engine_bridge.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static int in_menu, frames, bought[5], calls[5];
static int credit_calls[5], rendering, request_in_frame;
static JNIEnv env;
static fake_jclass cls;
static const char* const credit_methods[] = {
    "iap_buy_credits_100_000", "iap_buy_credits_300_000",
    "iap_buy_credits_1_000_000", "iap_buy_credits_3_000_000",
    "iap_buy_credits_10_000_000"
};

static void request_credits(int index) {
    fake_jclass caller = {"net/fishlabs/gof2hdallandroid2012/NativeFunctionCalls"};
    jmethodID mid = ((jmethodID (*)(JNIEnv*, jclass, const char*, const char*))
        g_env.functions->GetStaticMethodID)(
            &g_env, (jclass)&caller, credit_methods[index], "()V");
    ((void (*)(JNIEnv*, jclass, jmethodID, ...))g_env.functions->CallStaticVoidMethod)(
        &g_env, (jclass)&caller, mid);
    free(mid);
}

static void render(JNIEnv* e, jclass c, jlong now) {
    assert(e == &env && c == (jclass)&cls && now == 123);
    rendering = 1;
    if (request_in_frame) {
        request_credits(request_in_frame - 1);
        request_in_frame = 0;
    }
    ++frames;
    rendering = 0;
}

static jint menu(JNIEnv* e, jclass c) {
    assert(e == &env && c == (jclass)&cls && frames > 0);
    return in_menu;
}

#define DLC_GETTER(n) \
    static jint get_dlc_##n(JNIEnv* e, jclass c) { \
        assert(e == &env && c == (jclass)&cls && in_menu); \
        return bought[n]; \
    }
DLC_GETTER(0)
DLC_GETTER(1)
DLC_GETTER(2)
DLC_GETTER(3)
DLC_GETTER(4)

static void purchase(JNIEnv* e, jclass c, jint index, jint success) {
    assert(e == &env && c == (jclass)&cls && in_menu);
    assert(index >= 0 && index < 5 && success == 1);
    assert(!bought[index]);
    bought[index] = 1;
    ++calls[index];
}

static void credit_purchase(JNIEnv* e, jclass c, jint index) {
    assert(e == &env && c == (jclass)&cls && !rendering && frames > 0);
    assert(index >= 0 && index < 5);
    ++credit_calls[index];
}

static const char* array_text(JNIEnv* e, jarray a, jint i) {
    jobject value = ((jobject (*)(JNIEnv*, jarray, jint))
        e->functions->GetObjectArrayElement)(e, a, i);
    return ((const char* (*)(JNIEnv*, jstring, jboolean*))
        e->functions->GetStringUTFChars)(e, value, NULL);
}

static jstring sku(JNIEnv* e, jclass c, jint i) {
    assert(e == &g_env && c == (jclass)&cls && i >= 0 && i < 5);
    return mk_jstring(credit_methods[i]);
}

static int catalog_calls;
static void catalog(JNIEnv* e, jclass c, jarray ids, jarray names,
                    jarray desc, jarray currency, jarray prices) {
    static const char* const amounts[] = {
        "100.000 $", "300.000 $", "1.000.000 $", "3.000.000 $", "10.000.000 $"
    };
    assert(e == &g_env && c == (jclass)&cls);
    jarray arrays[] = {ids, names, desc, currency, prices};
    for (int i = 0; i < 5; ++i) {
        assert(((jint (*)(JNIEnv*, jarray))e->functions->GetArrayLength)(e, arrays[i]) == 5);
        assert(strcmp(array_text(e, ids, i), credit_methods[i]) == 0);
        assert(strcmp(array_text(e, names, i), amounts[i]) == 0);
        assert(strcmp(array_text(e, desc, i), amounts[i]) == 0);
        assert(strcmp(array_text(e, currency, i), "") == 0);
        assert(strcmp(array_text(e, prices, i), "0") == 0);
    }
    assert(((jobject (*)(JNIEnv*, jarray, jint))e->functions->GetObjectArrayElement)(e, ids, -1) == NULL);
    assert(((jobject (*)(JNIEnv*, jarray, jint))e->functions->GetObjectArrayElement)(e, ids, 5) == NULL);
    ++catalog_calls;
}

static void call_void_v(jclass c, jmethodID mid, ...) {
    va_list args;
    va_start(args, mid);
    ((void (*)(JNIEnv*, jclass, jmethodID, va_list))g_env.functions->CallStaticVoidMethodV)(
        &g_env, c, mid, args);
    va_end(args);
}

int main(void) {
    EngineBridge bridge = {
        .env = &env, .cls = (jclass)&cls,
        .renderstep = render, .is_in_main_menu = menu,
        .iap_bought_premium = purchase,
        .iap_bought_consumable = credit_purchase,
        .get_dlc_bought = {get_dlc_0, get_dlc_1, get_dlc_2, get_dlc_3, get_dlc_4}
    };
    engine_bridge_render(NULL, 123);
    EngineBridge empty = {0};
    engine_bridge_render(&empty, 123);
    assert(frames == 0);

    engine_bridge_render(&bridge, 123);
    assert(frames == 1 && !bridge.purchases_restored);
    for (int i = 0; i < 5; ++i) assert(calls[i] == 0);

    bought[1] = 1; /* Existing Kaamo Club ownership must not be granted again. */
    in_menu = 1;
    engine_bridge_render(&bridge, 123);
    assert(bridge.purchases_restored);
    for (int i = 0; i < 5; ++i) {
        assert(bought[i] == 1);
        assert(calls[i] == (i != 1));
    }
    engine_bridge_render(&bridge, 123);
    in_menu = 0;
    engine_bridge_render(&bridge, 123);
    in_menu = 1;
    engine_bridge_render(&bridge, 123);
    for (int i = 0; i < 5; ++i) assert(calls[i] == (i != 1));

    bridge.purchases_restored = 0; /* Restart with all purchases already owned. */
    engine_bridge_render(&bridge, 123);
    assert(bridge.purchases_restored);
    for (int i = 0; i < 5; ++i) assert(calls[i] == (i != 1));
    puts("PASS: restore five DLCs after startup, preserve ownership, no repeat grants");

    EngineBridge store = {
        .env = &g_env, .cls = (jclass)&cls,
        .get_consumable_sku = sku, .set_item_information = catalog
    };
    configure_credit_store(&store);
    assert(catalog_calls == 1);
    assert(jni_take_credit_purchase() == -1);

    /* Each real store request must grant exactly one pack, including repeats. */
    in_menu = 0;
    for (int i = 0; i < 5; ++i) {
        request_in_frame = i + 1;
        engine_bridge_render(&bridge, 123);
        assert(credit_calls[i] == 1);
        engine_bridge_render(&bridge, 123);
        assert(credit_calls[i] == 1);
    }
    request_credits(4);
    request_credits(4);
    request_credits(1);
    for (int i = 0; i < 4; ++i) engine_bridge_render(&bridge, 123);
    assert(credit_calls[4] == 3 && credit_calls[1] == 2);

    fake_jmethod method = {
        "net/fishlabs/gof2hdallandroid2012/NativeFunctionCalls", credit_methods[0], "()V"
    };
    call_void_v((jclass)&cls, (jmethodID)&method);
    ((void (*)(JNIEnv*, jclass, jmethodID, const void*))g_env.functions->CallStaticVoidMethodA)(
        &g_env, (jclass)&cls, (jmethodID)&method, NULL);
    for (int i = 0; i < 3; ++i) engine_bridge_render(&bridge, 123);
    assert(credit_calls[0] == 3);

    method.class_name = "another/Class";
    call_void_v((jclass)&cls, (jmethodID)&method);
    method.class_name = "net/fishlabs/gof2hdallandroid2012/NativeFunctionCalls";
    method.sig = "(I)V";
    call_void_v((jclass)&cls, (jmethodID)&method);
    method.sig = "()V";
    method.name = "iap_restore_purchases";
    call_void_v((jclass)&cls, (jmethodID)&method);
    call_void_v((jclass)&cls, NULL);
    assert(jni_take_credit_purchase() == -1);
    puts("PASS: free credit catalog, five pack IDs, deferred/repeated purchases, JNI variants");
    return 0;
}
