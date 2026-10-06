#ifndef MIOPAN_THREAD_H
#define MIOPAN_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

// Host threading/synchronisation wrapper.  Confines SDL threads/mutex/sema/TLS
// to the miopan layer so the SDK's EE-kernel shim (and libgraph's vblank
// thread) don't call SDL directly.  These are neutral primitives; the SDK keeps
// its own sce*/EE naming, slot tables and scheduling policy on top.

typedef struct MioPan_Thread MioPan_Thread;
typedef struct MioPan_Sema   MioPan_Sema;
typedef struct MioPan_Mutex  MioPan_Mutex;
typedef struct MioPan_Tls    MioPan_Tls;

// Threads.  entry(arg) runs on the new thread; the handle is owned by the
// caller until MioPan_ThreadDetach.  Returns NULL on failure.
typedef void (*MioPan_ThreadEntry)(void *arg);
MioPan_Thread *MioPan_ThreadCreate(MioPan_ThreadEntry entry, void *arg,
                                   const char *name);
void           MioPan_ThreadDetach(MioPan_Thread *t);

// Counting semaphores.
MioPan_Sema *MioPan_SemaCreate(int initial_count);
void         MioPan_SemaDestroy(MioPan_Sema *s);
void         MioPan_SemaSignal(MioPan_Sema *s);
void         MioPan_SemaWait(MioPan_Sema *s);
int          MioPan_SemaTryWait(MioPan_Sema *s); // 1 if acquired, 0 otherwise

// Mutexes.
MioPan_Mutex *MioPan_MutexCreate(void);
void          MioPan_MutexDestroy(MioPan_Mutex *m);
void          MioPan_MutexLock(MioPan_Mutex *m);
void          MioPan_MutexUnlock(MioPan_Mutex *m);

// Thread-local storage of a single void* per key.
MioPan_Tls *MioPan_TlsCreate(void);
void        MioPan_TlsSet(MioPan_Tls *key, void *value);
void       *MioPan_TlsGet(MioPan_Tls *key);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_THREAD_H */
