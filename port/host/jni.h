/* Minimal JNI ABI for hosting a bionic-linked native engine on Linux. */
#ifndef GOAF_JNI_H
#define GOAF_JNI_H

#include <stdint.h>
#include <stddef.h>

typedef int32_t jint;
typedef int64_t jlong;
typedef int8_t  jbyte;
typedef uint8_t jboolean;
typedef uint16_t jchar;
typedef double jdouble;
typedef float jfloat;
typedef int16_t jshort;

typedef struct _jobject {} *jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jthrowable;
typedef jobject jweak;
typedef jobject jarray;
typedef jarray jbooleanArray;
typedef jarray jbyteArray;
typedef jarray jcharArray;
typedef jarray jshortArray;
typedef jarray jintArray;
typedef jarray jlongArray;
typedef jarray jfloatArray;
typedef jarray jdoubleArray;
typedef jobject jmethodID;
typedef jobject jfieldID;

typedef struct JNIEnv_ JNIEnv;

/* function table, positions follow the JNI spec */
typedef struct JNINativeInterface_ {
    void* reserved0; void* reserved1; void* reserved2; void* reserved3;
    void* GetVersion; void* DefineClass; void* FindClass; void* FromReflectedMethod;
    void* FromReflectedField; void* ToReflectedMethod; void* GetSuperclass;
    void* IsAssignableFrom; void* ToReflectedField; void* Throw; void* ThrowNew;
    void* ExceptionOccurred; void* ExceptionDescribe; void* ExceptionClear;
    void* FatalError; void* PushLocalFrame; void* PopLocalFrame; void* NewGlobalRef;
    void* DeleteGlobalRef; void* DeleteLocalRef; void* IsSameObject; void* NewLocalRef;
    void* EnsureLocalCapacity; void* AllocObject; void* NewObject; void* NewObjectV;
    void* NewObjectA; void* GetObjectClass; void* IsInstanceOf; void* GetMethodID;
    void* CallObjectMethod; void* CallObjectMethodV; void* CallObjectMethodA;
    void* CallBooleanMethod; void* CallBooleanMethodV; void* CallBooleanMethodA;
    void* CallByteMethod; void* CallByteMethodV; void* CallByteMethodA;
    void* CallCharMethod; void* CallCharMethodV; void* CallCharMethodA;
    void* CallShortMethod; void* CallShortMethodV; void* CallShortMethodA;
    void* CallIntMethod; void* CallIntMethodV; void* CallIntMethodA;
    void* CallLongMethod; void* CallLongMethodV; void* CallLongMethodA;
    void* CallFloatMethod; void* CallFloatMethodV; void* CallFloatMethodA;
    void* CallDoubleMethod; void* CallDoubleMethodV; void* CallDoubleMethodA;
    void* CallVoidMethod; void* CallVoidMethodV; void* CallVoidMethodA;
    void* CallNonvirtualObjectMethod; void* CallNonvirtualObjectMethodV; void* CallNonvirtualObjectMethodA;
    void* CallNonvirtualBooleanMethod; void* CallNonvirtualBooleanMethodV; void* CallNonvirtualBooleanMethodA;
    void* CallNonvirtualByteMethod; void* CallNonvirtualByteMethodV; void* CallNonvirtualByteMethodA;
    void* CallNonvirtualCharMethod; void* CallNonvirtualCharMethodV; void* CallNonvirtualCharMethodA;
    void* CallNonvirtualShortMethod; void* CallNonvirtualShortMethodV; void* CallNonvirtualShortMethodA;
    void* CallNonvirtualIntMethod; void* CallNonvirtualIntMethodV; void* CallNonvirtualIntMethodA;
    void* CallNonvirtualLongMethod; void* CallNonvirtualLongMethodV; void* CallNonvirtualLongMethodA;
    void* CallNonvirtualFloatMethod; void* CallNonvirtualFloatMethodV; void* CallNonvirtualFloatMethodA;
    void* CallNonvirtualDoubleMethod; void* CallNonvirtualDoubleMethodV; void* CallNonvirtualDoubleMethodA;
    void* CallNonvirtualVoidMethod; void* CallNonvirtualVoidMethodV; void* CallNonvirtualVoidMethodA;
    void* GetFieldID; void* GetObjectField; void* GetBooleanField; void* GetByteField;
    void* GetCharField; void* GetShortField; void* GetIntField; void* GetLongField;
    void* GetFloatField; void* GetDoubleField; void* SetObjectField; void* SetBooleanField;
    void* SetByteField; void* SetCharField; void* SetShortField; void* SetIntField;
    void* SetLongField; void* SetFloatField; void* SetDoubleField; void* GetStaticMethodID;
    void* CallStaticObjectMethod; void* CallStaticObjectMethodV; void* CallStaticObjectMethodA;
    void* CallStaticBooleanMethod; void* CallStaticBooleanMethodV; void* CallStaticBooleanMethodA;
    void* CallStaticByteMethod; void* CallStaticByteMethodV; void* CallStaticByteMethodA;
    void* CallStaticCharMethod; void* CallStaticCharMethodV; void* CallStaticCharMethodA;
    void* CallStaticShortMethod; void* CallStaticShortMethodV; void* CallStaticShortMethodA;
    void* CallStaticIntMethod; void* CallStaticIntMethodV; void* CallStaticIntMethodA;
    void* CallStaticLongMethod; void* CallStaticLongMethodV; void* CallStaticLongMethodA;
    void* CallStaticFloatMethod; void* CallStaticFloatMethodV; void* CallStaticFloatMethodA;
    void* CallStaticDoubleMethod; void* CallStaticDoubleMethodV; void* CallStaticDoubleMethodA;
    void* CallStaticVoidMethod; void* CallStaticVoidMethodV; void* CallStaticVoidMethodA;
    void* GetStaticFieldID; void* GetStaticObjectField; void* GetStaticBooleanField;
    void* GetStaticByteField; void* GetStaticCharField; void* GetStaticShortField;
    void* GetStaticIntField; void* GetStaticLongField; void* GetStaticFloatField;
    void* GetStaticDoubleField; void* SetStaticObjectField; void* SetStaticBooleanField;
    void* SetStaticByteField; void* SetStaticCharField; void* SetStaticShortField;
    void* SetStaticIntField; void* SetStaticLongField; void* SetStaticFloatField;
    void* SetStaticDoubleField; void* NewString; void* GetStringLength;
    void* GetStringChars; void* ReleaseStringChars; void* NewStringUTF;
    void* GetStringUTFLength; void* GetStringUTFChars; void* ReleaseStringUTFChars;
    void* GetArrayLength; void* NewObjectArray; void* GetObjectArrayElement;
    void* SetObjectArrayElement; void* NewBooleanArray; void* NewByteArray;
    void* NewCharArray; void* NewShortArray; void* NewIntArray; void* NewLongArray;
    void* NewFloatArray; void* NewDoubleArray; void* GetBooleanArrayElements;
    void* GetByteArrayElements; void* GetCharArrayElements; void* GetShortArrayElements;
    void* GetIntArrayElements; void* GetLongArrayElements; void* GetFloatArrayElements;
    void* GetDoubleArrayElements; void* ReleaseBooleanArrayElements;
    void* ReleaseByteArrayElements; void* ReleaseCharArrayElements;
    void* ReleaseShortArrayElements; void* ReleaseIntArrayElements;
    void* ReleaseLongArrayElements; void* ReleaseFloatArrayElements;
    void* ReleaseDoubleArrayElements; void* GetBooleanArrayRegion; void* GetByteArrayRegion;
    void* GetCharArrayRegion; void* GetShortArrayRegion; void* GetIntArrayRegion;
    void* GetLongArrayRegion; void* GetFloatArrayRegion; void* GetDoubleArrayRegion;
    void* SetBooleanArrayRegion; void* SetByteArrayRegion; void* SetCharArrayRegion;
    void* SetShortArrayRegion; void* SetIntArrayRegion; void* SetLongArrayRegion;
    void* SetFloatArrayRegion; void* SetDoubleArrayRegion; void* RegisterNatives;
    void* UnregisterNatives; void* MonitorEnter; void* MonitorExit; void* GetJavaVM;
    void* GetStringRegion; void* GetStringUTFRegion; void* GetPrimitiveArrayCritical;
    void* ReleasePrimitiveArrayCritical; void* GetStringCritical;
    void* ReleaseStringCritical; void* NewWeakGlobalRef; void* DeleteWeakGlobalRef;
    void* ExceptionCheck; void* NewDirectByteBuffer; void* GetDirectBufferAddress;
    void* GetDirectBufferCapacity; void* GetObjectRefType;
} JNINativeInterface_;

struct JNIEnv_ {
    const JNINativeInterface_* functions;
};

typedef struct JNIInvokeInterface_ {
    void* reserved0; void* reserved1; void* reserved2; void* reserved3;
    void* DestroyJavaVM; void* AttachCurrentThread; void* DetachCurrentThread;
    void* GetEnv;
} JNIInvokeInterface_;

typedef struct JavaVM_ {
    const JNIInvokeInterface_* functions;
} JavaVM_;

typedef JavaVM_ JavaVM;

/* our fake string/class/method representations */
typedef struct fake_jstring { const char* utf; } fake_jstring;
typedef struct fake_jclass { const char* name; } fake_jclass;
typedef struct fake_jmethod { const char* class_name; const char* name; const char* sig; } fake_jmethod;

extern JNIEnv g_env;
extern JavaVM g_vm;

/* create a fake jstring from a C string (host side) */
jstring mk_jstring(const char* utf);

/* config injected by host */
extern int g_display_width;
extern int g_display_height;
extern int g_jni_verbose;

#endif
