/* Fake JNIEnv/JavaVM for hosting the GOF2HD engine on Linux. */
#include "jni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

/* verify table indices match the JNI spec (offsets in the struct) */
#define OFF(x) offsetof(JNINativeInterface_, x)
_Static_assert(OFF(GetVersion) == 4 * sizeof(void*), "GetVersion idx");
_Static_assert(OFF(FindClass) == 6 * sizeof(void*), "FindClass idx");
_Static_assert(OFF(GetObjectClass) == 31 * sizeof(void*), "GetObjectClass idx");
_Static_assert(OFF(GetMethodID) == 33 * sizeof(void*), "GetMethodID idx");
_Static_assert(OFF(GetStaticMethodID) == 113 * sizeof(void*), "GetStaticMethodID idx");
_Static_assert(OFF(CallStaticIntMethod) == 129 * sizeof(void*), "CallStaticIntMethod idx");
_Static_assert(OFF(CallStaticVoidMethod) == 141 * sizeof(void*), "CallStaticVoidMethod idx");
_Static_assert(OFF(GetStringUTFChars) == 169 * sizeof(void*), "GetStringUTFChars idx");
_Static_assert(OFF(ReleaseStringUTFChars) == 170 * sizeof(void*), "ReleaseStringUTFChars idx");

int g_display_width = 640;
int g_display_height = 480;
int g_jni_verbose = 0;

static void jlog(const char* fmt, ...) {
    if (!g_jni_verbose) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* ---------- fake representations ---------- */

static fake_jstring* mk_string(const char* utf) {
    fake_jstring* s = calloc(1, sizeof(fake_jstring));
    s->utf = utf;
    return s;
}

jstring mk_jstring(const char* utf) { return (jstring)mk_string(utf); }

/* ---------- implemented JNIEnv functions ---------- */

static jint GetVersion(JNIEnv* env) { (void)env; return 0x00010006; } /* JNI_VERSION_1_6 */

static jclass FindClass(JNIEnv* env, const char* name) {
    (void)env;
    jlog("[jni] FindClass(%s)\n", name);
    if (!name) return NULL;
    fake_jclass* c = calloc(1, sizeof(fake_jclass));
    c->name = strdup(name);
    return (jclass)c;
}

static jstring NewStringUTF(JNIEnv* env, const char* utf) {
    (void)env;
    jlog("[jni] NewStringUTF(%s)\n", utf ? utf : "(null)");
    return (jstring)mk_string(utf ? utf : "");
}

static jint GetStringUTFLength(JNIEnv* env, jstring s) {
    (void)env;
    fake_jstring* f = (fake_jstring*)s;
    return f && f->utf ? (jint)strlen(f->utf) : 0;
}

static const char* GetStringUTFChars(JNIEnv* env, jstring s, jboolean* isCopy) {
    (void)env;
    /* The engine copies the string into its own buffers only when isCopy
       is TRUE (bionic returns TRUE here).  Return TRUE so the copy happens. */
    if (isCopy) *isCopy = 1;
    fake_jstring* f = (fake_jstring*)s;
    return f ? f->utf : "";
}

static void ReleaseStringUTFChars(JNIEnv* env, jstring s, const char* chars) {
    /* On Android this releases the copied chars, NOT the jstring object.
       The engine reuses the same jstring across several calls (setZIPPath,
       SetDirectories...), so we must NOT free the fake_jstring here. */
    (void)env; (void)s; (void)chars;
}

static jmethodID GetStaticMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    (void)env;
    fake_jclass* c = (fake_jclass*)clazz;
    jlog("[jni] GetStaticMethodID(%s, %s, %s)\n", c ? c->name : "?", name, sig);
    fake_jmethod* m = calloc(1, sizeof(fake_jmethod));
    m->class_name = c ? c->name : "";
    m->name = name;
    m->sig = sig;
    return (jmethodID)m;
}

static jmethodID GetMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    (void)env;
    fake_jclass* c = (fake_jclass*)clazz;
    jlog("[jni] GetMethodID(%s, %s, %s)\n", c ? c->name : "?", name, sig);
    fake_jmethod* m = calloc(1, sizeof(fake_jmethod));
    m->class_name = c ? c->name : "";
    m->name = name;
    m->sig = sig;
    return (jmethodID)m;
}

