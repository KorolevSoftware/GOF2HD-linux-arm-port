/*
 * libfmodex.so — no-op stub of the FMOD Ex 4.x C API functions used by the
 * engine.  All return FMOD_OK.
 */
#include <stddef.h>
typedef int FMOD_RESULT;
#define FMOD_OK 0

struct FMOD_SYSTEM;
struct FMOD_SOUND;
struct FMOD_CHANNEL;
struct FMOD_SYNCPOINT;

FMOD_RESULT FMOD_System_Create(struct FMOD_SYSTEM** system, unsigned int header_version) {
    if (system) *system = (struct FMOD_SYSTEM*)1;
    return FMOD_OK;
}

FMOD_RESULT FMOD_Sound_GetSyncPoint(struct FMOD_SOUND* sound, int index, struct FMOD_SYNCPOINT** point) {
    if (point) *point = NULL;
    return FMOD_OK;
}

FMOD_RESULT FMOD_Sound_GetSyncPointInfo(struct FMOD_SOUND* sound, struct FMOD_SYNCPOINT* point,
                                        char* name, int namelen, unsigned int* offset, int type) {
    if (name && namelen > 0) name[0] = 0;
    if (offset) *offset = 0;
    return FMOD_OK;
}

FMOD_RESULT FMOD_Channel_GetCurrentSound(struct FMOD_CHANNEL* channel, struct FMOD_SOUND** sound) {
    if (sound) *sound = NULL;
    return FMOD_OK;
}

FMOD_RESULT FMOD_Channel_GetUserData(struct FMOD_CHANNEL* channel, void** userdata) {
    if (userdata) *userdata = NULL;
    return FMOD_OK;
}
