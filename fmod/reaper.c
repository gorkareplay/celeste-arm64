/*
 * reaper.c - extra code for the Celeste-FMOD2 shim.
 *
 * Problem: under FMOD 2, some Celeste music events (B-side remixes, cassette tracks, the
 * credits) never finish when stopped with FMOD_STUDIO_STOP_ALLOWFADEOUT. They sit in the
 * "stopping" state forever, and because Celeste releases the handle right after stopping,
 * nothing can stop them later, so they keep playing and stack up.
 *
 * Fix: this file defines Stop / Release / Start / System_Update itself. The build script
 * renames the generated forwarding entries for those four functions to shim_real_<name>.
 *
 *  - Stop with a fade-out: remember the instance and do the normal fade-out stop. Events
 *    known to get stuck also get a volume ramp done here (they have no fade of their own
 *    under FMOD 2): the volume falls in equal decibel steps on every System_Update, then is
 *    set to 0, and only a few updates later is the event stopped for good. Everything else
 *    gets a long safety-net deadline.
 *  - Release of a remembered instance that is still stopping: hold the real Release back.
 *  - Every System_Update: instances that finished get their pending Release; instances past
 *    their deadline get a forced immediate stop and then the Release.
 *  - Start of a remembered instance (Celeste reusing a handle) forgets it.
 *
 * Environment overrides (milliseconds):
 *   CELESTE_FMOD_FADE_MS    fade-out length for known-stuck events (default 1500)
 *   CELESTE_FMOD_GENERAL_MS deadline for any other event          (default 15000)
 *   CELESTE_FMOD_REAPER=0   disable everything (plain forwarding)
 */
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void *H;

#define FMOD_OK 0
#define STOP_ALLOWFADEOUT 0
#define STOP_IMMEDIATE 1
#define STATE_STOPPED 2

/* renamed forwarding entries (see build script) */
extern int shim_real_FMOD_Studio_EventInstance_Stop(H inst, int mode);
extern int shim_real_FMOD_Studio_EventInstance_Release(H inst);
extern int shim_real_FMOD_Studio_EventInstance_Start(H inst);
extern int shim_real_FMOD_Studio_System_Update(H sys);

/* untouched forwarding entries */
extern int FMOD_Studio_EventInstance_GetPlaybackState(H inst, int *state);
extern int FMOD_Studio_EventInstance_GetDescription(H inst, H *desc);
extern int FMOD_Studio_EventDescription_GetPath(H desc, char *path, int size, int *retrieved);
extern int FMOD_Studio_EventInstance_GetVolume(H inst, float *volume, float *finalvolume);
extern int FMOD_Studio_EventInstance_SetVolume(H inst, float volume);

#define MAXTRACK 512
#define SILENCE_SETTLE_MS 50

struct track {
    H inst;
    int64_t deadline_ms;
    int release_pending;
    int ramp;            /* known-stuck event: fade the volume out ourselves */
    int64_t start_ms;
    float start_volume;
    int64_t silent_at_ms; /* when the volume was set to 0 at the end of the ramp */
};

static struct track tracked[MAXTRACK];
static int ntracked = 0;
static pthread_mutex_t lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int64_t env_ms(const char *name, int64_t def)
{
    const char *v = getenv(name);
    return (v && *v) ? (int64_t)strtoll(v, NULL, 10) : def;
}

static int enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("CELESTE_FMOD_REAPER");
        cached = !(v && v[0] == '0');
    }
    return cached;
}

/* events that were measured to never finish under FMOD 2 */
static int is_known_stuck(H inst)
{
    static const char *const needles[] = {
        "/remix/", "/cassette/", "menu/credits", "cassette_rooms", "seed_complete", NULL
    };
    H desc = NULL;
    char path[256] = "";
    int len = 0;
    if (FMOD_Studio_EventInstance_GetDescription(inst, &desc) != FMOD_OK || !desc) return 0;
    if (FMOD_Studio_EventDescription_GetPath(desc, path, sizeof path, &len) != FMOD_OK) return 0;
    for (int i = 0; needles[i]; i++)
        if (strstr(path, needles[i])) return 1;
    return 0;
}

static int find_locked(H inst)
{
    for (int i = 0; i < ntracked; i++)
        if (tracked[i].inst == inst) return i;
    return -1;
}

static void remove_locked(int i)
{
    tracked[i] = tracked[--ntracked];
}

