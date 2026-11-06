/*
 * fmodtest.c - checks which Celeste FMOD events keep playing after stop()+release().
 * Talks to the real FMOD 2 libraries directly (not the Celeste-FMOD2 shim).
 *
 * usage: ./fmodtest <bank directory> [path filter, default "event:/"] [allowfade|immediate|keyoff|delayed|probe] [version, e.g. 0x00020222]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

/*
 * No FMOD headers needed: these are the FMOD 2.02 C API declarations used here,
 * written out by hand. All handles are opaque pointers, results are ints.
 */
typedef int FMOD_RESULT;
typedef void FMOD_STUDIO_SYSTEM;
typedef void FMOD_SYSTEM;
typedef void FMOD_STUDIO_BANK;
typedef void FMOD_STUDIO_EVENTDESCRIPTION;
typedef void FMOD_STUDIO_EVENTINSTANCE;

#define FMOD_OK 0
#define FMOD_OUTPUTTYPE_NOSOUND 2
#define FMOD_INIT_NORMAL 0
#define FMOD_STUDIO_INIT_NORMAL 0
#define FMOD_STUDIO_LOAD_BANK_NORMAL 0
#define FMOD_STUDIO_STOP_ALLOWFADEOUT 0
#define FMOD_STUDIO_STOP_IMMEDIATE 1

FMOD_RESULT FMOD_Studio_System_Create(FMOD_STUDIO_SYSTEM **system, unsigned int headerversion);
FMOD_RESULT FMOD_Studio_System_GetCoreSystem(FMOD_STUDIO_SYSTEM *system, FMOD_SYSTEM **coresystem);
FMOD_RESULT FMOD_System_SetOutput(FMOD_SYSTEM *system, int output);
FMOD_RESULT FMOD_Studio_System_Initialize(FMOD_STUDIO_SYSTEM *system, int maxchannels, unsigned int studioflags, unsigned int flags, void *extradriverdata);
FMOD_RESULT FMOD_Studio_System_Update(FMOD_STUDIO_SYSTEM *system);
FMOD_RESULT FMOD_Studio_System_Release(FMOD_STUDIO_SYSTEM *system);
FMOD_RESULT FMOD_Studio_System_LoadBankFile(FMOD_STUDIO_SYSTEM *system, const char *filename, unsigned int flags, FMOD_STUDIO_BANK **bank);
FMOD_RESULT FMOD_Studio_System_GetBankList(FMOD_STUDIO_SYSTEM *system, FMOD_STUDIO_BANK **array, int capacity, int *count);
FMOD_RESULT FMOD_Studio_Bank_GetEventCount(FMOD_STUDIO_BANK *bank, int *count);
FMOD_RESULT FMOD_Studio_Bank_GetEventList(FMOD_STUDIO_BANK *bank, FMOD_STUDIO_EVENTDESCRIPTION **array, int capacity, int *count);
FMOD_RESULT FMOD_Studio_EventDescription_GetPath(FMOD_STUDIO_EVENTDESCRIPTION *desc, char *path, int size, int *retrieved);
FMOD_RESULT FMOD_Studio_EventDescription_CreateInstance(FMOD_STUDIO_EVENTDESCRIPTION *desc, FMOD_STUDIO_EVENTINSTANCE **instance);
FMOD_RESULT FMOD_Studio_EventDescription_GetInstanceCount(FMOD_STUDIO_EVENTDESCRIPTION *desc, int *count);
FMOD_RESULT FMOD_Studio_EventInstance_Start(FMOD_STUDIO_EVENTINSTANCE *instance);
FMOD_RESULT FMOD_Studio_EventInstance_Stop(FMOD_STUDIO_EVENTINSTANCE *instance, int mode);
FMOD_RESULT FMOD_Studio_EventInstance_Release(FMOD_STUDIO_EVENTINSTANCE *instance);
FMOD_RESULT FMOD_Studio_EventInstance_KeyOff(FMOD_STUDIO_EVENTINSTANCE *instance);
FMOD_RESULT FMOD_Studio_EventInstance_GetPlaybackState(FMOD_STUDIO_EVENTINSTANCE *instance, int *state);
FMOD_RESULT FMOD_Studio_EventInstance_GetVolume(FMOD_STUDIO_EVENTINSTANCE *instance, float *volume, float *finalvolume);
FMOD_RESULT FMOD_Studio_EventInstance_SetVolume(FMOD_STUDIO_EVENTINSTANCE *instance, float volume);

/* FMOD_VERSION for 2.02.22 is 0x00020222; override with the 4th argument if the
 * library reports a header mismatch (error 31), e.g. 0x00020206 for 2.02.06. */
#define DEFAULT_FMOD_VERSION 0x00020222

#define MAXEV 4096

