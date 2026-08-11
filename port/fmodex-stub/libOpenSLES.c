/*
 * libOpenSLES.so — fake OpenSL ES for the Android FMOD libs on Linux.
 *
 * FMOD (this Android build) uses the OpenSL ES output by default.  On Linux
 * there is no OpenSL; instead of failing, this fake:
 *   - implements the SL object/interface vtables FMOD calls,
 *   - routes the PCM buffer queue (SLBufferQueueItf::Enqueue) into SDL2
 *     (SDL_QueueAudio), which plays it through ALSA,
 *   - runs a pacing thread that calls FMOD's buffer-queue callback so FMOD
 *     keeps the buffer fed.
 *
 * ABI notes (learned the hard way on the device):
 *   - OpenSL ES handles are POINTER-TO-POINTER (SLObjectItf = struct * const *).
 *     FMOD lowers (*obj)->Method(obj, ...) to **obj then ->Method, so every
 *     handle returned by slCreateEngine/CreateOutputMix/CreateAudioPlayer/
 *     GetInterface must be the ADDRESS of a pointer variable holding the
 *     struct address.
 *   - This FMOD build passes the SLInterfaceID as the 32-bit VALUE of the
 *     first word of the iid constant (0x2e=bufferqueue, 0x2f=play,
 *     0x28=volume, 0x2b=androidconfiguration, 0x80=androidsimplebufferqueue).
 *     Never memcmp() the iid pointer — it is not a 16-byte pointer here.
 *   - The SLEngineItf slot order FMOD uses: CreateAudioPlayer@2,
 *     CreateOutputMix@7 (not the stock OpenSL ES order).  All unused slots
 *     are filled with no-ops so any index FMOD touches is safe.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#include <SDL2/SDL.h>

typedef int  SLresult;
typedef unsigned int  SLuint32;
typedef unsigned int  SLmillisecond;
typedef char  SLboolean;
typedef struct SLObjectItf_*          SLObjectItf;
typedef struct SLEngineItf_*          SLEngineItf;
typedef struct SLPlayItf_*            SLPlayItf;
typedef struct SLBufferQueueItf_*     SLBufferQueueItf;
typedef struct SLVolumeItf_*          SLVolumeItf;
typedef struct SLAndroidConfigurationItf_* SLAndroidConfigurationItf;

#define SL_RESULT_SUCCESS   ((SLresult) 0)
#define SL_OBJECT_STATE_REALIZED ((SLuint32) 1)
#define SL_PLAYSTATE_STOPPED     ((SLuint32) 1)
#define SL_PLAYSTATE_PAUSED      ((SLuint32) 2)
#define SL_PLAYSTATE_PLAYING     ((SLuint32) 3)
#define SL_DATAFORMAT_PCM        ((SLuint32) 2)

/* FMOD references these data symbols (SL_IID_ENGINE must exist at dlopen). */
const unsigned char SL_IID_ENGINE[16] = {0x30,0,0,0,0x01,0,0x40,0x02,0xC4,0x9A,0x9B,0x51,0x64,0x9A,0xD6,0xAF};
#define DEF_IID(name,a,b,c,d,e,f,g,h,i,j,k) const unsigned char name[16]={a,b,c,d,e,f,g,h,i,j,k,0,0,0,0,0}
DEF_IID(SL_IID_OBJECT,0x02,0,0,0,0x01,0,0,0,0x01,0,0);
DEF_IID(SL_IID_OUTPUTMIX,0x2d,0,0,0,0x01,0,0,0,0x01,0,0);
DEF_IID(SL_IID_ENGINECAPS,0x31,0,0,0,0x01,0,0x40,0x02,0xC4,0x9A,0x9B);
DEF_IID(SL_IID_PLAY,0x2f,0,0,0,0x01,0,0,0,0x01,0,0);
DEF_IID(SL_IID_VOLUME,0x28,0,0,0,0x01,0,0,0,0x01,0,0);
DEF_IID(SL_IID_BUFFERQUEUE,0x2e,0,0,0,0x01,0,0,0,0x01,0,0);
DEF_IID(SL_IID_ANDROIDSIMPLEBUFFERQUEUE,0x80,0,0,0,0x01,0,0xff,0,0,0,0);
DEF_IID(SL_IID_ANDROIDCONFIGURATION,0x2b,0,0,0,0x01,0,0x80,0x01,0x94,0x45,0x4d);
DEF_IID(SL_IID_PLAYBACKRATE,0x70,0,0,0,0x01,0,0,0,0x01,0,0);
DEF_IID(SL_IID_METADATAEXTRACTION,0x34,0,0,0,0x01,0,0,0,0x01,0,0);
DEF_IID(SL_IID_RECORD,0x32,0,0,0,0x01,0,0x40,0x02,0xC4,0x9A,0x9B);