static jclass GetObjectClass(JNIEnv* env, jobject obj) {
    (void)env; (void)obj;
    jlog("[jni] GetObjectClass(obj)\n");
    fake_jclass* c = calloc(1, sizeof(fake_jclass));
    c->name = strdup("android/content/Context");
    return (jclass)c;
}

/* returns int for DeviceInfo.getDisplayWidth/getDisplayHeight/isPad */
static jint static_int_return(jclass clazz, jmethodID mid) {
    fake_jclass* c = (fake_jclass*)clazz;
    fake_jmethod* m = (fake_jmethod*)mid;
    if (c && c->name && strstr(c->name, "DeviceInfo")) {
        if (m && m->name && strcmp(m->name, "getDisplayWidth") == 0) return g_display_width;
        if (m && m->name && strcmp(m->name, "getDisplayHeight") == 0) return g_display_height;
        if (m && m->name && strcmp(m->name, "isPad") == 0) return 0;
    }
    return 0;
}

static void CallStaticVoidMethod(JNIEnv* env, jclass clazz, jmethodID mid, ...) {
    (void)env;
    fake_jclass* c = (fake_jclass*)clazz;
    fake_jmethod* m = (fake_jmethod*)mid;
    jlog("[jni] CallStaticVoidMethod(%s.%s)\n", c ? c->name : "?", m ? m->name : "?");
}

static jint CallStaticIntMethod(JNIEnv* env, jclass clazz, jmethodID mid, ...) {
    (void)env;
    return static_int_return(clazz, mid);
}

static jboolean CallStaticBooleanMethod(JNIEnv* env, jclass clazz, jmethodID mid, ...) {
    (void)env;
    return (jboolean)(static_int_return(clazz, mid) ? 1 : 0);
}

static jobject CallStaticObjectMethod(JNIEnv* env, jclass clazz, jmethodID mid, ...) {
    (void)env; (void)clazz; (void)mid;
    return NULL;
}

static jlong CallStaticLongMethod(JNIEnv* env, jclass clazz, jmethodID mid, ...) {
    (void)env; (void)clazz; (void)mid;
    return 0;
}

static jdouble CallStaticDoubleMethod(JNIEnv* env, jclass clazz, jmethodID mid, ...) {
    (void)env; (void)clazz; (void)mid;
    return 0.0;
}

static void CallVoidMethod(JNIEnv* env, jobject obj, jmethodID mid, ...) {
    (void)env; (void)obj;
    fake_jmethod* m = (fake_jmethod*)mid;
    jlog("[jni] CallVoidMethod(%s)\n", m ? m->name : "?");
}

static jint CallIntMethod(JNIEnv* env, jobject obj, jmethodID mid, ...) {
    (void)env; (void)obj; (void)mid;
    return 0;
}

static jobject CallObjectMethod(JNIEnv* env, jobject obj, jmethodID mid, ...) {
    (void)env; (void)obj; (void)mid;
    return NULL;
}

static jboolean CallBooleanMethod(JNIEnv* env, jobject obj, jmethodID mid, ...) {
    (void)env; (void)obj; (void)mid;
    return 0;
}

static jlong CallLongMethod(JNIEnv* env, jobject obj, jmethodID mid, ...) {
    (void)env; (void)obj; (void)mid;
    return 0;
}

static jdouble CallDoubleMethod(JNIEnv* env, jobject obj, jmethodID mid, ...) {
    (void)env; (void)obj; (void)mid;
    return 0.0;
}

static jint ExceptionClear(JNIEnv* env) { (void)env; return 0; }
static jint ExceptionDescribe(JNIEnv* env) { (void)env; return 0; }
static jboolean ExceptionCheck(JNIEnv* env) { (void)env; return 0; }
static jint Throw(JNIEnv* env, jthrowable t) { (void)env; (void)t; return 0; }
static jint ThrowNew(JNIEnv* env, jclass c, const char* m) { (void)env; (void)c; (void)m; return 0; }