static void check(FMOD_RESULT r, const char *what)
{
    if (r != FMOD_OK) {
        printf("FAILED: %s (FMOD error %d)\n", what, (int)r);
        exit(1);
    }
}

static void pump(FMOD_STUDIO_SYSTEM *sys, int ms)
{
    for (int t = 0; t < ms; t += 20) {
        FMOD_Studio_System_Update(sys);
        usleep(20000);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s <bank dir> [filter] [allowfade|immediate|keyoff|delayed|probe]\n", argv[0]);
        return 1;
    }
    const char *dir = argv[1];
    const char *filter = argc > 2 ? argv[2] : "event:/";
    int immediate = (argc > 3 && strcmp(argv[3], "immediate") == 0);
    int delayed = (argc > 3 && strcmp(argv[3], "delayed") == 0); /* fade-out stop, wait, report state, then force IMMEDIATE */
    int probe = (argc > 3 && strcmp(argv[3], "probe") == 0); /* measure volume over time for the first matching event */
    int keyoff = (argc > 3 && strcmp(argv[3], "keyoff") == 0); /* KeyOff, then a normal fade-out stop */

    FMOD_STUDIO_SYSTEM *sys = NULL;
    FMOD_SYSTEM *core = NULL;
    unsigned int version = argc > 4 ? (unsigned int)strtoul(argv[4], NULL, 0) : DEFAULT_FMOD_VERSION;
    check(FMOD_Studio_System_Create(&sys, version), "Studio_System_Create");
    check(FMOD_Studio_System_GetCoreSystem(sys, &core), "GetCoreSystem");
    check(FMOD_System_SetOutput(core, FMOD_OUTPUTTYPE_NOSOUND), "SetOutput(NOSOUND)");
    check(FMOD_Studio_System_Initialize(sys, 1024, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, NULL), "Initialize");

    /* load every .bank in the directory */
    DIR *d = opendir(dir);
    if (!d) { printf("cannot open %s\n", dir); return 1; }
    struct dirent *e;
    int loaded = 0;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n > 5 && strcmp(e->d_name + n - 5, ".bank") == 0) {
            char path[4096];
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            FMOD_STUDIO_BANK *b = NULL;
            FMOD_RESULT r = FMOD_Studio_System_LoadBankFile(sys, path, FMOD_STUDIO_LOAD_BANK_NORMAL, &b);
            printf("%s %s (result %d)\n", r == FMOD_OK ? "loaded" : "FAILED to load", e->d_name, (int)r);
            if (r == FMOD_OK) loaded++;
        }
    }
    closedir(d);
    if (!loaded) { printf("no banks loaded\n"); return 1; }

    /* collect matching events */
    static FMOD_STUDIO_EVENTDESCRIPTION *descs[MAXEV];
    static char names[MAXEV][256];
    static FMOD_STUDIO_EVENTINSTANCE *inst[MAXEV];
    int count = 0;

    FMOD_STUDIO_BANK *banks[128];
    int nb = 0;
    check(FMOD_Studio_System_GetBankList(sys, banks, 128, &nb), "GetBankList");
    for (int i = 0; i < nb; i++) {
        int ne = 0;
        FMOD_Studio_Bank_GetEventCount(banks[i], &ne);
        if (ne <= 0) continue;
        FMOD_STUDIO_EVENTDESCRIPTION *list[1024];
        int got = 0;
        if (FMOD_Studio_Bank_GetEventList(banks[i], list, ne > 1024 ? 1024 : ne, &got) != FMOD_OK) continue;
        for (int j = 0; j < got && count < MAXEV; j++) {
            char p[256] = "";
            int len = 0;
            FMOD_Studio_EventDescription_GetPath(list[j], p, sizeof p, &len);
            if (p[0] && strstr(p, filter)) {
                descs[count] = list[j];
                snprintf(names[count], sizeof names[count], "%s", p);
                count++;
            }
        }
    }
    printf("found %d matching events (filter \"%s\"), stop mode: %s\n",
           count, filter, immediate ? "IMMEDIATE" : delayed ? "ALLOWFADEOUT then IMMEDIATE after 3s" : keyoff ? "KEYOFF + ALLOWFADEOUT" : "ALLOWFADEOUT");
    if (!count) return 1;


    if (probe) {
        FMOD_STUDIO_EVENTINSTANCE *a = NULL, *b = NULL, *c = NULL;
        printf("probing %s\n", names[0]);

        printf("A: plain stop(ALLOWFADEOUT), never released\n");
        FMOD_Studio_EventDescription_CreateInstance(descs[0], &a);
        FMOD_Studio_EventInstance_Start(a);
        pump(sys, 2000);
        FMOD_Studio_EventInstance_Stop(a, FMOD_STUDIO_STOP_ALLOWFADEOUT);
        for (int t = 0; t <= 6000; t += 500) {
            int st = -1; float v = -1, fv = -1;
            FMOD_Studio_EventInstance_GetPlaybackState(a, &st);
            FMOD_Studio_EventInstance_GetVolume(a, &v, &fv);
            printf("  %5d ms  state %d  volume %.3f  final volume %.3f\n", t, st, v, fv);
            pump(sys, 500);
        }
        FMOD_Studio_EventInstance_Stop(a, FMOD_STUDIO_STOP_IMMEDIATE);
        FMOD_Studio_EventInstance_Release(a);

        printf("B: stop(ALLOWFADEOUT) plus an 800 ms volume ramp done with SetVolume (what the shim does)\n");
        FMOD_Studio_EventDescription_CreateInstance(descs[0], &b);
        FMOD_Studio_EventInstance_Start(b);
        pump(sys, 2000);
        FMOD_Studio_EventInstance_Stop(b, FMOD_STUDIO_STOP_ALLOWFADEOUT);
        for (int t = 0; t <= 1600; t += 100) {
            float want = 1.0f - (float)t / 800.0f;
            if (want < 0) want = 0;
            int r = FMOD_Studio_EventInstance_SetVolume(b, want);
            int st = -1; float v = -1, fv = -1;
            FMOD_Studio_EventInstance_GetPlaybackState(b, &st);
            FMOD_Studio_EventInstance_GetVolume(b, &v, &fv);
            printf("  %5d ms  set %.2f (result %d)  state %d  volume %.3f  final volume %.3f\n", t, want, r, st, v, fv);
            pump(sys, 100);
        }
        FMOD_Studio_EventInstance_Stop(b, FMOD_STUDIO_STOP_IMMEDIATE);
        FMOD_Studio_EventInstance_Release(b);

        printf("C: SetVolume(0.30) on a playing event, no stop\n");
        FMOD_Studio_EventDescription_CreateInstance(descs[0], &c);
        FMOD_Studio_EventInstance_Start(c);
        pump(sys, 1000);
        FMOD_Studio_EventInstance_SetVolume(c, 0.3f);
        pump(sys, 500);
        {
            float v = -1, fv = -1;
            FMOD_Studio_EventInstance_GetVolume(c, &v, &fv);
            printf("  volume %.3f  final volume %.3f\n", v, fv);
        }
        FMOD_Studio_EventInstance_Stop(c, FMOD_STUDIO_STOP_IMMEDIATE);
        FMOD_Studio_EventInstance_Release(c);
        FMOD_Studio_System_Release(sys);
        return 0;
    }

    /* start them all */
    for (int i = 0; i < count; i++) {
        inst[i] = NULL;
        if (FMOD_Studio_EventDescription_CreateInstance(descs[i], &inst[i]) == FMOD_OK)
            FMOD_Studio_EventInstance_Start(inst[i]);
    }
    pump(sys, 1500);

    if (delayed) {
        /* playback states: 0 PLAYING, 1 SUSTAINING, 2 STOPPED, 3 STARTING, 4 STOPPING */
        for (int i = 0; i < count; i++)
            if (inst[i]) FMOD_Studio_EventInstance_Stop(inst[i], FMOD_STUDIO_STOP_ALLOWFADEOUT);
        pump(sys, 3000);
        for (int i = 0; i < count; i++) {
            int st = -1;
            if (inst[i] && FMOD_Studio_EventInstance_GetPlaybackState(inst[i], &st) == FMOD_OK && st != 2)
                printf("3s after fade-out stop, not stopped: %s (state %d)\n", names[i], st);
        }
        for (int i = 0; i < count; i++)
            if (inst[i]) {
                FMOD_Studio_EventInstance_Stop(inst[i], FMOD_STUDIO_STOP_IMMEDIATE);
                FMOD_Studio_EventInstance_Release(inst[i]);
            }
        pump(sys, 3000);
    } else {
        /* stop + release, the same sequence Celeste's Audio.Stop() uses */
        for (int i = 0; i < count; i++) {
            if (!inst[i]) continue;
            if (keyoff) FMOD_Studio_EventInstance_KeyOff(inst[i]);
            FMOD_Studio_EventInstance_Stop(inst[i], immediate ? FMOD_STUDIO_STOP_IMMEDIATE : FMOD_STUDIO_STOP_ALLOWFADEOUT);
            FMOD_Studio_EventInstance_Release(inst[i]);
        }
        pump(sys, 8000);
    }

    /* anything still alive did not stop */
    int stuck = 0;
    for (int i = 0; i < count; i++) {
        int n = 0;
        FMOD_Studio_EventDescription_GetInstanceCount(descs[i], &n);
        if (n > 0) {
            printf("STILL PLAYING after stop+release: %s (%d instance%s)\n", names[i], n, n == 1 ? "" : "s");
            stuck++;
        }
    }
    printf("done: %d of %d events still alive\n", stuck, count);
    FMOD_Studio_System_Release(sys);
    return 0;
}
