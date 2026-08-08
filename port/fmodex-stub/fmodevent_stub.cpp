/*
 * libfmodevent.so — no-op stub of the FMOD Ex 4.x Event System C++ API.
 * The engine calls these methods directly (no vtables), so we only need to
 * export the mangled symbols.  All return FMOD_OK and no-op.
 *
 * IMPORTANT (as discovered on the device): the engine's FModSound::play()
 * stores whatever this stub returns in its sound slots and later dispatches
 * through those slots' vtables (FModSound::stopAllSoundFXEvents calls
 * vtable[7] — FMOD virtual stop()).  Returning fake (non-NULL) objects
 * makes the engine enter vtable-dispatch paths built for the real FMOD
 * layout, which crash later (SIGSEGV in .rodata).  Returning NULL for the
 * objects is the only safe choice — the engine's NULL-guarded paths must be
 * kept: FModSound::stopAllSoundFXEvents in the game binary is patched to a
 * no-op (movs r0,#0; bx lr) so empty slots never crash.
 */
#include <cstddef>

typedef int FMOD_RESULT;
#define FMOD_OK 0

struct FMOD_VECTOR { float x, y, z; };
struct FMOD_REVERB_PROPERTIES { char pad[64]; };
struct FMOD_EVENT_LOADINFO { char pad[64]; };
struct FMOD_EVENT_INFO { char pad[64]; };
enum FMOD_EVENT_PITCHUNITS { FMOD_EVENT_PITCHUNITS_RAW = 0, FMOD_EVENT_PITCHUNITS_SEMITONES, FMOD_EVENT_PITCHUNITS_CENTS, FMOD_EVENT_PITCHUNITS_OCTAVES };

struct FMOD_EVENT_SYSTEM;

namespace FMOD {
class EventProject;
class EventCategory;
class EventGroup;
class Event;
class EventParameter;

class EventSystem {
public:
    FMOD_RESULT getProject(const char*, EventProject**);
    FMOD_RESULT getCategory(const char*, EventCategory**);
    FMOD_RESULT setLanguage(const char*);
    FMOD_RESULT getProjectByIndex(int, EventProject**);
    FMOD_RESULT getEventBySystemID(unsigned int, unsigned int, Event**);
    FMOD_RESULT getNumReverbPresets(int*);
    FMOD_RESULT setReverbProperties(const FMOD_REVERB_PROPERTIES*);
    FMOD_RESULT getReverbPresetByIndex(int, FMOD_REVERB_PROPERTIES*, char**);
    FMOD_RESULT set3DListenerAttributes(int, const FMOD_VECTOR*, const FMOD_VECTOR*, const FMOD_VECTOR*, const FMOD_VECTOR*);
    FMOD_RESULT init(int, unsigned int, void*, unsigned int);
    FMOD_RESULT load(const char*, FMOD_EVENT_LOADINFO*, EventProject**);
    FMOD_RESULT unload();
    FMOD_RESULT update();
    FMOD_RESULT release();
};

class Event {
public:
    FMOD_RESULT getCategory(EventCategory**);
    FMOD_RESULT getProperty(const char*, void*, bool);
    FMOD_RESULT getParameter(const char*, EventParameter**);
    FMOD_RESULT getParentGroup(EventGroup**);
    FMOD_RESULT set3DAttributes(const FMOD_VECTOR*, const FMOD_VECTOR*, const FMOD_VECTOR*);
    FMOD_RESULT getParameterByIndex(int, EventParameter**);
    FMOD_RESULT stop(bool);
    FMOD_RESULT start();
    FMOD_RESULT getInfo(int*, char**, FMOD_EVENT_INFO*);
    FMOD_RESULT getState(unsigned int*);
    FMOD_RESULT setPitch(float, FMOD_EVENT_PITCHUNITS);
    FMOD_RESULT setPaused(bool);
    FMOD_RESULT setVolume(float);
};

class EventParameter {
public:
    FMOD_RESULT setValue(float);
};

} /* namespace FMOD */