typedef struct SLDataFormat_PCM_ {
    SLuint32 formatType; SLuint32 numChannels; SLuint32 samplesPerSec;
    SLuint32 bitsPerSample; SLuint32 containerSize; SLuint32 channelMask; SLuint32 endianness;
} SLDataFormat_PCM;

static int g_dbg = -1;
#define DLOG(fmt, ...) do { if (g_dbg < 0) g_dbg = getenv("OPENSL_DEBUG") ? 1 : 0; \
    if (g_dbg) fprintf(stderr, "[opensl] " fmt "\n", ##__VA_ARGS__); } while (0)

/* ========================= vtables ========================= */

struct SLObjectItf_ {
    SLresult (*Realize)(SLObjectItf, SLboolean);
    SLresult (*Resume)(SLObjectItf, SLboolean);
    SLresult (*GetState)(SLObjectItf, SLuint32*);
    SLresult (*GetInterface)(SLObjectItf, const void*, void*);
    SLresult (*RegisterCallback)(SLObjectItf, void (*)(SLObjectItf, void*), void*);
    void (*AbortAsyncOperation)(SLObjectItf);
    void (*Destroy)(SLObjectItf);
};
struct SLEngineItf_ {
    SLresult (*CreateLEDDevice)(SLEngineItf, SLObjectItf*, SLuint32, const void*, const SLboolean*);
    SLresult (*CreateAudioPlayer)(SLEngineItf, SLObjectItf*, void*, void*, SLuint32, const void*, const SLboolean*);
    SLresult (*CreateAudioRecorder)(SLEngineItf, SLObjectItf*, void*, void*, SLuint32, const void*, const SLboolean*);
    SLresult (*CreateMidiPlayer)(SLEngineItf, SLObjectItf*, void*, void*, void*, SLuint32, const void*, const SLboolean*);
    SLresult (*CreateListener)(SLEngineItf, SLObjectItf*, SLuint32, const void*, const SLboolean*);
    SLresult (*Create3DGroup)(SLEngineItf, SLObjectItf*, SLuint32, const void*, const SLboolean*);
    SLresult (*CreateOutputMix)(SLEngineItf, SLObjectItf*, SLuint32, const void*, const SLboolean*);
    SLresult (*CreateMetadataExtractor)(SLEngineItf, SLObjectItf*, void*, SLuint32, const void*, const SLboolean*);
    SLresult (*CreateExtensionObject)(SLEngineItf, SLObjectItf*, void*, SLuint32, SLuint32, const void*, const SLboolean*);
    SLresult (*GetNumSupportedInterfaces)(SLEngineItf, SLuint32*);
    SLresult (*QuerySupportedInterfaces)(SLEngineItf, SLuint32, void*);
    SLresult (*QueryNumSupportedInterfaces)(SLEngineItf, SLuint32*);
    SLresult (*GetInterface)(SLEngineItf, const void*, void*);
};
struct SLPlayItf_ {
    SLresult (*SetPlayState)(SLPlayItf, SLuint32);
    SLresult (*GetPlayState)(SLPlayItf, SLuint32*);
    SLresult (*GetDuration)(SLPlayItf, SLmillisecond*);
    SLresult (*GetPosition)(SLPlayItf, SLmillisecond*);
    SLresult (*RegisterPositionCallback)(SLPlayItf, void (*)(SLPlayItf, void*), void*);
    SLresult (*SetPositionUpdatePeriod)(SLPlayItf, SLmillisecond);
    SLresult (*SetCallbackEventsMask)(SLPlayItf, SLuint32);
    SLresult (*GetCallbackEventsMask)(SLPlayItf, SLuint32*);
    SLresult (*SetMarkerPosition)(SLPlayItf, SLuint32, SLmillisecond);
    SLresult (*ClearMarkerPosition)(SLPlayItf, SLuint32);
    SLresult (*GetMarkerPosition)(SLPlayItf, SLuint32, SLmillisecond*);
};
struct SLBufferQueueItf_ {
    SLresult (*Enqueue)(SLBufferQueueItf, const void*, SLuint32);
    SLresult (*Clear)(SLBufferQueueItf);
    SLresult (*GetState)(SLBufferQueueItf, SLuint32*);
    SLresult (*RegisterCallback)(SLBufferQueueItf, void (*)(SLBufferQueueItf, void*), void*);
    SLresult (*GetBuffer)(SLBufferQueueItf, SLuint32, void**, SLuint32*);
};
struct SLVolumeItf_ {
    SLresult (*SetVolumeLevel)(SLVolumeItf, int);
    SLresult (*GetVolumeLevel)(SLVolumeItf, int*);
    SLresult (*GetMaxVolumeLevel)(SLVolumeItf, int*);
    SLresult (*SetMute)(SLVolumeItf, SLboolean);
    SLresult (*GetMute)(SLVolumeItf, SLboolean*);
    SLresult (*EnableStereoPosition)(SLVolumeItf, SLboolean);
    SLresult (*SetStereoPosition)(SLVolumeItf, int);
    SLresult (*GetStereoPosition)(SLVolumeItf, int*);
};
struct SLAndroidConfigurationItf_ {
    SLresult (*SetConfiguration)(SLAndroidConfigurationItf, SLuint32, const void*, SLuint32);
    SLresult (*GetConfiguration)(SLAndroidConfigurationItf, SLuint32, void*, SLuint32*);
    SLresult (*SetCallback)(SLAndroidConfigurationItf, SLuint32, void (*)(SLAndroidConfigurationItf, void*, SLuint32, const void*, SLuint32));
    SLresult (*SetConfigCallback)(SLAndroidConfigurationItf, void (*)(SLAndroidConfigurationItf, void*, SLuint32, const void*, SLuint32));
};

/* ========================= objects (heap) ========================= */
static struct SLObjectItf_ *g_player_obj, *g_mix_obj, *g_engine_obj;
static struct SLEngineItf_ *g_engine_if;
static struct SLPlayItf_ *g_play_if;
static struct SLVolumeItf_ *g_vol_if;
static struct SLAndroidConfigurationItf_ *g_cfg_if;
static struct SLBufferQueueItf_ *g_bq_if;

/* handle variables (pointer-to-pointer targets) */
static struct SLObjectItf_* g_engine_h;
static struct SLObjectItf_* g_mix_h;
static struct SLObjectItf_* g_player_h;
static struct SLEngineItf_*  g_engine_if_h;
static struct SLPlayItf_*    g_play_if_h;
static struct SLVolumeItf_*  g_vol_if_h;
static struct SLAndroidConfigurationItf_* g_cfg_if_h;
static struct SLBufferQueueItf_* g_bq_if_h;

/* ========================= SDL audio ========================= */
static SDL_AudioDeviceID g_sdl_dev = 0;
static int g_sdl_freq = 44100, g_sdl_ch = 2;
static int g_input_freq = 44100, g_input_ch = 2;

static void audio_open(void) {
    SDL_AudioSpec want, got;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_Init(SDL_INIT_AUDIO) != 0) DLOG("SDL_Init fail: %s", SDL_GetError());
    }
    SDL_zero(want);
    want.freq = g_sdl_freq; want.format = AUDIO_S16SYS; want.channels = g_sdl_ch; want.samples = 1024;
    for (int attempt = 0; attempt < 20 && !g_sdl_dev; ++attempt) {
        g_sdl_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
        if (!g_sdl_dev) usleep(100000);
    }
    if (!g_sdl_dev) { DLOG("SDL_OpenAudioDevice fail: %s", SDL_GetError()); return; }
    SDL_PauseAudioDevice(g_sdl_dev, 0);
    DLOG("SDL audio device opened: %d Hz %d ch", got.freq, got.channels);
}