/* generic default: log once per index, return 0 */
static uintptr_t jni_default(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3) {
    (void)a0; (void)a1; (void)a2; (void)a3;
    return 0;
}

static uintptr_t jni_default_v(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3) {
    (void)a0; (void)a1; (void)a2; (void)a3;
    return 0;
}

static const JNINativeInterface_ g_iface = {
    /* reserved */ NULL, NULL, NULL, NULL,
    /* GetVersion */ (void*)GetVersion,
    /* DefineClass */ (void*)jni_default,
    /* FindClass */ (void*)FindClass,
    /* FromReflectedMethod */ (void*)jni_default,
    /* FromReflectedField */ (void*)jni_default,
    /* ToReflectedMethod */ (void*)jni_default,
    /* GetSuperclass */ (void*)jni_default,
    /* IsAssignableFrom */ (void*)jni_default,
    /* ToReflectedField */ (void*)jni_default,
    /* Throw */ (void*)Throw,
    /* ThrowNew */ (void*)ThrowNew,
    /* ExceptionOccurred */ (void*)jni_default,
    /* ExceptionDescribe */ (void*)ExceptionDescribe,
    /* ExceptionClear */ (void*)ExceptionClear,
    /* FatalError */ (void*)jni_default,
    /* PushLocalFrame */ (void*)jni_default,
    /* PopLocalFrame */ (void*)jni_default,
    /* NewGlobalRef */ (void*)jni_default,
    /* DeleteGlobalRef */ (void*)jni_default_v,
    /* DeleteLocalRef */ (void*)jni_default_v,
    /* IsSameObject */ (void*)jni_default,
    /* NewLocalRef */ (void*)jni_default,
    /* EnsureLocalCapacity */ (void*)jni_default,
    /* AllocObject */ (void*)jni_default,
    /* NewObject */ (void*)jni_default,
    /* NewObjectV */ (void*)jni_default,
    /* NewObjectA */ (void*)jni_default,
    /* GetObjectClass */ (void*)GetObjectClass,
    /* IsInstanceOf */ (void*)jni_default,
    /* GetMethodID */ (void*)GetMethodID,
    /* CallObjectMethod */ (void*)CallObjectMethod,
    /* CallObjectMethodV */ (void*)CallObjectMethod,
    /* CallObjectMethodA */ (void*)CallObjectMethod,
    /* CallBooleanMethod */ (void*)CallBooleanMethod,
    /* CallBooleanMethodV */ (void*)CallBooleanMethod,
    /* CallBooleanMethodA */ (void*)CallBooleanMethod,
    /* CallByteMethod */ (void*)jni_default,
    /* CallByteMethodV */ (void*)jni_default,
    /* CallByteMethodA */ (void*)jni_default,
    /* CallCharMethod */ (void*)jni_default,
    /* CallCharMethodV */ (void*)jni_default,
    /* CallCharMethodA */ (void*)jni_default,
    /* CallShortMethod */ (void*)jni_default,
    /* CallShortMethodV */ (void*)jni_default,
    /* CallShortMethodA */ (void*)jni_default,
    /* CallIntMethod */ (void*)CallIntMethod,
    /* CallIntMethodV */ (void*)CallIntMethod,
    /* CallIntMethodA */ (void*)CallIntMethod,
    /* CallLongMethod */ (void*)CallLongMethod,
    /* CallLongMethodV */ (void*)CallLongMethod,
    /* CallLongMethodA */ (void*)CallLongMethod,
    /* CallFloatMethod */ (void*)jni_default,
    /* CallFloatMethodV */ (void*)jni_default,
    /* CallFloatMethodA */ (void*)jni_default,
    /* CallDoubleMethod */ (void*)CallDoubleMethod,
    /* CallDoubleMethodV */ (void*)CallDoubleMethod,
    /* CallDoubleMethodA */ (void*)CallDoubleMethod,
    /* CallVoidMethod */ (void*)CallVoidMethod,
    /* CallVoidMethodV */ (void*)CallVoidMethod,
    /* CallVoidMethodA */ (void*)CallVoidMethod,
    /* CallNonvirtualObjectMethod */ (void*)CallObjectMethod,
    /* CallNonvirtualObjectMethodV */ (void*)CallObjectMethod,
    /* CallNonvirtualObjectMethodA */ (void*)CallObjectMethod,
    /* CallNonvirtualBooleanMethod */ (void*)CallBooleanMethod,
    /* CallNonvirtualBooleanMethodV */ (void*)CallBooleanMethod,
    /* CallNonvirtualBooleanMethodA */ (void*)CallBooleanMethod,
    /* CallNonvirtualByteMethod */ (void*)jni_default,
    /* CallNonvirtualByteMethodV */ (void*)jni_default,
    /* CallNonvirtualByteMethodA */ (void*)jni_default,
    /* CallNonvirtualCharMethod */ (void*)jni_default,
    /* CallNonvirtualCharMethodV */ (void*)jni_default,
    /* CallNonvirtualCharMethodA */ (void*)jni_default,
    /* CallNonvirtualShortMethod */ (void*)jni_default,
    /* CallNonvirtualShortMethodV */ (void*)jni_default,
    /* CallNonvirtualShortMethodA */ (void*)jni_default,
    /* CallNonvirtualIntMethod */ (void*)CallIntMethod,
    /* CallNonvirtualIntMethodV */ (void*)CallIntMethod,
    /* CallNonvirtualIntMethodA */ (void*)CallIntMethod,
    /* CallNonvirtualLongMethod */ (void*)CallLongMethod,
    /* CallNonvirtualLongMethodV */ (void*)CallLongMethod,
    /* CallNonvirtualLongMethodA */ (void*)CallLongMethod,
    /* CallNonvirtualFloatMethod */ (void*)jni_default,
    /* CallNonvirtualFloatMethodV */ (void*)jni_default,
    /* CallNonvirtualFloatMethodA */ (void*)jni_default,
    /* CallNonvirtualDoubleMethod */ (void*)CallDoubleMethod,
    /* CallNonvirtualDoubleMethodV */ (void*)CallDoubleMethod,
    /* CallNonvirtualDoubleMethodA */ (void*)CallDoubleMethod,
    /* CallNonvirtualVoidMethod */ (void*)CallVoidMethod,
    /* CallNonvirtualVoidMethodV */ (void*)CallVoidMethod,
    /* CallNonvirtualVoidMethodA */ (void*)CallVoidMethod,
    /* GetFieldID */ (void*)jni_default,
    /* GetObjectField */ (void*)jni_default,
    /* GetBooleanField */ (void*)jni_default,
    /* GetByteField */ (void*)jni_default,
    /* GetCharField */ (void*)jni_default,
    /* GetShortField */ (void*)jni_default,
    /* GetIntField */ (void*)jni_default,
    /* GetLongField */ (void*)jni_default,
    /* GetFloatField */ (void*)jni_default,
    /* GetDoubleField */ (void*)jni_default,
    /* SetObjectField */ (void*)jni_default_v,
    /* SetBooleanField */ (void*)jni_default_v,
    /* SetByteField */ (void*)jni_default_v,
    /* SetCharField */ (void*)jni_default_v,
    /* SetShortField */ (void*)jni_default_v,
    /* SetIntField */ (void*)jni_default_v,
    /* SetLongField */ (void*)jni_default_v,
    /* SetFloatField */ (void*)jni_default_v,
    /* SetDoubleField */ (void*)jni_default_v,
    /* GetStaticMethodID */ (void*)GetStaticMethodID,
    /* CallStaticObjectMethod */ (void*)CallStaticObjectMethod,
    /* CallStaticObjectMethodV */ (void*)CallStaticObjectMethod,
    /* CallStaticObjectMethodA */ (void*)CallStaticObjectMethod,
    /* CallStaticBooleanMethod */ (void*)CallStaticBooleanMethod,
    /* CallStaticBooleanMethodV */ (void*)CallStaticBooleanMethod,
    /* CallStaticBooleanMethodA */ (void*)CallStaticBooleanMethod,
    /* CallStaticByteMethod */ (void*)jni_default,
    /* CallStaticByteMethodV */ (void*)jni_default,
    /* CallStaticByteMethodA */ (void*)jni_default,
    /* CallStaticCharMethod */ (void*)jni_default,
    /* CallStaticCharMethodV */ (void*)jni_default,
    /* CallStaticCharMethodA */ (void*)jni_default,
    /* CallStaticShortMethod */ (void*)jni_default,
    /* CallStaticShortMethodV */ (void*)jni_default,
    /* CallStaticShortMethodA */ (void*)jni_default,
    /* CallStaticIntMethod */ (void*)CallStaticIntMethod,
    /* CallStaticIntMethodV */ (void*)CallStaticIntMethod,
    /* CallStaticIntMethodA */ (void*)CallStaticIntMethod,
    /* CallStaticLongMethod */ (void*)CallStaticLongMethod,
    /* CallStaticLongMethodV */ (void*)CallStaticLongMethod,
    /* CallStaticLongMethodA */ (void*)CallStaticLongMethod,
    /* CallStaticFloatMethod */ (void*)jni_default,
    /* CallStaticFloatMethodV */ (void*)jni_default,
    /* CallStaticFloatMethodA */ (void*)jni_default,
    /* CallStaticDoubleMethod */ (void*)CallStaticDoubleMethod,
    /* CallStaticDoubleMethodV */ (void*)CallStaticDoubleMethod,
    /* CallStaticDoubleMethodA */ (void*)CallStaticDoubleMethod,
    /* CallStaticVoidMethod */ (void*)CallStaticVoidMethod,
    /* CallStaticVoidMethodV */ (void*)CallStaticVoidMethod,
    /* CallStaticVoidMethodA */ (void*)CallStaticVoidMethod,
    /* GetStaticFieldID */ (void*)jni_default,
    /* GetStaticObjectField */ (void*)jni_default,
    /* GetStaticBooleanField */ (void*)jni_default,
    /* GetStaticByteField */ (void*)jni_default,
    /* GetStaticCharField */ (void*)jni_default,
    /* GetStaticShortField */ (void*)jni_default,
    /* GetStaticIntField */ (void*)jni_default,
    /* GetStaticLongField */ (void*)jni_default,
    /* GetStaticFloatField */ (void*)jni_default,
    /* GetStaticDoubleField */ (void*)jni_default,
    /* SetStaticObjectField */ (void*)jni_default_v,
    /* SetStaticBooleanField */ (void*)jni_default_v,
    /* SetStaticByteField */ (void*)jni_default_v,
    /* SetStaticCharField */ (void*)jni_default_v,
    /* SetStaticShortField */ (void*)jni_default_v,
    /* SetStaticIntField */ (void*)jni_default_v,
    /* SetStaticLongField */ (void*)jni_default_v,
    /* SetStaticFloatField */ (void*)jni_default_v,
    /* SetStaticDoubleField */ (void*)jni_default_v,
    /* NewString */ (void*)jni_default,
    /* GetStringLength */ (void*)GetStringUTFLength,
    /* GetStringChars */ (void*)jni_default,
    /* ReleaseStringChars */ (void*)jni_default_v,
    /* NewStringUTF */ (void*)NewStringUTF,
    /* GetStringUTFLength */ (void*)GetStringUTFLength,
    /* GetStringUTFChars */ (void*)GetStringUTFChars,
    /* ReleaseStringUTFChars */ (void*)ReleaseStringUTFChars,
    /* GetArrayLength */ (void*)jni_default,
    /* NewObjectArray */ (void*)jni_default,
    /* GetObjectArrayElement */ (void*)jni_default,
    /* SetObjectArrayElement */ (void*)jni_default_v,
    /* NewBooleanArray */ (void*)jni_default,
    /* NewByteArray */ (void*)jni_default,
    /* NewCharArray */ (void*)jni_default,
    /* NewShortArray */ (void*)jni_default,
    /* NewIntArray */ (void*)jni_default,
    /* NewLongArray */ (void*)jni_default,
    /* NewFloatArray */ (void*)jni_default,
    /* NewDoubleArray */ (void*)jni_default,
    /* GetBooleanArrayElements */ (void*)jni_default,
    /* GetByteArrayElements */ (void*)jni_default,
    /* GetCharArrayElements */ (void*)jni_default,
    /* GetShortArrayElements */ (void*)jni_default,
    /* GetIntArrayElements */ (void*)jni_default,
    /* GetLongArrayElements */ (void*)jni_default,
    /* GetFloatArrayElements */ (void*)jni_default,
    /* GetDoubleArrayElements */ (void*)jni_default,
    /* ReleaseBooleanArrayElements */ (void*)jni_default_v,
    /* ReleaseByteArrayElements */ (void*)jni_default_v,
    /* ReleaseCharArrayElements */ (void*)jni_default_v,
    /* ReleaseShortArrayElements */ (void*)jni_default_v,
    /* ReleaseIntArrayElements */ (void*)jni_default_v,
    /* ReleaseLongArrayElements */ (void*)jni_default_v,
    /* ReleaseFloatArrayElements */ (void*)jni_default_v,
    /* ReleaseDoubleArrayElements */ (void*)jni_default_v,
    /* GetBooleanArrayRegion */ (void*)jni_default_v,
    /* GetByteArrayRegion */ (void*)jni_default_v,
    /* GetCharArrayRegion */ (void*)jni_default_v,
    /* GetShortArrayRegion */ (void*)jni_default_v,
    /* GetIntArrayRegion */ (void*)jni_default_v,
    /* GetLongArrayRegion */ (void*)jni_default_v,
    /* GetFloatArrayRegion */ (void*)jni_default_v,
    /* GetDoubleArrayRegion */ (void*)jni_default_v,
    /* SetBooleanArrayRegion */ (void*)jni_default_v,
    /* SetByteArrayRegion */ (void*)jni_default_v,
    /* SetCharArrayRegion */ (void*)jni_default_v,
    /* SetShortArrayRegion */ (void*)jni_default_v,
    /* SetIntArrayRegion */ (void*)jni_default_v,
    /* SetLongArrayRegion */ (void*)jni_default_v,
    /* SetFloatArrayRegion */ (void*)jni_default_v,
    /* SetDoubleArrayRegion */ (void*)jni_default_v,
    /* RegisterNatives */ (void*)jni_default,
    /* UnregisterNatives */ (void*)jni_default,
    /* MonitorEnter */ (void*)jni_default,
    /* MonitorExit */ (void*)jni_default,
    /* GetJavaVM */ (void*)jni_default,
    /* GetStringRegion */ (void*)jni_default_v,
    /* GetStringUTFRegion */ (void*)jni_default_v,
    /* GetPrimitiveArrayCritical */ (void*)jni_default,
    /* ReleasePrimitiveArrayCritical */ (void*)jni_default_v,
    /* GetStringCritical */ (void*)GetStringUTFChars,
    /* ReleaseStringCritical */ (void*)ReleaseStringUTFChars,
    /* NewWeakGlobalRef */ (void*)jni_default,
    /* DeleteWeakGlobalRef */ (void*)jni_default_v,
    /* ExceptionCheck */ (void*)ExceptionCheck,
    /* NewDirectByteBuffer */ (void*)jni_default,
    /* GetDirectBufferAddress */ (void*)jni_default,
    /* GetDirectBufferCapacity */ (void*)jni_default,
    /* GetObjectRefType */ (void*)jni_default,
};

JNIEnv g_env = { &g_iface };

/* ---------- JavaVM ---------- */

static jint GetEnv(JavaVM_* vm, void** penv, jint version) {
    (void)vm; (void)version;
    if (penv) *penv = &g_env;
    return 0;
}

static jint AttachCurrentThread(JavaVM_* vm, void** penv, void* args) {
    (void)vm; (void)args;
    if (penv) *penv = &g_env;
    return 0;
}

static jint DetachCurrentThread(JavaVM_* vm) { (void)vm; return 0; }
static jint DestroyJavaVM(JavaVM_* vm) { (void)vm; return 0; }

static const JNIInvokeInterface_ g_vm_iface = {
    NULL, NULL, NULL, NULL,
    (void*)DestroyJavaVM,
    (void*)AttachCurrentThread,
    /* index 6: some NDK builds expect GetEnv semantics here */
    (void*)AttachCurrentThread,
    /* index 7: standard GetEnv */
    (void*)GetEnv,
};

JavaVM g_vm = { &g_vm_iface };