int FMOD_Studio_EventInstance_Stop(H inst, int mode)
{
    int r = shim_real_FMOD_Studio_EventInstance_Stop(inst, mode);
    if (r != FMOD_OK || !enabled() || !inst) return r;

    int64_t grace = 0;
    int ramp = 0;
    float vol = 1.0f;
    if (mode == STOP_ALLOWFADEOUT) {
        if (is_known_stuck(inst)) {
            ramp = 1;
            grace = env_ms("CELESTE_FMOD_FADE_MS", 1500);
            if (FMOD_Studio_EventInstance_GetVolume(inst, &vol, NULL) != FMOD_OK) vol = 1.0f;
        } else {
            grace = env_ms("CELESTE_FMOD_GENERAL_MS", 15000);
        }
    }

    pthread_mutex_lock(&lock);
    int i = find_locked(inst);
    if (i < 0 && ntracked < MAXTRACK) {
        i = ntracked++;
        tracked[i].inst = inst;
        tracked[i].release_pending = 0;
    }
    if (i >= 0) {
        int64_t now = now_ms();
        tracked[i].deadline_ms = now + grace; /* immediate stop: due on the next update */
        tracked[i].ramp = ramp;
        tracked[i].start_ms = now;
        tracked[i].start_volume = vol;
        tracked[i].silent_at_ms = 0;
    }
    pthread_mutex_unlock(&lock);
    return r;
}

int FMOD_Studio_EventInstance_Release(H inst)
{
    if (enabled() && inst) {
        pthread_mutex_lock(&lock);
        int i = find_locked(inst);
        if (i >= 0) {
            tracked[i].release_pending = 1; /* done later, once it has really stopped */
            pthread_mutex_unlock(&lock);
            return FMOD_OK;
        }
        pthread_mutex_unlock(&lock);
    }
    return shim_real_FMOD_Studio_EventInstance_Release(inst);
}

int FMOD_Studio_EventInstance_Start(H inst)
{
    if (enabled() && inst) {
        pthread_mutex_lock(&lock);
        int i = find_locked(inst);
        if (i >= 0) remove_locked(i); /* handle is being reused, stop tracking it */
        pthread_mutex_unlock(&lock);
    }
    return shim_real_FMOD_Studio_EventInstance_Start(inst);
}

struct action {
    H inst;
    int force_stop;
    int do_release;
    int set_volume;      /* ramp step: apply 'volume' */
    float volume;
};

static void reap(void)
{
    struct action todo[MAXTRACK];
    int n = 0;
    int64_t now = now_ms();

    pthread_mutex_lock(&lock);
    for (int i = 0; i < ntracked;) {
        int state = -1;
        int gone = FMOD_Studio_EventInstance_GetPlaybackState(tracked[i].inst, &state) != FMOD_OK;
        if (gone || state == STATE_STOPPED) {
            if (!gone && tracked[i].release_pending) {
                todo[n].inst = tracked[i].inst;
                todo[n].force_stop = 0;
                todo[n].do_release = 1;
                todo[n].set_volume = 0;
                n++;
            }
            remove_locked(i);
            continue;
        }
        if (now >= tracked[i].deadline_ms) {
            if (tracked[i].ramp && tracked[i].silent_at_ms == 0) {
                /* end of the ramp: set the volume to 0 first, stop a few updates later */
                tracked[i].silent_at_ms = now;
                todo[n].inst = tracked[i].inst;
                todo[n].force_stop = 0;
                todo[n].do_release = 0;
                todo[n].set_volume = 1;
                todo[n].volume = 0.0f;
                n++;
                i++;
                continue;
            }
            if (tracked[i].ramp && now < tracked[i].silent_at_ms + SILENCE_SETTLE_MS) {
                i++;
                continue;
            }
            todo[n].inst = tracked[i].inst;
            todo[n].force_stop = 1;
            todo[n].do_release = tracked[i].release_pending;
            todo[n].set_volume = 0;
            n++;
            remove_locked(i);
            continue;
        }
        if (tracked[i].ramp) {
            int64_t total = tracked[i].deadline_ms - tracked[i].start_ms;
            float t = total > 0 ? (float)(now - tracked[i].start_ms) / (float)total : 1.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            todo[n].inst = tracked[i].inst;
            todo[n].force_stop = 0;
            todo[n].do_release = 0;
            todo[n].set_volume = 1;
            /* 60 dB over the whole fade: equal steps in decibels sound like an even fade */
            todo[n].volume = tracked[i].start_volume * powf(10.0f, -3.0f * t);
            n++;
        }
        i++;
    }
    pthread_mutex_unlock(&lock);

    /* FMOD calls happen outside the lock */
    for (int k = 0; k < n; k++) {
        if (todo[k].set_volume)
            FMOD_Studio_EventInstance_SetVolume(todo[k].inst, todo[k].volume);
        if (todo[k].force_stop)
            shim_real_FMOD_Studio_EventInstance_Stop(todo[k].inst, STOP_IMMEDIATE);
        if (todo[k].do_release)
            shim_real_FMOD_Studio_EventInstance_Release(todo[k].inst);
    }
}

int FMOD_Studio_System_Update(H sys)
{
    int r = shim_real_FMOD_Studio_System_Update(sys);
    if (enabled()) reap();
    return r;
}