/* ========================= impls ========================= */

static SLresult noop0(void* a, void* b) { (void)a; (void)b; return SL_RESULT_SUCCESS; }
static SLresult noop0v(void* a) { (void)a; return SL_RESULT_SUCCESS; }
static SLresult noop0u(void* a, unsigned b) { (void)a; (void)b; return SL_RESULT_SUCCESS; }
static void noop_void(void* a) { (void)a; }

static SLresult obj_Realize(SLObjectItf self, SLboolean async) { (void)self; (void)async; DLOG("obj_Realize %p", (void*)self); return SL_RESULT_SUCCESS; }
static SLresult obj_GetState(SLObjectItf self, SLuint32* state) { if (state) *state = SL_OBJECT_STATE_REALIZED; return SL_RESULT_SUCCESS; }
static void obj_Destroy(SLObjectItf self) { DLOG("obj_Destroy %p", (void*)self); }

static void (*g_bq_callback)(SLBufferQueueItf, void*) = NULL;
static void* g_bq_context = NULL;
static unsigned g_last_size = 4096;
static unsigned g_enqueue_count;
static SLuint32 g_play_state = SL_PLAYSTATE_STOPPED;
static pthread_mutex_t g_bq_lock = PTHREAD_MUTEX_INITIALIZER;
static SLuint32 g_bq_count;
static SLuint32 g_bq_index;

