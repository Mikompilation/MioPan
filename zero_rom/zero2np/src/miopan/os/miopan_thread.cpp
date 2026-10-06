#include "miopan_thread.h"

#include <stdlib.h>

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_mutex.h>

// The opaque handle types wrap the SDL primitives.  Threads carry a small
// trampoline so the caller's void(*)(void*) entry runs under SDL's SDLCALL
// int(void*) signature, with TLS-based identity handled by the SDK on top.

struct MioPan_Thread
{
    SDL_Thread        *sdl;
    MioPan_ThreadEntry entry;
    void              *arg;
};

struct MioPan_Sema
{
    SDL_Semaphore *sdl;
};

struct MioPan_Mutex
{
    SDL_Mutex *sdl;
};

struct MioPan_Tls
{
    SDL_TLSID id;
};

static int SDLCALL ThreadTrampoline(void *data)
{
    MioPan_Thread *t = (MioPan_Thread *)data;
    if (t != 0 && t->entry != 0)
    {
        t->entry(t->arg);
    }
    return 0;
}

extern "C" {

MioPan_Thread *MioPan_ThreadCreate(MioPan_ThreadEntry entry, void *arg,
                                   const char *name)
{
    if (entry == 0)
    {
        return 0;
    }

    MioPan_Thread *t = (MioPan_Thread *)malloc(sizeof(MioPan_Thread));
    if (t == 0)
    {
        return 0;
    }

    t->entry = entry;
    t->arg   = arg;
    t->sdl   = SDL_CreateThread(ThreadTrampoline, name != 0 ? name : "miopan-thread", t);
    if (t->sdl == 0)
    {
        free(t);
        return 0;
    }

    return t;
}

void MioPan_ThreadDetach(MioPan_Thread *t)
{
    if (t == 0)
    {
        return;
    }
    if (t->sdl != 0)
    {
        SDL_DetachThread(t->sdl);
    }
    free(t);
}

MioPan_Sema *MioPan_SemaCreate(int initial_count)
{
    MioPan_Sema *s = (MioPan_Sema *)malloc(sizeof(MioPan_Sema));
    if (s == 0)
    {
        return 0;
    }
    s->sdl = SDL_CreateSemaphore((Uint32)(initial_count < 0 ? 0 : initial_count));
    if (s->sdl == 0)
    {
        free(s);
        return 0;
    }
    return s;
}

void MioPan_SemaDestroy(MioPan_Sema *s)
{
    if (s == 0)
    {
        return;
    }
    if (s->sdl != 0)
    {
        SDL_DestroySemaphore(s->sdl);
    }
    free(s);
}

void MioPan_SemaSignal(MioPan_Sema *s)
{
    if (s != 0 && s->sdl != 0)
    {
        SDL_SignalSemaphore(s->sdl);
    }
}

void MioPan_SemaWait(MioPan_Sema *s)
{
    if (s != 0 && s->sdl != 0)
    {
        SDL_WaitSemaphore(s->sdl);
    }
}

int MioPan_SemaTryWait(MioPan_Sema *s)
{
    if (s == 0 || s->sdl == 0)
    {
        return 0;
    }
    return SDL_TryWaitSemaphore(s->sdl) ? 1 : 0;
}

MioPan_Mutex *MioPan_MutexCreate(void)
{
    MioPan_Mutex *m = (MioPan_Mutex *)malloc(sizeof(MioPan_Mutex));
    if (m == 0)
    {
        return 0;
    }
    m->sdl = SDL_CreateMutex();
    if (m->sdl == 0)
    {
        free(m);
        return 0;
    }
    return m;
}

void MioPan_MutexDestroy(MioPan_Mutex *m)
{
    if (m == 0)
    {
        return;
    }
    if (m->sdl != 0)
    {
        SDL_DestroyMutex(m->sdl);
    }
    free(m);
}

void MioPan_MutexLock(MioPan_Mutex *m)
{
    if (m != 0 && m->sdl != 0)
    {
        SDL_LockMutex(m->sdl);
    }
}

void MioPan_MutexUnlock(MioPan_Mutex *m)
{
    if (m != 0 && m->sdl != 0)
    {
        SDL_UnlockMutex(m->sdl);
    }
}

MioPan_Tls *MioPan_TlsCreate(void)
{
    MioPan_Tls *k = (MioPan_Tls *)malloc(sizeof(MioPan_Tls));
    if (k == 0)
    {
        return 0;
    }
    // SDL_TLSID is zero-initialised until first use; SDL lazily assigns it.
    SDL_TLSID zero = {0};
    k->id = zero;
    return k;
}

void MioPan_TlsSet(MioPan_Tls *key, void *value)
{
    if (key != 0)
    {
        SDL_SetTLS(&key->id, value, 0);
    }
}

void *MioPan_TlsGet(MioPan_Tls *key)
{
    if (key == 0)
    {
        return 0;
    }
    return SDL_GetTLS(&key->id);
}

}
