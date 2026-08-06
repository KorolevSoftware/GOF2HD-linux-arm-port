/*
 * libm.c — bionic libm shim for GOF2HD on hardfp ARM.
 *
 * The engine imports math symbols versioned @LIBC from libm.so:
 *   acosf@LIBC atanf@LIBC cosf@LIBC powf@LIBC rint@LIBC sinf@LIBC sqrtf@LIBC
 * The engine is softfp (pcs aapcs): float args arrive in r0-r3.  glibc libm
 * is hardfp (aapcs-vfp): floats in s0-s15.  So each export is a pcs("aapcs")
 * softfp entry that forwards through a pcs("aapcs-vfp") glibc pointer.
 *
 * Build (hardfp): arm-linux-gnueabihf-gcc -mfloat-abi=hard
 *   -shared -fPIC -Wl,--version-script=libm.map libm.c -o libm.so
 */
#include <dlfcn.h>

#define SF  __attribute__((pcs("aapcs")))
#define HF  __attribute__((pcs("aapcs-vfp")))

static HF float (*gl_acosf)(float);
static HF float (*gl_atanf)(float);
static HF float (*gl_cosf)(float);
static HF float (*gl_powf)(float, float);
static HF double (*gl_rint)(double);
static HF float (*gl_sinf)(float);
static HF float (*gl_sqrtf)(float);

__attribute__((constructor)) static void libm_init(void) {
    void* h = dlopen("libm.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = dlopen("libm.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) return;
    gl_acosf = (HF float (*)(float))dlsym(h, "acosf");
    gl_atanf = (HF float (*)(float))dlsym(h, "atanf");
    gl_cosf  = (HF float (*)(float))dlsym(h, "cosf");
    gl_powf  = (HF float (*)(float, float))dlsym(h, "powf");
    gl_rint  = (HF double (*)(double))dlsym(h, "rint");
    gl_sinf  = (HF float (*)(float))dlsym(h, "sinf");
    gl_sqrtf = (HF float (*)(float))dlsym(h, "sqrtf");
}

SF float acosf(float a) { return gl_acosf ? gl_acosf(a) : a; }
SF float atanf(float a) { return gl_atanf ? gl_atanf(a) : a; }
SF float cosf(float a)  { return gl_cosf  ? gl_cosf(a)  : a; }
SF float powf(float a, float b) { return gl_powf ? gl_powf(a, b) : a; }
SF double rint(double a) { return gl_rint ? gl_rint(a) : a; }
SF float sinf(float a)  { return gl_sinf  ? gl_sinf(a)  : a; }
SF float sqrtf(float a) { return gl_sqrtf ? gl_sqrtf(a) : a; }