static SLresult bq_Enqueue(SLBufferQueueItf self, const void* b, SLuint32 sz) {
    unsigned enqueue_count = ++g_enqueue_count;
    if (g_dbg && b && sz >= sizeof(int16_t) && (enqueue_count <= 3 || (enqueue_count % 120) == 0)) {
        const int16_t *samples = (const int16_t *)b;
        unsigned count = sz / sizeof(*samples);
        int lo = samples[0], hi = samples[0];
        for (unsigned i = 1; i < count; ++i) {
            if (samples[i] < lo) lo = samples[i];
            if (samples[i] > hi) hi = samples[i];
        }
        DLOG("bq.Enqueue %u queued=%u PCM16=[%d,%d]", sz,
             (unsigned)SDL_GetQueuedAudioSize(g_sdl_dev), lo, hi);
    }
    unsigned queued_size = sz;
    void *resampled = NULL;
    if (g_sdl_dev && g_sdl_freq == g_input_freq * 2 &&
        g_sdl_ch == g_input_ch && g_input_ch > 0) {
        unsigned frames = sz / (sizeof(int16_t) * (unsigned)g_input_ch);
        int16_t *output = malloc((size_t)frames * g_input_ch * 2 * sizeof(*output));
        if (output) {
            const int16_t *input = b;
            for (unsigned frame = 0; frame < frames; ++frame) {
                for (int channel = 0; channel < g_input_ch; ++channel) {
                    int16_t sample = input[frame * g_input_ch + channel];
                    output[(frame * 2) * g_input_ch + channel] = sample;
                    output[(frame * 2 + 1) * g_input_ch + channel] = sample;
                }
            }
            resampled = output;
            queued_size = frames * g_input_ch * 2 * sizeof(*output);
        }
    }
    g_last_size = queued_size;
    pthread_mutex_lock(&g_bq_lock);
    ++g_bq_count;
    pthread_mutex_unlock(&g_bq_lock);
    if (g_sdl_dev) SDL_QueueAudio(g_sdl_dev, resampled ? resampled : b, queued_size);
    free(resampled);
    return SL_RESULT_SUCCESS;
}
static SLresult bq_RegisterCallback(SLBufferQueueItf self, void (*cb)(SLBufferQueueItf, void*), void* ctx) {
    DLOG("bq.RegisterCallback cb=%p", (void*)cb);
    g_bq_callback = cb; g_bq_context = ctx; return SL_RESULT_SUCCESS;
}

