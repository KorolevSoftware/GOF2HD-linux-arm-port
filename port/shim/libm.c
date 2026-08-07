/*
 * libm.c — bionic libm shim for GOF2HD on hardfp ARM.
 *
 * The engine (softfp/pcs aapcs, floats in r0-r3) imports math symbols
 * versioned @LIBC from libm.so: acosf@LIBC atanf@LIBC cosf@LIBC powf@LIBC
 * rint@LIBC sinf@LIBC sqrtf@LIBC.  glibc libm is hardfp (aapcs-vfp): floats
 * in s0-s15.  Each export here is a pcs("aapcs") softfp entry that forwards
 * through a pcs("aapcs-vfp") pointer resolved with dlsym() from glibc libm.
 *
 * Build (hardfp): arm-linux-gnueabihf-gcc -mfloat-abi=hard
 *   -shared -fPIC -Wl,--version-script=libm.map libm.c -o libm.so
 */
#include <dlfcn.h>

#define SF  __attribute__((pcs("aapcs")))
#define HF  __attribute__((pcs("aapcs-vfp")))

typedef HF float  (*HFf)(float);
typedef HF double (*HFd)(double);
typedef HF double (*HFdd)(double, double);
typedef HF double (*HFdi)(double, int);
typedef HF double (*HFdip)(double, int*);
typedef HF float  (*HFff)(float, float);
typedef HF long   (*HFl)(float);

static HFf  gl_acosf;
static HFf  gl_atanf;
static HFf  gl_cosf;
static HFf  gl_sinf;
static HFf  gl_sqrtf;
static HFff gl_powf;
static HFd  gl_rint;

static HFd  gl_acos;
static HFdd gl_atan2;
static HFd  gl_ceil;
static HFd  gl_cos;
static HFd  gl_exp;
static HFd  gl_floor;
static HFd  gl_log;
static HFd  gl_log10;
static HFdd gl_pow;
static HFd  gl_sin;
static HFd  gl_sqrt;
static HFd  gl_tan;
static HFf  gl_log10f;
static HFdi gl_ldexp;
static HFdip gl_frexp;
static HFl  gl_lrintf;

__attribute__((constructor)) static void libm_init(void) {
    void* h = dlopen("libm.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = dlopen("libm.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) return;

    gl_acosf  = (HFf) dlsym(h, "acosf");
    gl_atanf  = (HFf) dlsym(h, "atanf");
    gl_cosf   = (HFf) dlsym(h, "cosf");
    gl_powf   = (HFff)dlsym(h, "powf");
    gl_rint   = (HFd) dlsym(h, "rint");
    gl_sinf   = (HFf) dlsym(h, "sinf");
    gl_sqrtf  = (HFf) dlsym(h, "sqrtf");

    gl_acos   = (HFd) dlsym(h, "acos");
    gl_atan2  = (HFdd)dlsym(h, "atan2");
    gl_ceil   = (HFd) dlsym(h, "ceil");
    gl_cos    = (HFd) dlsym(h, "cos");
    gl_exp    = (HFd) dlsym(h, "exp");
    gl_floor  = (HFd) dlsym(h, "floor");
    gl_log    = (HFd) dlsym(h, "log");
    gl_log10  = (HFd) dlsym(h, "log10");
    gl_log10f = (HFf) dlsym(h, "log10f");
    gl_pow    = (HFdd)dlsym(h, "pow");
    gl_sin    = (HFd) dlsym(h, "sin");
    gl_sqrt   = (HFd) dlsym(h, "sqrt");
    gl_tan    = (HFd) dlsym(h, "tan");
    gl_ldexp  = (HFdi) dlsym(h, "ldexp");
    gl_frexp  = (HFdip)dlsym(h, "frexp");
    gl_lrintf = (HFl) dlsym(h, "lrintf");
}

SF float  acosf(float a)       { return gl_acosf  ? gl_acosf(a)        : a; }
SF float  atanf(float a)       { return gl_atanf  ? gl_atanf(a)        : a; }
SF float  cosf(float a)        { return gl_cosf   ? gl_cosf(a)         : a; }
SF float  sinf(float a)        { return gl_sinf   ? gl_sinf(a)         : a; }
SF float  sqrtf(float a)       { return gl_sqrtf  ? gl_sqrtf(a)        : a; }
SF float  powf(float a, float b) { return gl_powf ? gl_powf(a, b)      : a; }
SF double rint(double a)       { return gl_rint   ? gl_rint(a)         : a; }

SF double acos(double a)       { return gl_acos   ? gl_acos(a)         : a; }
SF double atan2(double a, double b) { return gl_atan2 ? gl_atan2(a, b) : a; }
SF double ceil(double a)       { return gl_ceil   ? gl_ceil(a)         : a; }
SF double cos(double a)        { return gl_cos    ? gl_cos(a)          : a; }
SF double exp(double a)        { return gl_exp    ? gl_exp(a)          : a; }
SF double floor(double a)      { return gl_floor  ? gl_floor(a)        : a; }
SF double log(double a)        { return gl_log    ? gl_log(a)          : a; }
SF double log10(double a)      { return gl_log10  ? gl_log10(a)        : a; }
SF float  log10f(float a)      { return gl_log10f ? gl_log10f(a)       : a; }
SF double pow(double a, double b) { return gl_pow ? gl_pow(a, b)       : a; }
SF double sin(double a)        { return gl_sin    ? gl_sin(a)          : a; }
SF double sqrt(double a)       { return gl_sqrt   ? gl_sqrt(a)         : a; }
SF double tan(double a)        { return gl_tan    ? gl_tan(a)          : a; }
SF double ldexp(double a, int b) { return gl_ldexp ? gl_ldexp(a, b)    : a; }
SF double frexp(double a, int* b) { return gl_frexp? gl_frexp(a, b)    : a; }
SF long   lrintf(float a)      { return gl_lrintf ? gl_lrintf(a)       : 0; }