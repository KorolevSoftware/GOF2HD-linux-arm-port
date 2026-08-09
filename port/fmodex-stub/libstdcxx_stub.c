/*
 * libstdc++.so — minimal replacement for bionic's tiny libstdc++.
 *
 * The real Android libfmodex.so/libfmodevent.so link bionic's libstdc++.so
 * (a ~9-symbol C++ ABI shim, nothing like the GNU lib).  On the device that
 * lib does not exist, so this stub exports the same set.  The C++ runtime
 * symbols have no float args, so no pcs() annotations are needed.
 *
 * NOTE: libstdc++.so.6 (GNU) is normally NOT loaded into the process, so we
 * cannot forward to it — everything must be self-contained or use glibc.
 *
 * ABI: the FMOD libs reference the UNMANGLED C ABI names (__cxa_pure_virtual,
 * __cxa_begin_cleanup, ...) and the mangled "operator delete" (_ZdlPv).
 * All must keep C linkage, so wrap them in extern "C".
 */
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void _ZdlPv(void* p) { free(p); }              /* operator delete  */

void __cxa_pure_virtual(void) { abort(); }     /* pure virtual call */

void __cxa_call_unexpected(void* e) { (void)e; abort(); }

/* exception-cleanup hooks; FMOD never throws, so minimal semantics */
void* __cxa_begin_cleanup(void* ue) { (void)ue; return NULL; }
void* __cxa_end_cleanup(void* ue) { (void)ue; return NULL; }
void* __cxa_type_match(const void* t, const void* c, const void* r, const void* o) {
    (void)t; (void)c; (void)r; (void)o;
    return NULL;
}

/* __cxa_atexit/__cxa_finalize/__aeabi_atexit: forward to glibc */
extern int __cxa_atexit(void (*fn)(void*), void* arg, void* dso);
int __aeabi_atexit(void* obj, void (*fn)(void*), void* dso) {
    return __cxa_atexit(fn, obj, dso);
}

#ifdef __cplusplus
}
#endif