static void bq_consume_once(void) {
    unsigned queued = g_sdl_dev ? SDL_GetQueuedAudioSize(g_sdl_dev) : 0;
    int consumed = 0;
    static unsigned consume_calls;
    if (queued < g_last_size * 3) {
        pthread_mutex_lock(&g_bq_lock);
        if (g_bq_count) {
            --g_bq_count;
            ++g_bq_index;
            consumed = 1;
        }
        pthread_mutex_unlock(&g_bq_lock);
    }
    ++consume_calls;
    if (g_dbg && (consume_calls <= 3 || (consume_calls % 120) == 0))
        DLOG("bq.consume queued=%u count=%u callback=%p", queued, g_bq_count,
             (void *)g_bq_callback);
    if (consumed && g_bq_callback)
        g_bq_callback((SLBufferQueueItf)&g_bq_if_h, g_bq_context);
}

static void* bq_thread_fn(void* arg) {
    DLOG("bq pacing thread started");
    for (;;) {
        /* Poll halfway through a PCM block so SDL keeps a small lead.  Waiting
         * for a whole block lets the ALSA queue drain to zero on this device. */
        unsigned int wait_us = g_sdl_freq > 0 && g_sdl_ch > 0 ?
            (unsigned int)((uint64_t)g_last_size * 1000000u /
                           ((unsigned)g_sdl_freq * (unsigned)g_sdl_ch * sizeof(int16_t) * 2u)) : 10666;
        usleep(wait_us ? wait_us : 10666);
        if (g_bq_callback)
            bq_consume_once();
    }
    return NULL;
}
static SLresult bq_Clear(SLBufferQueueItf self) {
    (void)self;
    pthread_mutex_lock(&g_bq_lock);
    g_bq_count = 0;
    pthread_mutex_unlock(&g_bq_lock);
    if (g_sdl_dev) SDL_ClearQueuedAudio(g_sdl_dev);
    return SL_RESULT_SUCCESS;
}

static SLresult bq_GetState(SLBufferQueueItf self, SLuint32* st) {
    (void)self;
    if (st) {
        pthread_mutex_lock(&g_bq_lock);
        /* SLAndroidSimpleBufferQueueState is two consecutive uint32 values. */
        st[0] = g_bq_count;
        st[1] = g_bq_index;
        pthread_mutex_unlock(&g_bq_lock);
    }
    return SL_RESULT_SUCCESS;
}
static SLresult bq_GetBuffer(SLBufferQueueItf self, SLuint32 i, void** b, SLuint32* s) { (void)i; if(b)*b=0; if(s)*s=0; return SL_RESULT_SUCCESS; }

static SLresult play_SetPlayState(SLPlayItf self, SLuint32 state) {
    g_play_state = state;
    DLOG("play.SetPlayState %u", state);
    return SL_RESULT_SUCCESS;
}
static SLresult play_GetPlayState(SLPlayItf self, SLuint32* state) {
    if (state) *state = g_play_state;
    return SL_RESULT_SUCCESS;
}
static SLresult play_GetDuration(SLPlayItf self, SLmillisecond* m) { if (m) *m = 0; return SL_RESULT_SUCCESS; }
static SLresult play_GetPosition(SLPlayItf self, SLmillisecond* m) { if (m) *m = 0; return SL_RESULT_SUCCESS; }
static SLresult play_RegPosCb(SLPlayItf self, void (*cb)(SLPlayItf, void*), void* c) { return SL_RESULT_SUCCESS; }
static SLresult play_SetPosPeriod(SLPlayItf self, SLmillisecond m) { return SL_RESULT_SUCCESS; }
static SLresult play_SetCbMask(SLPlayItf self, SLuint32 f) { return SL_RESULT_SUCCESS; }
static SLresult play_GetCbMask(SLPlayItf self, SLuint32* f) { if (f) *f = 0; return SL_RESULT_SUCCESS; }
static SLresult play_SetMarker(SLPlayItf self, SLuint32 m, SLmillisecond ms) { return SL_RESULT_SUCCESS; }
static SLresult play_ClearMarker(SLPlayItf self, SLuint32 m) { return SL_RESULT_SUCCESS; }
static SLresult play_GetMarker(SLPlayItf self, SLuint32 m, SLmillisecond* ms) { if (ms) *ms = 0; return SL_RESULT_SUCCESS; }