/* All getters must return NULL in the out-parameters (see header comment). */
#define OUT_NULL(ptr) if (ptr) *(ptr) = 0

FMOD_RESULT FMOD::EventSystem::getProject(const char* n, EventProject** p) { (void)n; OUT_NULL(p); return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::getCategory(const char* n, EventCategory** c) { (void)n; OUT_NULL(c); return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::setLanguage(const char* l) { (void)l; return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::getProjectByIndex(int i, EventProject** p) { (void)i; OUT_NULL(p); return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::getEventBySystemID(unsigned int s, unsigned int i, Event** e) { (void)s; (void)i; OUT_NULL(e); return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::getNumReverbPresets(int* n) { if (n) *n = 0; return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::setReverbProperties(const FMOD_REVERB_PROPERTIES* p) { (void)p; return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::getReverbPresetByIndex(int i, FMOD_REVERB_PROPERTIES* p, char** n) { (void)i; (void)p; if (n) *n = (char*)""; return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::set3DListenerAttributes(int l, const FMOD_VECTOR* a, const FMOD_VECTOR* b, const FMOD_VECTOR* c, const FMOD_VECTOR* d) { (void)l;(void)a;(void)b;(void)c;(void)d; return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::init(int m, unsigned int f, void* e, unsigned int c) { (void)m;(void)f;(void)e;(void)c; return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::load(const char* n, FMOD_EVENT_LOADINFO* l, EventProject** p) { (void)n;(void)l; OUT_NULL(p); return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::unload() { return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::update() { return FMOD_OK; }
FMOD_RESULT FMOD::EventSystem::release() { return FMOD_OK; }

FMOD_RESULT FMOD::Event::getCategory(EventCategory** c) { OUT_NULL(c); return FMOD_OK; }
FMOD_RESULT FMOD::Event::getProperty(const char* n, void* v, bool rw) { (void)n;(void)v;(void)rw; return FMOD_OK; }
FMOD_RESULT FMOD::Event::getParameter(const char* n, EventParameter** p) { (void)n; OUT_NULL(p); return FMOD_OK; }
FMOD_RESULT FMOD::Event::getParentGroup(EventGroup** g) { OUT_NULL(g); return FMOD_OK; }
FMOD_RESULT FMOD::Event::set3DAttributes(const FMOD_VECTOR* a, const FMOD_VECTOR* b, const FMOD_VECTOR* c) { (void)a;(void)b;(void)c; return FMOD_OK; }
FMOD_RESULT FMOD::Event::getParameterByIndex(int i, EventParameter** p) { (void)i; OUT_NULL(p); return FMOD_OK; }
FMOD_RESULT FMOD::Event::stop(bool i) { (void)i; return FMOD_OK; }
FMOD_RESULT FMOD::Event::start() { return FMOD_OK; }
FMOD_RESULT FMOD::Event::getInfo(int* i, char** n, FMOD_EVENT_INFO* f) { if (i) *i = 0; if (n) *n = (char*)""; (void)f; return FMOD_OK; }
FMOD_RESULT FMOD::Event::getState(unsigned int* s) { if (s) *s = 0; return FMOD_OK; }
FMOD_RESULT FMOD::Event::setPitch(float p, FMOD_EVENT_PITCHUNITS u) { (void)p;(void)u; return FMOD_OK; }
FMOD_RESULT FMOD::Event::setPaused(bool p) { (void)p; return FMOD_OK; }
FMOD_RESULT FMOD::Event::setVolume(float v) { (void)v; return FMOD_OK; }

FMOD_RESULT FMOD::EventParameter::setValue(float v) { (void)v; return FMOD_OK; }

extern "C" FMOD_RESULT FMOD_EventSystem_Create(FMOD_EVENT_SYSTEM** eventsystem, unsigned int header_version) {
    (void)header_version;
    if (eventsystem) *eventsystem = (FMOD_EVENT_SYSTEM*)1; /* any non-NULL opaque tag is fine */
    return FMOD_OK;
}