static SLresult vol_SetVolumeLevel(SLVolumeItf self, int mb) { DLOG("vol.SetVolumeLevel %d", mb); return SL_RESULT_SUCCESS; }
static SLresult vol_GetVolumeLevel(SLVolumeItf self, int* mb) { if (mb) *mb = 0; return SL_RESULT_SUCCESS; }
static SLresult vol_GetMax(SLVolumeItf self, int* mb) { if (mb) *mb = 0; return SL_RESULT_SUCCESS; }
static SLresult vol_SetMute(SLVolumeItf self, SLboolean m) { return SL_RESULT_SUCCESS; }
static SLresult vol_GetMute(SLVolumeItf self, SLboolean* m) { if (m) *m = 0; return SL_RESULT_SUCCESS; }
static SLresult vol_EnableStereo(SLVolumeItf self, SLboolean e) { return SL_RESULT_SUCCESS; }
static SLresult vol_SetStereo(SLVolumeItf self, int p) { return SL_RESULT_SUCCESS; }
static SLresult vol_GetStereo(SLVolumeItf self, int* p) { if (p) *p = 0; return SL_RESULT_SUCCESS; }

static SLresult cfg_SetConfiguration(SLAndroidConfigurationItf self, SLuint32 key, const void* val, SLuint32 size) { DLOG("cfg.SetConfiguration key=%u", key); return SL_RESULT_SUCCESS; }
static SLresult cfg_GetConfiguration(SLAndroidConfigurationItf self, SLuint32 key, void* val, SLuint32* size) { return SL_RESULT_SUCCESS; }
static SLresult cfg_SetCallback(SLAndroidConfigurationItf self, SLuint32 key, void (*cb)(SLAndroidConfigurationItf, void*, SLuint32, const void*, SLuint32)) { return SL_RESULT_SUCCESS; }
static SLresult cfg_SetConfigCallback(SLAndroidConfigurationItf self, void (*cb)(SLAndroidConfigurationItf, void*, SLuint32, const void*, SLuint32)) { return SL_RESULT_SUCCESS; }

/* ========================= interface dispatch ========================= */
static void* match_if(const void* iid) {
    /* FMOD passes the iid as a 32-bit VALUE (first word of its constant). */
    unsigned v = (unsigned)(unsigned long)iid;
    DLOG("match_if iid=0x%x", v);
    if (v == 0x2e || v == 0x80) return &g_bq_if_h;   /* bufferqueue / androidsimplebufferqueue */
    if (v == 0x2f) return &g_play_if_h;              /* play */
    if (v == 0x28) return &g_vol_if_h;               /* volume */
    if (v == 0x2b) return &g_cfg_if_h;               /* androidconfiguration */
    return &g_bq_if_h;
}

static SLresult player_GetInterface(SLObjectItf self, const void* iid, void* p) {
    void* v = match_if(iid);
    DLOG("player_GetInterface -> %p", v);
    if (p) *(void**)p = v;
    return SL_RESULT_SUCCESS;
}
static SLresult mix_GetInterface(SLObjectItf self, const void* iid, void* p) {
    if (p) *(void**)p = &g_vol_if_h;
    return SL_RESULT_SUCCESS;
}
static SLresult eng_GetInterface(SLEngineItf self, const void* iid, void* p) {
    if (p) *(void**)p = &g_engine_if_h;
    return SL_RESULT_SUCCESS;
}
static SLresult engine_obj_GetInterface(SLObjectItf self, const void* iid, void* p) {
    if (p) *(void**)p = &g_engine_if_h;
    return SL_RESULT_SUCCESS;
}
static SLresult eng_CreateOutputMix(SLEngineItf self, SLObjectItf* mix, SLuint32 n, const void* ids, const SLboolean* req) {
    DLOG("eng_CreateOutputMix n=%u", n);
    if (mix) *mix = &g_mix_h;
    return SL_RESULT_SUCCESS;
}
static SLresult eng_CreateAudioPlayer(SLEngineItf self, SLObjectItf* player, void* src, void* sink, SLuint32 n, const void* ids, const SLboolean* req) {
    DLOG("eng_CreateAudioPlayer n=%u", n);
    if (src) {
        /* This FMOD build passes the format directly in some output paths
         * and inside SLDataSource in others. Accept both layouts before
         * opening SDL so the hardware format matches the mixer. */
        const uintptr_t *source = (const uintptr_t *)src;
        const SLDataFormat_PCM* pcm = (const SLDataFormat_PCM*)source[0];
        if (!pcm || pcm->formatType != SL_DATAFORMAT_PCM)
            pcm = (const SLDataFormat_PCM*)source[1];
        if (pcm && pcm->formatType == SL_DATAFORMAT_PCM) {
            g_input_freq = pcm->samplesPerSec / 1000;
            g_input_ch = pcm->numChannels;
            g_sdl_freq = g_input_freq == 24000 ? 48000 : g_input_freq;
            g_sdl_ch = g_input_ch;
            DLOG("  PCM %u Hz x %u ch x %u bit -> SDL %d Hz", g_input_freq,
                 g_input_ch, pcm->bitsPerSample, g_sdl_freq);
        } else {
            DLOG("  unknown source layout: %p %p", (void*)source[0], (void*)source[1]);
            if (g_dbg && source[0] && source[1]) {
                const SLuint32 *left = (const SLuint32 *)source[0];
                const SLuint32 *right = (const SLuint32 *)source[1];
                DLOG("  source[0]: %08x %08x %08x %08x", left[0], left[1], left[2], left[3]);
                DLOG("  source[1]: %08x %08x %08x %08x", right[0], right[1], right[2], right[3]);
            }
        }
    }
    if (!g_sdl_dev) audio_open();
    if (player) *player = &g_player_h;
    return SL_RESULT_SUCCESS;
}

static void init_all(void) {
    g_engine_obj = calloc(1, sizeof(*g_engine_obj));
    g_mix_obj = calloc(1, sizeof(*g_mix_obj));
    g_player_obj = calloc(1, sizeof(*g_player_obj));
    g_engine_if = calloc(1, sizeof(*g_engine_if));
    g_play_if = calloc(1, sizeof(*g_play_if));
    g_vol_if = calloc(1, sizeof(*g_vol_if));
    g_cfg_if = calloc(1, sizeof(*g_cfg_if));
    g_bq_if = calloc(1, sizeof(*g_bq_if));
    g_engine_h = g_engine_obj;
    g_mix_h = g_mix_obj;
    g_player_h = g_player_obj;
    g_engine_if_h = g_engine_if;
    g_play_if_h = g_play_if;
    g_vol_if_h = g_vol_if;
    g_cfg_if_h = g_cfg_if;
    g_bq_if_h = g_bq_if;

    g_engine_obj->Realize = obj_Realize; g_engine_obj->Resume = (SLresult(*)(SLObjectItf,SLboolean))noop0;
    g_engine_obj->GetState = obj_GetState;
    g_engine_obj->GetInterface = engine_obj_GetInterface;
    g_engine_obj->RegisterCallback = (SLresult(*)(SLObjectItf,void(*)(SLObjectItf,void*),void*))noop0;
    g_engine_obj->AbortAsyncOperation = noop_void;
    g_engine_obj->Destroy = obj_Destroy;

    g_mix_obj->Realize = obj_Realize; g_mix_obj->Resume = (SLresult(*)(SLObjectItf,SLboolean))noop0;
    g_mix_obj->GetState = obj_GetState;
    g_mix_obj->GetInterface = mix_GetInterface;
    g_mix_obj->RegisterCallback = (SLresult(*)(SLObjectItf,void(*)(SLObjectItf,void*),void*))noop0;
    g_mix_obj->AbortAsyncOperation = noop_void;
    g_mix_obj->Destroy = obj_Destroy;

    g_player_obj->Realize = obj_Realize; g_player_obj->Resume = (SLresult(*)(SLObjectItf,SLboolean))noop0;
    g_player_obj->GetState = obj_GetState;
    g_player_obj->GetInterface = player_GetInterface;
    g_player_obj->RegisterCallback = (SLresult(*)(SLObjectItf,void(*)(SLObjectItf,void*),void*))noop0;
    g_player_obj->AbortAsyncOperation = noop_void;
    g_player_obj->Destroy = obj_Destroy;

    /* FMOD's SLEngineItf slot order: CreateAudioPlayer@2, CreateOutputMix@7. */
    g_engine_if->CreateLEDDevice = (SLresult(*)(SLEngineItf,SLObjectItf*,SLuint32,const void*,const SLboolean*))noop0;
    g_engine_if->CreateAudioPlayer = (SLresult(*)(SLEngineItf,SLObjectItf*,void*,void*,SLuint32,const void*,const SLboolean*))noop0;
    g_engine_if->CreateAudioRecorder = (SLresult(*)(SLEngineItf,SLObjectItf*,void*,void*,SLuint32,const void*,const SLboolean*))eng_CreateAudioPlayer;
    g_engine_if->CreateMidiPlayer = (SLresult(*)(SLEngineItf,SLObjectItf*,void*,void*,void*,SLuint32,const void*,const SLboolean*))noop0;
    g_engine_if->CreateListener = (SLresult(*)(SLEngineItf,SLObjectItf*,SLuint32,const void*,const SLboolean*))noop0;
    g_engine_if->Create3DGroup = (SLresult(*)(SLEngineItf,SLObjectItf*,SLuint32,const void*,const SLboolean*))noop0;
    g_engine_if->CreateOutputMix = (SLresult(*)(SLEngineItf,SLObjectItf*,SLuint32,const void*,const SLboolean*))noop0;
    g_engine_if->CreateMetadataExtractor = (SLresult(*)(SLEngineItf,SLObjectItf*,void*,SLuint32,const void*,const SLboolean*))eng_CreateOutputMix;
    g_engine_if->CreateExtensionObject = (SLresult(*)(SLEngineItf,SLObjectItf*,void*,SLuint32,SLuint32,const void*,const SLboolean*))noop0;
    g_engine_if->GetNumSupportedInterfaces = (SLresult(*)(SLEngineItf,SLuint32*))noop0v;
    g_engine_if->QuerySupportedInterfaces = (SLresult(*)(SLEngineItf,SLuint32,void*))noop0u;
    g_engine_if->QueryNumSupportedInterfaces = (SLresult(*)(SLEngineItf,SLuint32*))noop0v;
    g_engine_if->GetInterface = eng_GetInterface;

    g_play_if->SetPlayState = play_SetPlayState; g_play_if->GetPlayState = play_GetPlayState;
    g_play_if->GetDuration = play_GetDuration; g_play_if->GetPosition = play_GetPosition;
    g_play_if->RegisterPositionCallback = play_RegPosCb; g_play_if->SetPositionUpdatePeriod = play_SetPosPeriod;
    g_play_if->SetCallbackEventsMask = play_SetCbMask; g_play_if->GetCallbackEventsMask = play_GetCbMask;
    g_play_if->SetMarkerPosition = play_SetMarker; g_play_if->ClearMarkerPosition = play_ClearMarker;
    g_play_if->GetMarkerPosition = play_GetMarker;

    g_vol_if->SetVolumeLevel = vol_SetVolumeLevel; g_vol_if->GetVolumeLevel = vol_GetVolumeLevel;
    g_vol_if->GetMaxVolumeLevel = vol_GetMax; g_vol_if->SetMute = vol_SetMute; g_vol_if->GetMute = vol_GetMute;
    g_vol_if->EnableStereoPosition = vol_EnableStereo; g_vol_if->SetStereoPosition = vol_SetStereo;
    g_vol_if->GetStereoPosition = vol_GetStereo;

    g_cfg_if->SetConfiguration = cfg_SetConfiguration; g_cfg_if->GetConfiguration = cfg_GetConfiguration;
    g_cfg_if->SetCallback = cfg_SetCallback; g_cfg_if->SetConfigCallback = cfg_SetConfigCallback;

    g_bq_if->Enqueue = bq_Enqueue; g_bq_if->Clear = bq_Clear; g_bq_if->GetState = bq_GetState;
    g_bq_if->RegisterCallback = bq_RegisterCallback; g_bq_if->GetBuffer = bq_GetBuffer;
}

SLresult slCreateEngine(SLObjectItf* pEngine, SLuint32 numOptions, const void* pEngineOptions,
                        SLuint32 numInterfaces, const void* pInterfaceIds,
                        const SLboolean* pInterfaceRequired) {
    DLOG("slCreateEngine");
    init_all();
    if (pEngine) *pEngine = &g_engine_h;
    pthread_t bt;
    pthread_create(&bt, NULL, bq_thread_fn, NULL);
    return SL_RESULT_SUCCESS;
}
