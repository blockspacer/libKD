/******************************************************************************
 * libKD
 * zlib/libpng License
 ******************************************************************************
 * Copyright (c) 2014-2016 Kevin Schmidt
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 ******************************************************************************/

/******************************************************************************
 * Implementation notes
 *
 * - Only one window is supported
 * - Networking is not supported
 * - KD_EVENT_QUIT events received by threads other then the mainthread
 *   only exit the thread
 * - To receive orientation changes AndroidManifest.xml should include
 *   android:configChanges="orientation|keyboardHidden|screenSize"
 *
 ******************************************************************************/

/******************************************************************************
 * Header workarounds
 ******************************************************************************/

/* clang-format off */
#ifdef __unix__
#   ifdef __linux__
#       define _GNU_SOURCE
#   endif
#   ifdef __EMSCRIPTEN__
#       define _POSIX_SOURCE
#   endif
#   include <sys/param.h>
#   ifdef BSD
#       define _BSD_SOURCE
#   endif
#endif

#ifdef _MSC_VER
#   define _CRT_SECURE_NO_WARNINGS 1
#endif

/******************************************************************************
 * KD includes
 ******************************************************************************/

#include <KD/kd.h>
#include <KD/kdext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/******************************************************************************
 * C includes
 ******************************************************************************/

/* Freestanding safe */
#include <float.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(KD_FREESTANDING)
#   include <errno.h>
#   include <locale.h>
#   include <stdlib.h>
#   include <stdio.h>
#   include <time.h>
#endif

/******************************************************************************
 * Platform includes
 ******************************************************************************/

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
/* CryptGenRandom etc. */
#   include <wincrypt.h>
/* R_OK/W_OK/X_OK */
#   include <direct.h>
/* _mm_* */
#   include <intrin.h>
/* MSVC redefinition fix*/
#   ifndef inline
#       define inline __inline
#   endif
#endif

#if  defined(__unix__) || defined(__APPLE__)
#   include <unistd.h>
#   include <fcntl.h>
#   include <dirent.h>
#   include <dlfcn.h>
#   include <sys/stat.h>
#   include <sys/syscall.h>
#   include <sys/utsname.h>
#   if defined(__APPLE__) || defined(BSD)
#       include <sys/mount.h>
#   else
#       include <sys/vfs.h>
#   endif
#   if defined(__linux__)
#       include <sys/prctl.h>
#   endif
#   if !defined(__TINYC__)  
#       if defined(__x86_64__) || defined(__i386__)
#           include <x86intrin.h>        
#       elif defined(__ARM_NEON__)       
#           include <arm_neon.h>     
#       endif
#   endif
#   if defined(__ANDROID__)
#       include <android/log.h>
#       include <android/native_activity.h>
#       include <android/native_window.h>
#       include <android/window.h>
#   else
#       if defined(KD_WINDOW_X11)
#           include <X11/Xlib.h>
#           include <X11/Xutil.h>
#       endif
#   endif
/* POSIX reserved but OpenKODE uses this */
#   undef st_mtime
#endif

#if defined(__EMSCRIPTEN__)
#   include <emscripten/emscripten.h>
#endif

#if defined(KD_THREAD_POSIX)
#   include <pthread.h>
#elif defined(KD_THREAD_C11)
#   include <threads.h>
#endif

#if defined(KD_ATOMIC_C11)
#   include <stdatomic.h>
#endif
/* clang-format on */

/******************************************************************************
 * Errors
 ******************************************************************************/

typedef struct __KDCallback __KDCallback;
struct KDThread {
#if defined(KD_THREAD_C11)
    thrd_t nativethread;
#elif defined(KD_THREAD_POSIX)
    pthread_t nativethread;
#elif defined(KD_THREAD_WIN32)
    HANDLE nativethread;
#endif
    void *(*start_routine)(void *);
    void *arg;
    const KDThreadAttr *attr;
    KDQueueVEN *eventqueue;
    KDDirent *lastdirent;
    KDEvent *lastevent;
    KDint lasterror;
    KDuint callbackindex;
    __KDCallback **callbacks;
    void *tlsptr;
};

/* kdGetError: Get last error indication. */
KD_API KDint KD_APIENTRY kdGetError(void)
{
    return kdThreadSelf()->lasterror;
}

/* kdSetError: Set last error indication. */
KD_API void KD_APIENTRY kdSetError(KDint error)
{
    kdThreadSelf()->lasterror = error;
}

KD_API void KD_APIENTRY kdSetErrorPlatformVEN(KDPlatformErrorVEN error, KDint allowed)
{
    KDint kderror = 0;
#if defined(_WIN32)
    switch(error)
    {
        case(ERROR_ACCESS_DENIED):
        {
            kderror = KD_EACCES;
            break;
        }
        case(ERROR_TOO_MANY_OPEN_FILES):
        {
            kderror = KD_EMFILE;
            break;
        }
        case(ERROR_FILE_NOT_FOUND):
        case(ERROR_PATH_NOT_FOUND):
        {
            kderror = KD_ENOENT;
            break;
        }
        case(ERROR_NOT_ENOUGH_MEMORY):
        case(ERROR_OUTOFMEMORY):
        {
            kderror = KD_ENOMEM;
            break;
        }
        default:
        {
            /* TODO: Handle other errorcodes */
            kdAssert(0);
        }
    }
#else
    switch(error)
    {
        case(EACCES):
        case(EROFS):
        case(EISDIR):
        {
            kderror = KD_EACCES;
            break;
        }
        case(EBADF):
        {
            kderror = KD_EBADF;
            break;
        }
        case(EBUSY):
        {
            kderror = KD_EBUSY;
            break;
        }
        case(EEXIST):
        case(ENOTEMPTY):
        {
            kderror = KD_EEXIST;
            break;
        }
        case(EFBIG):
        {
            kderror = KD_EFBIG;
            break;
        }
        case(EINVAL):
        {
            kderror = KD_EINVAL;
            break;
        }
        case(EIO):
        {
            kderror = KD_EIO;
            break;
        }
        case(EMFILE):
        case(ENFILE):
        {
            kderror = KD_EMFILE;
            break;
        }
        case(ENAMETOOLONG):
        {
            kderror = KD_ENAMETOOLONG;
            break;
        }
        case(ENOENT):
        case(ENOTDIR):
        {
            kderror = KD_ENOENT;
            break;
        }
        case(ENOMEM):
        {
            kderror = KD_ENOMEM;
            break;
        }
        case(ENOSPC):
        {
            kderror = KD_ENOSPC;
            break;
        }
        case(EOVERFLOW):
        {
            kderror = KD_EOVERFLOW;
            break;
        }
        default:
        {
            /* TODO: Handle other errorcodes */
            kdAssert(0);
        }
    }
#endif

    /* KD errors are 1 to 37*/
    for(KDint i = KD_EACCES; i <= KD_ETRY_AGAIN; i++)
    {
        if(kderror == (allowed & i))
        {
            kdSetError(kderror);
            return;
        }
    }
    /* Error is not in allowed list */
    kdAssert(0);
}

/******************************************************************************
 * Versioning and attribute queries
 ******************************************************************************/

/* kdQueryAttribi: Obtain the value of a numeric OpenKODE Core attribute. */
KD_API KDint KD_APIENTRY kdQueryAttribi(KD_UNUSED KDint attribute, KD_UNUSED KDint *value)
{
    kdSetError(KD_EINVAL);
    return -1;
}

/* kdQueryAttribcv: Obtain the value of a string OpenKODE Core attribute. */
KD_API const KDchar *KD_APIENTRY kdQueryAttribcv(KDint attribute)
{
    if(attribute == KD_ATTRIB_VENDOR)
    {
        return "libKD (zlib license)";
    }
    else if(attribute == KD_ATTRIB_VERSION)
    {
        return "1.0.3 (libKD 0.1.0)";
    }
    else if(attribute == KD_ATTRIB_PLATFORM)
    {
#if defined(_WIN32)
        return "Windows";
#elif defined(__unix__) || defined(__APPLE__)
        static struct utsname name;
        uname(&name);
        return name.sysname;
#endif
    }
    kdSetError(KD_EINVAL);
    return KD_NULL;
}

/* kdQueryIndexedAttribcv: Obtain the value of an indexed string OpenKODE Core attribute. */
KD_API const KDchar *KD_APIENTRY kdQueryIndexedAttribcv(KD_UNUSED KDint attribute, KD_UNUSED KDint __index)
{
    /* Some C implementations define an index function in string.h which leads to errors with GCC 4.6 and -Wshadow */
    kdSetError(KD_EINVAL);
    return KD_NULL;
}

/******************************************************************************
 * Threads and synchronization
 ******************************************************************************/

/* kdThreadAttrCreate: Create a thread attribute object. */
struct KDThreadAttr {
#if defined(KD_THREAD_POSIX)
    pthread_attr_t nativeattr;
#endif
    KDint detachstate;
    KDsize stacksize;
    KDchar debugname[256];
};
KD_API KDThreadAttr *KD_APIENTRY kdThreadAttrCreate(void)
{
    KDThreadAttr *attr = (KDThreadAttr *)kdMalloc(sizeof(KDThreadAttr));
    if(attr == KD_NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }

    /* Spec default */
    attr->detachstate = KD_THREAD_CREATE_JOINABLE;
    /* Impl default */
    attr->stacksize = 100000;
    kdStrcpy_s(attr->debugname, 256, "KDThread");
#if defined(KD_THREAD_POSIX)
    pthread_attr_init(&attr->nativeattr);
    pthread_attr_setdetachstate(&attr->nativeattr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&attr->nativeattr, attr->stacksize);
#endif
    return attr;
}

/* kdThreadAttrFree: Free a thread attribute object. */
KD_API KDint KD_APIENTRY kdThreadAttrFree(KDThreadAttr *attr)
{
    kdFree(attr);
    return 0;
}

/* kdThreadAttrSetDetachState: Set detachstate attribute. */
KD_API KDint KD_APIENTRY kdThreadAttrSetDetachState(KDThreadAttr *attr, KDint detachstate)
{
    if(detachstate == KD_THREAD_CREATE_JOINABLE)
    {
#if defined(KD_THREAD_POSIX)
        pthread_attr_setdetachstate(&attr->nativeattr, PTHREAD_CREATE_JOINABLE);
#endif
    }
    else if(detachstate == KD_THREAD_CREATE_DETACHED)
    {
#if defined(KD_THREAD_POSIX)
        pthread_attr_setdetachstate(&attr->nativeattr, PTHREAD_CREATE_DETACHED);
#endif
    }
    else
    {
        kdSetError(KD_EINVAL);
        return -1;
    }
    attr->detachstate = detachstate;
    return 0;
}


/* kdThreadAttrSetStackSize: Set stacksize attribute. */
KD_API KDint KD_APIENTRY kdThreadAttrSetStackSize(KDThreadAttr *attr, KDsize stacksize)
{
    attr->stacksize = stacksize;
#if defined(KD_THREAD_POSIX)
    KDint result = pthread_attr_setstacksize(&attr->nativeattr, attr->stacksize);
    if(result == EINVAL)
    {
        kdSetError(KD_EINVAL);
        return -1;
    }
#endif
    return 0;
}

/* kdThreadAttrSetDebugNameVEN: Set debugname attribute. */
KD_API KDint KD_APIENTRY kdThreadAttrSetDebugNameVEN(KDThreadAttr *attr, const char *debugname)
{
    kdStrcpy_s(attr->debugname, 256, debugname);
    return 0;
}

/* kdThreadCreate: Create a new thread. */
#if defined(KD_THREAD_C11)
static tss_t __kd_threadlocal;
#elif defined(KD_THREAD_POSIX)
static pthread_key_t __kd_threadlocal;
#elif defined(KD_THREAD_WIN32)
static DWORD __kd_threadlocal;
#else
static KDThread *__kd_threadlocal;
#endif

#if defined(KD_THREAD_C11) || defined(KD_THREAD_POSIX) || defined(KD_THREAD_WIN32)
static KDThreadOnce __kd_threadlocal_once = KD_THREAD_ONCE_INIT;
static void __kdThreadInitOnce(void)
{
#if defined(KD_THREAD_C11)
    tss_create(&__kd_threadlocal, KD_NULL);
#elif defined(KD_THREAD_POSIX)
    pthread_key_create(&__kd_threadlocal, KD_NULL);
#elif defined(KD_THREAD_WIN32)
    __kd_threadlocal = FlsAlloc(KD_NULL);
#endif
}

static void *__kdThreadStart(void *init)
{
    KDThread *thread = (KDThread *)init;
    kdThreadOnce(&__kd_threadlocal_once, __kdThreadInitOnce);
    /* Set the thread name */
    KD_UNUSED const char *threadname = thread->attr ? thread->attr->debugname : "KDThread";
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4204)
#pragma warning(disable : 6312)
#pragma warning(disable : 6322)
/* https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx */
#pragma pack(push, 8)
    struct THREADNAME_INFO {
        KDuint32 type;       // must be 0x1000
        const KDchar *name;  // pointer to name (in user addr space)
        KDuint32 threadid;   // thread ID (-1=caller thread)
        KDuint32 flags;      // reserved for future use, must be zero
    };
#pragma pack(pop)
    struct THREADNAME_INFO info = {.type = 0x1000, .name = threadname, .threadid = GetCurrentThreadId(), .flags = 0};
    if(IsDebuggerPresent())
    {
        __try
        {
            RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR *)&info);
        }
        /* clang-format off */
        __except(EXCEPTION_CONTINUE_EXECUTION)
        /* clang-format on */
        {
        }
    }
#pragma warning(pop)
#elif defined(__linux__)
    prctl(PR_SET_NAME, (long)threadname, 0UL, 0UL, 0UL);
#endif

#if defined(KD_THREAD_C11)
    tss_set(__kd_threadlocal, thread);
#elif defined(KD_THREAD_POSIX)
    pthread_setspecific(__kd_threadlocal, thread);
#if defined(__APPLE__)
    pthread_setname_np(threadname);
#endif
#elif defined(KD_THREAD_WIN32)
    FlsSetValue(__kd_threadlocal, thread);
#endif

    void *result = thread->start_routine(thread->arg);
    return result;
}
#endif

static KDThread *__kdThreadInit(void)
{
    KDThread *thread = (KDThread *)kdMalloc(sizeof(KDThread));
    if(thread == KD_NULL)
    {
        kdSetError(KD_EAGAIN);
        return KD_NULL;
    }
    thread->eventqueue = kdQueueCreateVEN(100);
    if(thread->eventqueue == KD_NULL)
    {
        kdFree(thread);
        kdSetError(KD_EAGAIN);
        return KD_NULL;
    }
    thread->lastdirent = (KDDirent *)kdMalloc(sizeof(KDDirent));
    if(thread->lastdirent == KD_NULL)
    {
        kdQueueFreeVEN(thread->eventqueue);
        kdFree(thread);
        kdSetError(KD_EAGAIN);
        return KD_NULL;
    }
    thread->lastevent = KD_NULL;
    thread->lasterror = 0;
    thread->callbackindex = 0;
    thread->callbacks = (__KDCallback **)kdMalloc(sizeof(__KDCallback *));
    if(thread->callbacks == KD_NULL)
    {
        kdFree(thread->lastdirent);
        kdQueueFreeVEN(thread->eventqueue);
        kdFree(thread);
        kdSetError(KD_EAGAIN);
        return KD_NULL;
    }
    return thread;
}

static void __kdThreadFree(KDThread *thread)
{
    for(KDsize i = 0; i < thread->callbackindex; i++)
    {
        kdFree(thread->callbacks[i]);
    }
    kdFree(thread->callbacks);
    if(thread->lastevent != KD_NULL)
    {
        kdFreeEvent(thread->lastevent);
    }
    kdFree(thread->lastdirent);
    kdQueueFreeVEN(thread->eventqueue);
    kdFree(thread);
}

KD_API KDThread *KD_APIENTRY kdThreadCreate(const KDThreadAttr *attr, void *(*start_routine)(void *), void *arg)
{
#if !defined(KD_THREAD_C11) && !defined(KD_THREAD_POSIX) && !defined(KD_THREAD_WIN32)
    kdSetError(KD_ENOSYS);
    return KD_NULL;
#endif

    KDThread *thread = __kdThreadInit();
    if(thread == KD_NULL)
    {
        kdSetError(KD_EAGAIN);
        return KD_NULL;
    }
    thread->start_routine = start_routine;
    thread->arg = arg;
    thread->attr = attr;

    KDint error = 0;
#if defined(KD_THREAD_C11)
    error = thrd_create(&thread->nativethread, (thrd_start_t)__kdThreadStart, thread);
#elif defined(KD_THREAD_POSIX)
    error = pthread_create(&thread->nativethread, attr ? &attr->nativeattr : KD_NULL, __kdThreadStart, thread);
#elif defined(KD_THREAD_WIN32)
    thread->nativethread = CreateThread(KD_NULL, attr ? attr->stacksize : 0, (LPTHREAD_START_ROUTINE)__kdThreadStart, (LPVOID)thread, 0, KD_NULL);
    error = thread->nativethread ? 0 : 1;
#else
    kdAssert(0);
#endif

    if(error != 0)
    {
        __kdThreadFree(thread);
        kdSetError(KD_EAGAIN);
        return KD_NULL;
    }

    if(attr != KD_NULL && attr->detachstate == KD_THREAD_CREATE_DETACHED)
    {
        kdThreadDetach(thread);
        __kdThreadFree(thread);
        return KD_NULL;
    }

    return thread;
}

/* kdThreadExit: Terminate this thread. */
KD_API KD_NORETURN void KD_APIENTRY kdThreadExit(void *retval)
{
    KD_UNUSED KDint result = 0;
    if(retval != KD_NULL)
    {
        result = *(KDint *)retval;
    }
#if defined(KD_THREAD_C11)
    thrd_exit(result);
#elif defined(KD_THREAD_POSIX)
    pthread_exit(retval);
#elif defined(KD_THREAD_WIN32)
    ExitThread(result);
#endif
    while(1)
    {
        ;
    }
}

/* kdThreadJoin: Wait for termination of another thread. */
KD_API KDint KD_APIENTRY kdThreadJoin(KDThread *thread, void **retval)
{
    KD_UNUSED KDint error = 0;
    KDint resinit = 0;
    KD_UNUSED KDint *result = &resinit;
    if(retval != KD_NULL)
    {
        result = *retval;
    }
#if defined(KD_THREAD_C11)
    error = thrd_join(thread->nativethread, result);
    if(error == thrd_error)
#elif defined(KD_THREAD_POSIX)
    error = pthread_join(thread->nativethread, retval);
    if(error == EINVAL || error == ESRCH)
#elif defined(KD_THREAD_WIN32)
    error = WaitForSingleObject(thread->nativethread, INFINITE);
    GetExitCodeThread(thread->nativethread, (LPDWORD)result);
    CloseHandle(thread->nativethread);
    if(error != 0)
#else
    kdAssert(0);
#endif
    {
        kdSetError(KD_EINVAL);
        return -1;
    }
    __kdThreadFree(thread);
    return 0;
}

/* kdThreadDetach: Allow resources to be freed as soon as a thread terminates. */
KD_API KDint KD_APIENTRY kdThreadDetach(KDThread *thread)
{
    KDint error = 0;
#if defined(KD_THREAD_C11)
    error = thrd_detach(thread->nativethread);
#elif defined(KD_THREAD_POSIX)
    KDint detachstate = 0;
    error = pthread_attr_getdetachstate(&thread->attr->nativeattr, &detachstate);
    /* Already detached */
    if(error == 0 && detachstate == PTHREAD_CREATE_DETACHED)
    {
        error = pthread_detach(thread->nativethread);
    }
#elif defined(KD_THREAD_WIN32)
    CloseHandle(thread->nativethread);
#else
    kdAssert(0);
#endif
    if(error != 0)
    {
        kdSetError(KD_EINVAL);
        return -1;
    }
    return 0;
}

/* kdThreadSelf: Return calling thread's ID. */
KD_API KDThread *KD_APIENTRY kdThreadSelf(void)
{
#if defined(KD_THREAD_C11)
    return tss_get(__kd_threadlocal);
#elif defined(KD_THREAD_POSIX)
    return pthread_getspecific(__kd_threadlocal);
#elif defined(KD_THREAD_WIN32)
    return FlsGetValue(__kd_threadlocal);
#else
    return __kd_threadlocal;
#endif
}

/* kdThreadOnce: Wrap initialization code so it is executed only once. */
#ifndef KD_NO_STATIC_DATA
#if defined(KD_THREAD_WIN32)
static BOOL CALLBACK call_once_callback(KD_UNUSED PINIT_ONCE flag, PVOID param, KD_UNUSED PVOID *context)
{
    void (*func)(void) = KD_NULL;
    kdMemcpy(&func, &param, sizeof(param));
    func();
    return TRUE;
}
#endif
KD_API KDint KD_APIENTRY kdThreadOnce(KDThreadOnce *once_control, void (*init_routine)(void))
{
#if defined(KD_THREAD_C11)
    call_once((once_flag *)once_control, init_routine);
#elif defined(KD_THREAD_POSIX)
    pthread_once((pthread_once_t *)once_control, init_routine);
#elif defined(KD_THREAD_WIN32)
    void *pfunc = KD_NULL;
    kdMemcpy(&pfunc, &init_routine, sizeof(init_routine));
    InitOnceExecuteOnce((PINIT_ONCE)once_control, call_once_callback, pfunc, NULL);
#else
    if(once_control == 0)
    {
        once_control = (void *)1;
        init_routine();
    }
#endif
    return 0;
}
#endif /* ndef KD_NO_STATIC_DATA */

/* kdThreadMutexCreate: Create a mutex. */
struct KDThreadMutex {
#if defined(KD_THREAD_C11)
    mtx_t nativemutex;
#elif defined(KD_THREAD_POSIX)
    pthread_mutex_t nativemutex;
#elif defined(KD_THREAD_WIN32)
    SRWLOCK nativemutex;
#else
    KDboolean nativemutex;
#endif
};
KD_API KDThreadMutex *KD_APIENTRY kdThreadMutexCreate(KD_UNUSED const void *mutexattr)
{
    /* TODO: Write KDThreadMutexAttr extension */
    KDThreadMutex *mutex = (KDThreadMutex *)kdMalloc(sizeof(KDThreadMutex));
    if(mutex == KD_NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    KDint error = 0;
#if defined(KD_THREAD_C11)
    error = mtx_init(&mutex->nativemutex, mtx_plain);
#elif defined(KD_THREAD_POSIX)
    error = pthread_mutex_init(&mutex->nativemutex, KD_NULL);
    if(error == ENOMEM)
    {
        kdSetError(KD_ENOMEM);
        kdFree(mutex);
        return KD_NULL;
    }
#elif defined(KD_THREAD_WIN32)
    InitializeSRWLock(&mutex->nativemutex);
#else
    mutex->nativemutex = 0;
#endif
    if(error != 0)
    {
        kdSetError(KD_EAGAIN);
        kdFree(mutex);
        return KD_NULL;
    }
    return mutex;
}

/* kdThreadMutexFree: Free a mutex. */
KD_API KDint KD_APIENTRY kdThreadMutexFree(KDThreadMutex *mutex)
{
#if defined(KD_THREAD_C11)
    mtx_destroy(&mutex->nativemutex);
#elif defined(KD_THREAD_POSIX)
    pthread_mutex_destroy(&mutex->nativemutex);
#elif defined(KD_THREAD_WIN32)
/* No need to free anything */
#endif
    kdFree(mutex);
    return 0;
}

/* kdThreadMutexLock: Lock a mutex. */
KD_API KDint KD_APIENTRY kdThreadMutexLock(KDThreadMutex *mutex)
{
#if defined(KD_THREAD_C11)
    mtx_lock(&mutex->nativemutex);
#elif defined(KD_THREAD_POSIX)
    pthread_mutex_lock(&mutex->nativemutex);
#elif defined(KD_THREAD_WIN32)
    AcquireSRWLockExclusive(&mutex->nativemutex);
#else
    mutex->nativemutex = 1;
#endif
    return 0;
}

/* kdThreadMutexUnlock: Unlock a mutex. */
KD_API KDint KD_APIENTRY kdThreadMutexUnlock(KDThreadMutex *mutex)
{
#if defined(KD_THREAD_C11)
    mtx_unlock(&mutex->nativemutex);
#elif defined(KD_THREAD_POSIX)
    pthread_mutex_unlock(&mutex->nativemutex);
#elif defined(KD_THREAD_WIN32)
    ReleaseSRWLockExclusive(&mutex->nativemutex);
#else
    mutex->nativemutex = 0;
#endif
    return 0;
}

/* kdThreadCondCreate: Create a condition variable. */
struct KDThreadCond {
#if defined(KD_THREAD_C11)
    cnd_t nativecond;
#elif defined(KD_THREAD_POSIX)
    pthread_cond_t nativecond;
#elif defined(KD_THREAD_WIN32)
    CONDITION_VARIABLE nativecond;
#else
    KDint placebo;
#endif
};
KD_API KDThreadCond *KD_APIENTRY kdThreadCondCreate(KD_UNUSED const void *attr)
{
#if !defined(KD_THREAD_C11) && !defined(KD_THREAD_POSIX) && !defined(KD_THREAD_WIN32)
    kdSetError(KD_ENOSYS);
    return KD_NULL;
#endif

    KDThreadCond *cond = (KDThreadCond *)kdMalloc(sizeof(KDThreadCond));
    if(cond == KD_NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    KDint error = 0;
#if defined(KD_THREAD_C11)
    error = cnd_init(&cond->nativecond);
    if(error == thrd_nomem)
    {
        kdSetError(KD_ENOMEM);
        kdFree(cond);
        return KD_NULL;
    }
#elif defined(KD_THREAD_POSIX)
    error = pthread_cond_init(&cond->nativecond, KD_NULL);
#elif defined(KD_THREAD_WIN32)
    InitializeConditionVariable(&cond->nativecond);
#else
    kdAssert(0);
#endif
    if(error != 0)
    {
        kdSetError(KD_EAGAIN);
        kdFree(cond);
        return KD_NULL;
    }
    return cond;
}

/* kdThreadCondFree: Free a condition variable. */
KD_API KDint KD_APIENTRY kdThreadCondFree(KDThreadCond *cond)
{
#if defined(KD_THREAD_C11)
    cnd_destroy(&cond->nativecond);
#elif defined(KD_THREAD_POSIX)
    pthread_cond_destroy(&cond->nativecond);
#elif defined(KD_THREAD_WIN32)
/* No need to free anything */
#else
    kdAssert(0);
#endif
    kdFree(cond);
    return 0;
}

/* kdThreadCondSignal, kdThreadCondBroadcast: Signal a condition variable. */
KD_API KDint KD_APIENTRY kdThreadCondSignal(KDThreadCond *cond)
{
#if defined(KD_THREAD_C11)
    cnd_signal(&cond->nativecond);
#elif defined(KD_THREAD_POSIX)
    pthread_cond_signal(&cond->nativecond);
#elif defined(KD_THREAD_WIN32)
    WakeConditionVariable(&cond->nativecond);
#else
    kdAssert(0);
#endif
    return 0;
}

KD_API KDint KD_APIENTRY kdThreadCondBroadcast(KDThreadCond *cond)
{
#if defined(KD_THREAD_C11)
    cnd_broadcast(&cond->nativecond);
#elif defined(KD_THREAD_POSIX)
    pthread_cond_broadcast(&cond->nativecond);
#elif defined(KD_THREAD_WIN32)
    WakeAllConditionVariable(&cond->nativecond);
#else
    kdAssert(0);
#endif
    return 0;
}

/* kdThreadCondWait: Wait for a condition variable to be signalled. */
KD_API KDint KD_APIENTRY kdThreadCondWait(KDThreadCond *cond, KDThreadMutex *mutex)
{
#if defined(KD_THREAD_C11)
    cnd_wait(&cond->nativecond, &mutex->nativemutex);
#elif defined(KD_THREAD_POSIX)
    pthread_cond_wait(&cond->nativecond, &mutex->nativemutex);
#elif defined(KD_THREAD_WIN32)
    SleepConditionVariableSRW(&cond->nativecond, &mutex->nativemutex, INFINITE, 0);
#else
    kdAssert(0);
#endif
    return 0;
}

/* kdThreadSemCreate: Create a semaphore. */
struct KDThreadSem {
    KDuint count;
    KDThreadMutex *mutex;
#if defined(KD_THREAD_C11) || defined(KD_THREAD_POSIX) || defined(KD_THREAD_WIN32)
    KDThreadCond *condition;
#endif
};
KD_API KDThreadSem *KD_APIENTRY kdThreadSemCreate(KDuint value)
{
    KDThreadSem *sem = (KDThreadSem *)kdMalloc(sizeof(KDThreadSem));
    if(sem == KD_NULL)
    {
        kdSetError(KD_ENOSPC);
        return KD_NULL;
    }

    sem->count = value;
    sem->mutex = kdThreadMutexCreate(KD_NULL);
    if(sem->mutex == KD_NULL)
    {
        kdFree(sem);
        kdSetError(KD_ENOSPC);
        return KD_NULL;
    }
#if defined(KD_THREAD_C11) || defined(KD_THREAD_POSIX) || defined(KD_THREAD_WIN32)
    sem->condition = kdThreadCondCreate(KD_NULL);
    if(sem->condition == KD_NULL)
    {
        kdThreadMutexFree(sem->mutex);
        kdFree(sem);
        kdSetError(KD_ENOSPC);
        return KD_NULL;
    }
#endif
    return sem;
}

/* kdThreadSemFree: Free a semaphore. */
KD_API KDint KD_APIENTRY kdThreadSemFree(KDThreadSem *sem)
{
    kdThreadMutexFree(sem->mutex);
#if defined(KD_THREAD_C11) || defined(KD_THREAD_POSIX) || defined(KD_THREAD_WIN32)
    kdThreadCondFree(sem->condition);
#endif
    kdFree(sem);
    return 0;
}

/* kdThreadSemWait: Lock a semaphore. */
KD_API KDint KD_APIENTRY kdThreadSemWait(KDThreadSem *sem)
{
    kdThreadMutexLock(sem->mutex);
#if defined(KD_THREAD_C11) || defined(KD_THREAD_POSIX) || defined(KD_THREAD_WIN32)
    while(sem->count == 0)
    {
        kdThreadCondWait(sem->condition, sem->mutex);
    }
#endif
    --sem->count;
    kdThreadMutexUnlock(sem->mutex);
    return 0;
}

/* kdThreadSemPost: Unlock a semaphore. */
KD_API KDint KD_APIENTRY kdThreadSemPost(KDThreadSem *sem)
{
    kdThreadMutexLock(sem->mutex);
    ++sem->count;
#if defined(KD_THREAD_C11) || defined(KD_THREAD_POSIX) || defined(KD_THREAD_WIN32)
    kdThreadCondSignal(sem->condition);
#endif
    kdThreadMutexUnlock(sem->mutex);
    return 0;
}

/* kdThreadSleepVEN: Blocks the current thread for nanoseconds. */
KD_API KDint KD_APIENTRY kdThreadSleepVEN(KDust timeout)
{
#if defined(KD_THREAD_C11) || defined(KD_THREAD_POSIX)
    struct timespec ts = {0};
    /* Determine seconds from the overall nanoseconds */
    if((timeout % 1000000000) == 0)
    {
        ts.tv_sec = (timeout / 1000000000);
    }
    else
    {
        ts.tv_sec = (timeout - (timeout % 1000000000)) / 1000000000;
    }

    /* Remaining nanoseconds */
    ts.tv_nsec = (KDint32)timeout - ((KDint32)ts.tv_sec * 1000000000);
#endif

#if defined(KD_THREAD_C11)
    thrd_sleep(&ts, NULL);
#elif defined(KD_THREAD_POSIX)
#ifdef __EMSCRIPTEN__
    emscripten_sleep_with_yield(timeout / 1000000);
#else
    nanosleep(&ts, NULL);
#endif
#elif defined(KD_THREAD_WIN32)
    HANDLE timer = CreateWaitableTimer(KD_NULL, 1, KD_NULL);
    if(!timer)
    {
        kdAssert(0);
    }
    LARGE_INTEGER li = {{0}};
    li.QuadPart = -(timeout / 100);
    if(!SetWaitableTimer(timer, &li, 0, KD_NULL, KD_NULL, 0))
    {
        kdAssert(0);
    }
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
#else
    KDust now, then;
    now = then = kdGetTimeUST();
    while((now - then) < timeout)
    {
        now = kdGetTimeUST();
    }
#endif
    return 0;
}

/******************************************************************************
 * Events
 ******************************************************************************/

/* kdWaitEvent: Get next event from thread's event queue. */
KD_API const KDEvent *KD_APIENTRY kdWaitEvent(KDust timeout)
{
    KDQueueVEN *eventqueue = kdThreadSelf()->eventqueue;
    KDEvent *lastevent = kdThreadSelf()->lastevent;
    if(lastevent != KD_NULL)
    {
        kdFreeEvent(lastevent);
    }
    if(timeout != -1)
    {
        kdThreadSleepVEN(timeout);
    }
    kdPumpEvents();
    if(kdQueueSizeVEN(eventqueue) > 0)
    {
        lastevent = (KDEvent *)kdQueuePopHeadVEN(eventqueue);
    }
    else
    {
        lastevent = KD_NULL;
        kdSetError(KD_EAGAIN);
    }
    return lastevent;
}

/* kdSetEventUserptr: Set the userptr for global events. */
static void *__kd_userptr = KD_NULL;
static KDThreadMutex *__kd_userptrmtx = KD_NULL;
KD_API void KD_APIENTRY kdSetEventUserptr(KD_UNUSED void *userptr)
{
    kdThreadMutexLock(__kd_userptrmtx);
    __kd_userptr = userptr;
    kdThreadMutexUnlock(__kd_userptrmtx);
}

/* kdDefaultEvent: Perform default processing on an unrecognized event. */
KD_API void KD_APIENTRY kdDefaultEvent(const KDEvent *event)
{
    if(event)
    {
        if(event->type == KD_EVENT_QUIT)
        {
            kdThreadExit(KD_NULL);
        }
    }
}

/* kdPumpEvents: Pump the thread's event queue, performing callbacks. */
struct __KDCallback {
    KDCallbackFunc *func;
    KDint eventtype;
    void *eventuserptr;
};
static KDboolean __kdExecCallback(KDEvent *event)
{
    KDuint callbackindex = kdThreadSelf()->callbackindex;
    __KDCallback **callbacks = kdThreadSelf()->callbacks;
    for(KDuint i = 0; i < callbackindex; i++)
    {
        if(callbacks[i]->func != KD_NULL)
        {
            KDboolean typematch = callbacks[i]->eventtype == event->type || callbacks[i]->eventtype == 0;
            KDboolean userptrmatch = callbacks[i]->eventuserptr == event->userptr;
            if(typematch && userptrmatch)
            {
                callbacks[i]->func(event);
                kdFreeEvent(event);
                return 1;
            }
        }
    }
    return 0;
}

#if defined(KD_WINDOW_SUPPORTED)
struct KDWindow {
    EGLNativeWindowType nativewindow;
    EGLNativeDisplayType nativedisplay;
    EGLint format;
    void *eventuserptr;
    KDThread *originthr;
};
#if defined(KD_WINDOW_ANDROID)
static AInputQueue *__kd_androidinputqueue = KD_NULL;
static KDThreadMutex *__kd_androidinputqueue_mutex = KD_NULL;
#endif
static KDWindow *__kd_window = KD_NULL;
#endif
KD_API KDint KD_APIENTRY kdPumpEvents(void)
{
#ifdef __EMSCRIPTEN__
    /* Give back control to the browser */
    emscripten_sleep_with_yield(1);
#endif
    KDsize queuesize = kdQueueSizeVEN(kdThreadSelf()->eventqueue);
    for(KDuint i = 0; i < queuesize; i++)
    {
        KDEvent *callbackevent = kdQueuePopHeadVEN(kdThreadSelf()->eventqueue);
        if(callbackevent != KD_NULL)
        {
            if(!__kdExecCallback(callbackevent))
            {
                /* Not a callback */
                kdPostEvent(callbackevent);
            }
        }
    }
#if defined(KD_WINDOW_SUPPORTED)
#if defined(KD_WINDOW_ANDROID)
    AInputEvent *aevent = NULL;
    kdThreadMutexLock(__kd_androidinputqueue_mutex);
    if(__kd_androidinputqueue != KD_NULL)
    {
        while(AInputQueue_getEvent(__kd_androidinputqueue, &aevent) >= 0)
        {
            AInputQueue_preDispatchEvent(__kd_androidinputqueue, aevent);
            KDEvent *event = kdCreateEvent();
            switch(AInputEvent_getType(aevent))
            {
                case(AINPUT_EVENT_TYPE_KEY):
                {
                    switch(AKeyEvent_getKeyCode(aevent))
                    {
                        case(AKEYCODE_BACK):
                        default:
                        {
                            kdFreeEvent(event);
                            break;
                        }
                    }
                    break;
                }
                default:
                {
                    kdFreeEvent(event);
                    break;
                }
            }
            AInputQueue_finishEvent(__kd_androidinputqueue, aevent, 1);
        }
    }
    kdThreadMutexUnlock(__kd_androidinputqueue_mutex);
#elif defined(KD_WINDOW_WIN32)
    if(__kd_window)
    {
        MSG msg = {0};
        while(PeekMessage(&msg, __kd_window->nativewindow, 0, 0, PM_REMOVE) != 0)
        {
            KDEvent *event = kdCreateEvent();
            switch(msg.message)
            {
                case WM_CLOSE:
                case WM_DESTROY:
                case WM_QUIT:
                {
                    ShowWindow(__kd_window->nativewindow, SW_HIDE);
                    event->type = KD_EVENT_QUIT;
                    if(!__kdExecCallback(event))
                    {
                        kdPostEvent(event);
                    }
                    break;
                }
                case WM_INPUT:
                {
                    KDchar buffer[sizeof(RAWINPUT)] = {0};
                    KDsize size = sizeof(RAWINPUT);
                    GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, buffer, (PUINT)&size, sizeof(RAWINPUTHEADER));
                    RAWINPUT *raw = (RAWINPUT *)buffer;
                    if(raw->header.dwType == RIM_TYPEMOUSE)
                    {
                        if(raw->data.mouse.usButtonFlags == RI_MOUSE_LEFT_BUTTON_DOWN ||
                            raw->data.mouse.usButtonFlags == RI_MOUSE_RIGHT_BUTTON_DOWN ||
                            raw->data.mouse.usButtonFlags == RI_MOUSE_MIDDLE_BUTTON_DOWN)
                        {
                            event->type = KD_EVENT_INPUT_POINTER;
                            event->data.inputpointer.index = KD_INPUT_POINTER_SELECT;
                            event->data.inputpointer.select = 1;
                            event->data.inputpointer.x = raw->data.mouse.lLastX;
                            event->data.inputpointer.y = raw->data.mouse.lLastY;
                            if(!__kdExecCallback(event))
                            {
                                kdPostEvent(event);
                            }
                        }
                        else if(raw->data.mouse.usButtonFlags == RI_MOUSE_LEFT_BUTTON_UP ||
                            raw->data.mouse.usButtonFlags == RI_MOUSE_RIGHT_BUTTON_UP ||
                            raw->data.mouse.usButtonFlags == RI_MOUSE_MIDDLE_BUTTON_UP)
                        {
                            event->type = KD_EVENT_INPUT_POINTER;
                            event->data.inputpointer.index = KD_INPUT_POINTER_SELECT;
                            event->data.inputpointer.select = 0;
                            event->data.inputpointer.x = raw->data.mouse.lLastX;
                            event->data.inputpointer.y = raw->data.mouse.lLastY;
                            if(!__kdExecCallback(event))
                            {
                                kdPostEvent(event);
                            }
                        }
                        else if(raw->data.keyboard.Flags & MOUSE_MOVE_ABSOLUTE)
                        {
                            event->type = KD_EVENT_INPUT_POINTER;
                            event->data.inputpointer.index = KD_INPUT_POINTER_X;
                            event->data.inputpointer.x = raw->data.mouse.lLastX;
                            if(!__kdExecCallback(event))
                            {
                                kdPostEvent(event);
                            }
                            KDEvent *event2 = kdCreateEvent();
                            event2->type = KD_EVENT_INPUT_POINTER;
                            event2->data.inputpointer.index = KD_INPUT_POINTER_Y;
                            event2->data.inputpointer.y = raw->data.mouse.lLastY;
                            if(!__kdExecCallback(event2))
                            {
                                kdPostEvent(event2);
                            }
                        }
                        break;
                    }
                    else if(raw->header.dwType == RIM_TYPEKEYBOARD)
                    {
                        event->type = KD_EVENT_INPUT_KEY_VEN;
                        KDEventInputKeyVEN *keyevent = (KDEventInputKeyVEN *)(&event->data);

/* Press or release */
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 6313)
#endif
                        if(raw->data.keyboard.Flags & RI_KEY_MAKE)
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
                        {
                            keyevent->flags = KD_KEY_PRESS_VEN;
                        }
                        else
                        {
                            keyevent->flags = 0;
                        }

                        switch(raw->data.keyboard.VKey)
                        {
                            case(VK_UP):
                            {
                                keyevent->keycode = KD_KEY_UP_VEN;
                                break;
                            }
                            case(VK_DOWN):
                            {
                                keyevent->keycode = KD_KEY_DOWN_VEN;
                                break;
                            }
                            case(VK_LEFT):
                            {
                                keyevent->keycode = KD_KEY_LEFT_VEN;
                                break;
                            }
                            case(VK_RIGHT):
                            {
                                keyevent->keycode = KD_KEY_RIGHT_VEN;
                                break;
                            }
                            default:
                            {
                                event->type = KD_EVENT_INPUT_KEYCHAR_VEN;
                                KDEventInputKeyCharVEN *keycharevent = (KDEventInputKeyCharVEN *)(&event->data);
                                GetKeyNameText((KDint64)MapVirtualKey(raw->data.keyboard.VKey, MAPVK_VK_TO_VSC) << 16, (KDchar *)&keycharevent->character, sizeof(KDint32));
                                break;
                            }
                        }
                    }
                    if(!__kdExecCallback(event))
                    {
                        kdPostEvent(event);
                    }
                    break;
                }
                default:
                {
                    kdFreeEvent(event);
                    DispatchMessage(&msg);
                    break;
                }
            }
        }
    }
#elif defined(KD_WINDOW_X11)
    if(__kd_window)
    {
        XSelectInput(__kd_window->nativedisplay, __kd_window->nativewindow, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
        while(XPending(__kd_window->nativedisplay) > 0)
        {
            KDEvent *event = kdCreateEvent();
            XEvent xevent = {0};
            XNextEvent(__kd_window->nativedisplay, &xevent);
            switch(xevent.type)
            {
                case ButtonPress:
                {
                    event->type = KD_EVENT_INPUT_POINTER;
                    event->data.inputpointer.index = KD_INPUT_POINTER_SELECT;
                    event->data.inputpointer.select = 1;
                    event->data.inputpointer.x = xevent.xbutton.x;
                    event->data.inputpointer.y = xevent.xbutton.y;
                    if(!__kdExecCallback(event))
                    {
                        kdPostEvent(event);
                    }
                    break;
                }
                case ButtonRelease:
                {
                    event->type = KD_EVENT_INPUT_POINTER;
                    event->data.inputpointer.index = KD_INPUT_POINTER_SELECT;
                    event->data.inputpointer.select = 0;
                    event->data.inputpointer.x = xevent.xbutton.x;
                    event->data.inputpointer.y = xevent.xbutton.y;
                    if(!__kdExecCallback(event))
                    {
                        kdPostEvent(event);
                    }
                    break;
                }
                case KeyRelease:
                case KeyPress:
                {
                    KeySym keysym;
                    XLookupString(&xevent.xkey, NULL, 25, &keysym, NULL);
                    event->type = KD_EVENT_INPUT_KEY_VEN;
                    KDEventInputKeyVEN *keyevent = (KDEventInputKeyVEN *)(&event->data);

                    /* Press or release */
                    if(xevent.type == KeyPress)
                    {
                        keyevent->flags = KD_KEY_PRESS_VEN;
                    }
                    else
                    {
                        keyevent->flags = 0;
                    }

                    switch(keysym)
                    {
                        case(XK_Up):
                        {

                            keyevent->keycode = KD_KEY_UP_VEN;
                            break;
                        }
                        case(XK_Down):
                        {

                            keyevent->keycode = KD_KEY_DOWN_VEN;
                            break;
                        }
                        case(XK_Left):
                        {

                            keyevent->keycode = KD_KEY_LEFT_VEN;
                            break;
                        }
                        case(XK_Right):
                        {

                            keyevent->keycode = KD_KEY_RIGHT_VEN;
                            break;
                        }
                        default:
                        {
                            event->type = KD_EVENT_INPUT_KEYCHAR_VEN;
                            KDEventInputKeyCharVEN *keycharevent = (KDEventInputKeyCharVEN *)(&event->data);
                            keycharevent->character = (KDint32)keysym;
                            break;
                        }
                    }
                    if(!__kdExecCallback(event))
                    {
                        kdPostEvent(event);
                    }
                    break;
                }
                case MotionNotify:
                {
                    event->type = KD_EVENT_INPUT_POINTER;
                    event->data.inputpointer.index = KD_INPUT_POINTER_X;
                    event->data.inputpointer.x = xevent.xmotion.x;
                    if(!__kdExecCallback(event))
                    {
                        kdPostEvent(event);
                    }
                    KDEvent *event2 = kdCreateEvent();
                    event2->type = KD_EVENT_INPUT_POINTER;
                    event2->data.inputpointer.index = KD_INPUT_POINTER_Y;
                    event2->data.inputpointer.y = xevent.xmotion.y;
                    if(!__kdExecCallback(event2))
                    {
                        kdPostEvent(event2);
                    }
                    break;
                }
                case ConfigureNotify:
                {
                    event->type = KD_EVENT_WINDOWPROPERTY_CHANGE;

                    if(!__kdExecCallback(event))
                    {
                        kdPostEvent(event);
                    }
                    break;
                }
                case ClientMessage:
                {
                    if((Atom)xevent.xclient.data.l[0] == XInternAtom(__kd_window->nativedisplay, "WM_DELETE_WINDOW", False))
                    {
                        event->type = KD_EVENT_QUIT;
                        if(!__kdExecCallback(event))
                        {
                            kdPostEvent(event);
                        }
                        break;
                    }
                }
                case MappingNotify:
                {
                    XRefreshKeyboardMapping((XMappingEvent *)&xevent);
                    break;
                }
                default:
                {
                    kdFreeEvent(event);
                    break;
                }
            }
        }
    }
#endif
#endif
    return 0;
}

/* kdInstallCallback: Install or remove a callback function for event processing. */
KD_API KDint KD_APIENTRY kdInstallCallback(KDCallbackFunc *func, KDint eventtype, void *eventuserptr)
{
    KDuint callbackindex = kdThreadSelf()->callbackindex;
    __KDCallback **callbacks = kdThreadSelf()->callbacks;
    for(KDuint i = 0; i < callbackindex; i++)
    {
        KDboolean typematch = callbacks[i]->eventtype == eventtype || callbacks[i]->eventtype == 0;
        KDboolean userptrmatch = callbacks[i]->eventuserptr == eventuserptr;
        if(typematch && userptrmatch)
        {
            callbacks[i]->func = func;
            return 0;
        }
    }
    callbacks[callbackindex] = (__KDCallback *)kdMalloc(sizeof(__KDCallback));
    callbacks[callbackindex]->func = func;
    callbacks[callbackindex]->eventtype = eventtype;
    callbacks[callbackindex]->eventuserptr = eventuserptr;
    kdThreadSelf()->callbackindex++;
    return 0;
}

/* kdCreateEvent: Create an event for posting. */
KD_API KDEvent *KD_APIENTRY kdCreateEvent(void)
{
    KDEvent *event = (KDEvent *)kdMalloc(sizeof(KDEvent));
    if(event == KD_NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    event->timestamp = 0;
    event->type = -1;
    event->userptr = KD_NULL;
    return event;
}

/* kdPostEvent, kdPostThreadEvent: Post an event into a queue. */
KD_API KDint KD_APIENTRY kdPostEvent(KDEvent *event)
{
    return kdPostThreadEvent(event, kdThreadSelf());
}
KD_API KDint KD_APIENTRY kdPostThreadEvent(KDEvent *event, KDThread *thread)
{
    if(event->timestamp == 0)
    {
        event->timestamp = kdGetTimeUST();
    }
    kdQueuePushTailVEN(thread->eventqueue, (void *)event);
    return 0;
}

/* kdFreeEvent: Abandon an event instead of posting it. */
KD_API void KD_APIENTRY kdFreeEvent(KDEvent *event)
{
    kdFree(event);
}

/******************************************************************************
 * System events
 ******************************************************************************/
/* Header only */

/******************************************************************************
 * Application startup and exit.
 ******************************************************************************/
extern const char *__progname;
const char *__kdAppName(KD_UNUSED const char *argv0)
{
#ifdef __GLIBC__
    return __progname;
#else
    /* TODO: argv[0] is not a reliable way to get the appname */
    if(argv0 == KD_NULL)
    {
        return "";
    }
    return argv0;
#endif
}

#ifdef __ANDROID__
/* All Android events are send to the mainthread */
static KDThread *__kd_androidmainthread = KD_NULL;
static ANativeActivity *__kd_androidactivity = KD_NULL;
static KDThreadMutex *__kd_androidactivity_mutex = KD_NULL;
static void __kd_AndroidOnDestroy(ANativeActivity *activity)
{
    KDEvent *event = kdCreateEvent();
    event->type = KD_EVENT_QUIT;
    kdPostThreadEvent(event, __kd_androidmainthread);
}

static void __kd_AndroidOnStart(ANativeActivity *activity)
{
    KDEvent *event = kdCreateEvent();
    event->type = KD_EVENT_RESUME;
    kdPostThreadEvent(event, __kd_androidmainthread);
}

static void __kd_AndroidOnResume(ANativeActivity *activity)
{
    KDEvent *event = kdCreateEvent();
    event->type = KD_EVENT_RESUME;
    kdPostThreadEvent(event, __kd_androidmainthread);
}

static void *__kd_AndroidOnSaveInstanceState(ANativeActivity *activity, size_t *len)
{
    /* TODO: Save state */
    return KD_NULL;
}

static void __kd_AndroidOnPause(ANativeActivity *activity)
{
    KDEvent *event = kdCreateEvent();
    event->type = KD_EVENT_PAUSE;
    kdPostThreadEvent(event, __kd_androidmainthread);
}

static void __kd_AndroidOnStop(ANativeActivity *activity)
{
    KDEvent *event = kdCreateEvent();
    event->type = KD_EVENT_PAUSE;
    kdPostThreadEvent(event, __kd_androidmainthread);
}

static void __kd_AndroidOnConfigurationChanged(ANativeActivity *activity)
{
    KDEvent *event = kdCreateEvent();
    event->type = KD_EVENT_ORIENTATION;
    kdPostThreadEvent(event, __kd_androidmainthread);
}

static void __kd_AndroidOnLowMemory(ANativeActivity *activity)
{
    /* TODO: Avoid getting killed by Android */
}

static void __kd_AndroidOnWindowFocusChanged(ANativeActivity *activity, int focused)
{
    KDEvent *event = kdCreateEvent();
    event->type = KD_EVENT_WINDOW_FOCUS;
    event->data.windowfocus.focusstate = focused;
    kdPostThreadEvent(event, __kd_androidmainthread);
}

static ANativeWindow *__kd_androidwindow = KD_NULL;
static KDThreadMutex *__kd_androidwindow_mutex = KD_NULL;
static void __kd_AndroidOnNativeWindowCreated(ANativeActivity *activity, ANativeWindow *window)
{
    kdThreadMutexLock(__kd_androidwindow_mutex);
    __kd_androidwindow = window;
    kdThreadMutexUnlock(__kd_androidwindow_mutex);
}

static void __kd_AndroidOnNativeWindowDestroyed(ANativeActivity *activity, ANativeWindow *window)
{
    kdThreadMutexLock(__kd_androidwindow_mutex);
    __kd_androidwindow = KD_NULL;
    kdThreadMutexUnlock(__kd_androidwindow_mutex);
    KDEvent *event = kdCreateEvent();
    event->type = KD_EVENT_WINDOW_CLOSE;
    kdPostThreadEvent(event, __kd_androidmainthread);
}

static void __kd_AndroidOnInputQueueCreated(ANativeActivity *activity, AInputQueue *queue)
{
    kdThreadMutexLock(__kd_androidinputqueue_mutex);
    __kd_androidinputqueue = queue;
    kdThreadMutexUnlock(__kd_androidinputqueue_mutex);
}

static void __kd_AndroidOnInputQueueDestroyed(ANativeActivity *activity, AInputQueue *queue)
{
    kdThreadMutexLock(__kd_androidinputqueue_mutex);
    __kd_androidinputqueue = KD_NULL;
    kdThreadMutexUnlock(__kd_androidinputqueue_mutex);
}
#endif

static int __kdPreMain(int argc, char **argv)
{
    __kd_userptrmtx = kdThreadMutexCreate(KD_NULL);
#if !defined(__ANDROID__)
    KDThread *thread = __kdThreadInit();
    kdThreadOnce(&__kd_threadlocal_once, __kdThreadInitOnce);
#if defined(KD_THREAD_C11)
    tss_set(__kd_threadlocal, thread);
#elif defined(KD_THREAD_POSIX)
    pthread_setspecific(__kd_threadlocal, thread);
#elif defined(KD_THREAD_WIN32)
    FlsSetValue(__kd_threadlocal, thread);
#else
    __kd_threadlocal = thread;
#endif
#endif

    KDint result = 0;
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    typedef KDint(KD_APIENTRY * KDMAIN)(KDint, const KDchar *const *);
    KDMAIN kdmain = KD_NULL;
#endif
#if defined(__ANDROID__)
    result = kdMain(argc, (const KDchar *const *)argv);
    kdThreadMutexFree(__kd_androidactivity_mutex);
    kdThreadMutexFree(__kd_androidwindow_mutex);
    kdThreadMutexFree(__kd_androidinputqueue_mutex);
#elif defined(__EMSCRIPTEN__)
    result = kdMain(argc, (const KDchar *const *)argv);
#elif defined(_WIN32)
    HMODULE handle = GetModuleHandle(0);
    kdmain = (KDMAIN)GetProcAddress(handle, "kdMain");
    result = kdmain(argc, (const KDchar *const *)argv);
#else
    void *app = dlopen(NULL, RTLD_NOW);
    /* ISO C forbids assignment between function pointer and ‘void *’ */
    void *rawptr = dlsym(app, "kdMain");
    kdMemcpy(&kdmain, &rawptr, sizeof(rawptr));
    if(dlerror() != NULL)
    {
        kdLogMessage("Cant dlopen self. Dont strip symbols from me.\n");
        kdAssert(0);
    }
    result = kdmain(argc, (const KDchar *const *)argv);
    dlclose(app);
#endif

#if !defined(__ANDROID__)
#if defined(KD_THREAD_C11)
    tss_delete(__kd_threadlocal);
#elif defined(KD_THREAD_POSIX)
    pthread_key_delete(__kd_threadlocal);
#elif defined(KD_THREAD_WIN32)
    FlsFree(__kd_threadlocal);
#endif
    __kdThreadFree(thread);
#endif
    kdThreadMutexFree(__kd_userptrmtx);
    return result;
}

#ifdef __ANDROID__
static void *__kdAndroidPreMain(void *arg)
{
    __kdPreMain(0, KD_NULL);
    return 0;
}
void ANativeActivity_onCreate(ANativeActivity *activity, void *savedState, size_t savedStateSize)
{
    __kd_androidwindow_mutex = kdThreadMutexCreate(KD_NULL);
    __kd_androidinputqueue_mutex = kdThreadMutexCreate(KD_NULL);
    __kd_androidactivity_mutex = kdThreadMutexCreate(KD_NULL);

    activity->callbacks->onDestroy = __kd_AndroidOnDestroy;
    activity->callbacks->onStart = __kd_AndroidOnStart;
    activity->callbacks->onResume = __kd_AndroidOnResume;
    activity->callbacks->onSaveInstanceState = __kd_AndroidOnSaveInstanceState;
    activity->callbacks->onPause = __kd_AndroidOnPause;
    activity->callbacks->onStop = __kd_AndroidOnStop;
    activity->callbacks->onConfigurationChanged = __kd_AndroidOnConfigurationChanged;
    activity->callbacks->onLowMemory = __kd_AndroidOnLowMemory;
    activity->callbacks->onWindowFocusChanged = __kd_AndroidOnWindowFocusChanged;
    activity->callbacks->onNativeWindowCreated = __kd_AndroidOnNativeWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = __kd_AndroidOnNativeWindowDestroyed;
    activity->callbacks->onInputQueueCreated = __kd_AndroidOnInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = __kd_AndroidOnInputQueueDestroyed;

    kdThreadMutexLock(__kd_androidactivity_mutex);
    __kd_androidactivity = activity;
    ANativeActivity_setWindowFlags(__kd_androidactivity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    kdThreadMutexUnlock(__kd_androidactivity_mutex);

    __kd_androidmainthread = kdThreadCreate(KD_NULL, __kdAndroidPreMain, KD_NULL);
    kdThreadDetach(__kd_androidmainthread);
}
#endif


#if defined(_WIN32) && defined(KD_FREESTANDING)
int WINAPI mainCRTStartup(void)
{
    return __kdPreMain(0, KD_NULL);
}
int WINAPI WinMainCRTStartup(void)
{
    return __kdPreMain(0, KD_NULL);
}
#else
KD_API int main(int argc, char **argv)
{
    return __kdPreMain(argc, argv);
}
#endif

/* kdExit: Exit the application. */
KD_API KD_NORETURN void KD_APIENTRY kdExit(KDint status)
{
#if defined(_WIN32)
    ExitProcess(status);
    while(1)
    {
        ;
    }
#else
    exit(status);
#endif
}

/******************************************************************************
 * Utility library functions
 *
 * Notes:
 * - Based on the BSD libc developed at the University of California, Berkeley
 * - kdStrtof and __kdItoa based on K&R Second Edition
 * - kdFtostr based on http://stackoverflow.com/a/7097567
 ******************************************************************************/
/******************************************************************************
 * Copyright (c) 1990, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 ******************************************************************************/

static KDint __kdIsalpha(KDint c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

static KDint __kdIsdigit(KDint c)
{
    return ((c >= '0') && (c <= '9'));
}

static KDint __kdIsspace(KDint c)
{
    return ((c >= 0x09 && c <= 0x0D) || (c == 0x20));
}

static KDint __kdIsupper(KDint c)
{
    return ((c >= 'A') && (c <= 'Z'));
}

/* kdAbs: Compute the absolute value of an integer. */
KD_API KDint KD_APIENTRY kdAbs(KDint i)
{
    return (i < 0 ? -i : i);
}

/* kdStrtof: Convert a string to a floating point number. */
KD_API KDfloat32 KD_APIENTRY kdStrtof(const KDchar *s, KD_UNUSED KDchar **endptr)
{
    KDfloat32 val, power;
    KDint i, sign;

    for(i = 0; __kdIsspace(s[i]); i++)
    {
        ;
    }
    sign = (s[i] == '-') ? -1 : 1;
    if(s[i] == '+' || s[i] == '-')
    {
        i++;
    }
    for(val = 0.0f; __kdIsdigit(s[i]); i++)
    {
        val = 10.0f * val + (s[i] - '0');
    }
    if(s[i] == '.')
    {
        i++;
    }
    for(power = 1.0f; __kdIsdigit(s[i]); i++)
    {
        val = 10.0f * val + (s[i] - '0');
        power *= 10.0f;
    }
    return (KDfloat32)sign * val / power;
}

/* kdStrtol, kdStrtoul: Convert a string to an integer. */
KD_API KDint KD_APIENTRY kdStrtol(const KDchar *nptr, KDchar **endptr, KDint base)
{
    const KDchar *s;
    KDint64 acc, cutoff;
    KDint c;
    KDint neg, any, cutlim;
    /*
     * Ensure that base is between 2 and 36 inclusive, or the special
     * value of 0.
     */
    if(base < 0 || base == 1 || base > 36)
    {
        if(endptr != 0)
        {
            *endptr = (KDchar *)nptr;
        }
        kdSetError(KD_EINVAL);
        return 0;
    }
    /*
     * Skip white space and pick up leading +/- sign if any.
     * If base is 0, allow 0x for hex and 0 for octal, else
     * assume decimal; if base is already 16, allow 0x.
     */
    s = nptr;
    do
    {
        c = (KDuint8)*s++;
    } while(__kdIsspace(c));
    if(c == '-')
    {
        neg = 1;
        c = *s++;
    }
    else
    {
        neg = 0;
        if(c == '+')
        {
            c = *s++;
        }
    }
    if((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X'))
    {
        c = s[1];
        s += 2;
        base = 16;
    }
    if(base == 0)
    {
        base = c == '0' ? 8 : 10;
    }
    /*
     * Compute the cutoff value between legal numbers and illegal
     * numbers.  That is the largest legal value, divided by the
     * base.  An input number that is greater than this value, if
     * followed by a legal input character, is too big.  One that
     * is equal to this value may be valid or not; the limit
     * between valid and invalid numbers is then based on the last
     * digit.  For instance, if the range for intmax_t is
     * [-9223372036854775808..9223372036854775807] and the input base
     * is 10, cutoff will be set to 922337203685477580 and cutlim to
     * either 7 (neg==0) or 8 (neg==1), meaning that if we have
     * accumulated a value > 922337203685477580, or equal but the
     * next digit is > 7 (or 8), the number is too big, and we will
     * return a range error.
     *
     * Set any if any `digits' consumed; make it negative to indicate
     * overflow.
     */
    cutoff = neg ? KDINT_MIN : KDINT_MAX;
    cutlim = cutoff % base;
    cutoff /= base;
    if(neg)
    {
        if(cutlim > 0)
        {
            cutlim -= base;
            cutoff += 1;
        }
        cutlim = -cutlim;
    }
    for(acc = 0, any = 0;; c = (KDuint8)*s++)
    {
        if(__kdIsdigit(c))
        {
            c -= '0';
        }
        else if(__kdIsalpha(c))
        {
            c -= __kdIsupper(c) ? 'A' - 10 : 'a' - 10;
        }
        else
        {
            break;
        }
        if(c >= base)
        {
            break;
        }
        if(any < 0)
        {
            continue;
        }
        if(neg)
        {
            if(acc < cutoff || (acc == cutoff && c > cutlim))
            {
                any = -1;
                acc = KDINT_MIN;
                kdSetError(KD_ERANGE);
            }
            else
            {
                any = 1;
                acc *= base;
                acc -= c;
            }
        }
        else
        {
            if(acc > cutoff || (acc == cutoff && c > cutlim))
            {
                any = -1;
                acc = KDINT_MAX;
                kdSetError(KD_ERANGE);
            }
            else
            {
                any = 1;
                acc *= base;
                acc += c;
            }
        }
    }
    if(endptr != KD_NULL)
    {
        *endptr = (KDchar *)(any ? s - 1 : nptr);
    }
    return (KDint)acc;
}

KD_API KDuint KD_APIENTRY kdStrtoul(const KDchar *nptr, KDchar **endptr, KDint base)
{
    const KDchar *s;
    KDint64 acc, cutoff;
    KDint c;
    KDint neg, any, cutlim;
    /*
     * See strtoimax for comments as to the logic used.
     */
    if(base < 0 || base == 1 || base > 36)
    {
        if(endptr != 0)
        {
            *endptr = (KDchar *)nptr;
        }
        kdSetError(KD_EINVAL);
        return 0;
    }
    s = nptr;
    do
    {
        c = (KDuint8)*s++;
    } while(__kdIsspace(c));
    if(c == '-')
    {
        neg = 1;
        c = *s++;
    }
    else
    {
        neg = 0;
        if(c == '+')
        {
            c = *s++;
        }
    }
    if((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X'))
    {
        c = s[1];
        s += 2;
        base = 16;
    }
    if(base == 0)
    {
        base = c == '0' ? 8 : 10;
    }
    cutoff = KDUINT_MAX / (KDuint)base;
    cutlim = KDUINT_MAX % (KDuint)base;
    for(acc = 0, any = 0;; c = (KDuint8)*s++)
    {
        if(__kdIsdigit(c))
        {
            c -= '0';
        }
        else if(__kdIsalpha(c))
        {
            c -= __kdIsupper(c) ? 'A' - 10 : 'a' - 10;
        }
        else
        {
            break;
        }
        if(c >= base)
        {
            break;
        }
        if(any < 0)
        {
            continue;
        }
        if(acc > cutoff || (acc == cutoff && c > cutlim))
        {
            any = -1;
            acc = KDUINT_MAX;
            kdSetError(KD_ERANGE);
        }
        else
        {
            any = 1;
            acc *= (KDuint)base;
            acc += c;
        }
    }
    if(neg && any > 0)
    {
        acc = -acc;
    }
    if(endptr != 0)
    {
        *endptr = (KDchar *)(any ? s - 1 : nptr);
    }
    return (KDuint)acc;
}

/* kdLtostr, kdUltostr: Convert an integer to a string. */
static KDssize __kdItoa(KDchar *buffer, KDsize buflen, KDint number, KDint base)
{
    if(buflen == 0)
    {
        return -1;
    }

    KDint sign = number;
    if(sign < 0)
    {
        number = -number;
    }

    KDssize size = 0;
    do
    {
        buffer[size++] = (KDchar)(number % base + '0');
    } while((number /= base) > 0);

    if(sign < 0)
    {
        buffer[size++] = '-';
    }

    buffer[size] = '\0';

    KDchar temp;
    for(KDsize i = 0, j = kdStrlen(buffer) - 1; i < j; i++, j--)
    {
        temp = buffer[i];
        buffer[i] = buffer[j];
        buffer[j] = temp;
    }
    return size;
}

KD_API KDssize KD_APIENTRY kdLtostr(KDchar *buffer, KDsize buflen, KDint number)
{
    return __kdItoa(buffer, buflen, number, 10);
}

KD_API KDssize KD_APIENTRY kdUltostr(KDchar *buffer, KDsize buflen, KDuint number, KDint base)
{
    return __kdItoa(buffer, buflen, (KDint)number, base);
}

/* kdFtostr: Convert a float to a string. */
KD_API KDssize KD_APIENTRY kdFtostr(KDchar *buffer, KDsize buflen, KDfloat32 number)
{
    if(buflen == 0)
    {
        return -1;
    }
    if(number == 0.0f)
    {
        return (KDssize)kdStrcpy_s(buffer, buflen, "0");
    }

    KDboolean sign = (number < 0.0f);
    if(sign)
    {
        number = -number;
    }
    /* Calculate magnitude */
    KDint m = (KDint)(kdLogf(number) / kdLogf(10.0f));
    KDboolean exp = (m >= 14 || (sign && m >= 9) || m <= -9);
    if(sign)
    {
        *(buffer++) = '-';
    }
    /* Set up for scientific notation */
    KDint m1 = 0;
    if(exp)
    {
        if(m < 0)
        {
            m -= 1;
        }
        number = number / kdPowf(10.0f, (KDfloat32)m);
        m1 = m;
        m = 0;
    }
    if(m < 1)
    {
        m = 0;
    }
    /* Convert the number */
    KDfloat32 precision = 0.000001f;
    while(number > precision || m >= 0)
    {
        KDfloat32 weight = kdPowf(10.0f, (KDfloat32)m);
        if(weight > 0.0f && weight < KD_INFINITY)
        {
            KDint digit = (KDint)kdFloorf(number / weight);
            number -= (digit * weight);
            *(buffer++) = (KDchar)('0' + digit);
        }
        if(m == 0 && number > 0.0f)
        {
            *(buffer++) = '.';
        }
        m--;
    }
    if(exp)
    {
        /* Convert the exponent */
        *(buffer++) = 'e';
        if(m1 > 0)
        {
            *(buffer++) = '+';
        }
        else
        {
            *(buffer++) = '-';
            m1 = -m1;
        }
        m = 0;
        while(m1 > 0)
        {
            *(buffer++) = '0' + m1 % 10;
            m1 /= 10;
            m++;
        }
        buffer -= m;
        for(KDint i = 0, j = m - 1; i < j; i++, j--)
        {
            /* Swap without temporary */
            buffer[i] ^= buffer[j];
            buffer[j] ^= buffer[i];
            buffer[i] ^= buffer[j];
        }
        buffer += m;
    }
    *(buffer) = '\0';
    return m;
}

/* kdCryptoRandom: Return random data. */
KD_API KDint KD_APIENTRY kdCryptoRandom(KD_UNUSED KDuint8 *buf, KD_UNUSED KDsize buflen)
{
#if defined(_MSC_VER) && defined(_M_ARM)
    /* TODO: Implement for this platform */
    kdAssert(0);
    return -1;
#elif defined(_WIN32)
    HCRYPTPROV provider = 0;
    KDboolean error = !CryptAcquireContext(&provider, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT);
    if(error == 0)
    {
        error = !CryptGenRandom(provider, (KDuint32)buflen, buf);
    }
    CryptReleaseContext(provider, 0);
    return error ? -1 : 0;
#elif defined(__OpenBSD__)
    return getentropy(buf, buflen);
#elif defined(__EMSCRIPTEN__)
    for(KDsize i = 0; i < buflen; i++)
    {
        buf[i] = (KDuint8)(emscripten_random() * 255) % 256;
    }
    return 0;
#elif defined(__unix__) || defined(__APPLE__)
    FILE *urandom = fopen("/dev/urandom", "r");
    KDsize result = fread((void *)buf, sizeof(KDuint8), buflen, urandom);
    fclose(urandom);
    if(result != buflen)
    {
        kdSetError(KD_ENOMEM);
        return -1;
    }
    return 0;
#endif
}

/******************************************************************************
 * Locale specific functions
 ******************************************************************************/

/* kdGetLocale: Determine the current language and locale. */
KD_API const KDchar *KD_APIENTRY kdGetLocale(void)
{
#if defined(KD_FREESTANDING)
    return "";
#else
    const KDchar *result = setlocale(LC_ALL, NULL);
    if(result == NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    return result;
#endif
}

/******************************************************************************
 * Memory allocation
 ******************************************************************************/

/* kdMalloc: Allocate memory. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((__malloc__))
#endif
KD_API void *KD_APIENTRY
kdMalloc(KDsize size)
{
    void *result = KD_NULL;
#if defined(_WIN32)
    result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
#else
    result = malloc(size);
#endif
    if(result == NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    return result;
}

/* kdFree: Free allocated memory block. */
KD_API void KD_APIENTRY kdFree(void *ptr)
{
#if defined(_WIN32)
    HeapFree(GetProcessHeap(), 0, ptr);
#else
    free(ptr);
#endif
}

/* kdRealloc: Resize memory block. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((__malloc__))
#endif
KD_API void *KD_APIENTRY
kdRealloc(void *ptr, KDsize size)
{
    void *result = KD_NULL;
#if defined(_WIN32)
    result = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ptr, size);
#else
    result = realloc(ptr, size);
#endif
    if(result == NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    return result;
}

/******************************************************************************
 * Thread-local storage.
 ******************************************************************************/

/* kdGetTLS: Get the thread-local storage pointer. */
KD_API void *KD_APIENTRY kdGetTLS(void)
{
    return kdThreadSelf()->tlsptr;
}

/* kdSetTLS: Set the thread-local storage pointer. */
KD_API void KD_APIENTRY kdSetTLS(void *ptr)
{
    kdThreadSelf()->tlsptr = ptr;
}

/******************************************************************************
 * Mathematical functions
 *
 * Notes:
 * - Based on FDLIBM developed at Sun Microsystems, Inc.
 ******************************************************************************/
/******************************************************************************
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 ******************************************************************************/

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4723)
#pragma warning(disable : 4756)
#endif

/* TODO: Cleanup */
static volatile KDfloat32
    twom100 = 7.8886090522e-31f, /* 2**-100=0x0d800000 */
    tiny = 1.0e-30f,
    pio2_lo = 7.5497894159e-08f, /* 0x33a22168 */
    pi_lo = -8.7422776573e-08f;  /* 0xb3bbbd2e */

static const KDfloat32
    huge = 1.000e+30f,           /* coefficient for R(x^2) */
    pio2_hi = 1.5707962513e+00f, /* 0x3fc90fda */
    pS0 = 1.6666586697e-01f,
    pS1 = -4.2743422091e-02f,
    pS2 = -8.6563630030e-03f,
    qS1 = -7.0662963390e-01f,
    two24f = 16777216.0f,             /* 0x4b800000 */
    ln2_hi = 6.9313812256e-01f,       /* 0x3f317180 */
    ln2_lo = 9.0580006145e-06f,       /* 0x3717f7d1 */
    two25 = 3.355443200e+07f,         /* 0x4c000000 */
    twom25 = 2.9802322388e-08f,       /* 0x33000000 */
    o_threshold = 8.8721679688e+01f,  /* 0x42b17180 */
    u_threshold = -1.0397208405e+02f, /* 0xc2cff1b5 */
    invln2 = 1.4426950216e+00f,       /* 0x3fb8aa3b */
    /* |(log(1+s)-log(1-s))/s - Lg(s)| < 2**-34.24 (~[-4.95e-11, 4.97e-11]). */
    Lg1 = 6.6666662693023682e-01f, /* 0xaaaaaa.0p-24f */
    Lg2 = 4.0000972151756287e-01f, /* 0xccce13.0p-25f */
    Lg3 = 2.8498786687850952e-01f, /* 0x91e9ee.0p-25f */
    Lg4 = 2.4279078841209412e-01f, /* 0xf89e26.0p-26f */
    /* Domain [-0.34568, 0.34568], range ~[-4.278e-9, 4.447e-9]: |x*(exp(x)+1)/(exp(x)-1) - p(x)| < 2**-27.74 */
    P1 = 1.6666625440e-1f,  /*  0xaaaa8f.0p-26 */
    P2 = -2.7667332906e-3f, /* -0xb55215.0p-32 */
    /* poly coefs for (3/2)*(log(x)-2s-2/3*s**3 */
    L1 = 6.0000002384e-01f,      /* 0x3f19999a */
    L2 = 4.2857143283e-01f,      /* 0x3edb6db7 */
    L3 = 3.3333334327e-01f,      /* 0x3eaaaaab */
    L4 = 2.7272811532e-01f,      /* 0x3e8ba305 */
    L5 = 2.3066075146e-01f,      /* 0x3e6c3255 */
    L6 = 2.0697501302e-01f,      /* 0x3e53f142 */
    P3 = 6.6137559770e-05f,      /* 0x388ab355 */
    P4 = -1.6533901999e-06f,     /* 0xb5ddea0e */
    P5 = 4.1381369442e-08f,      /* 0x3331bb4c */
    lg2 = 6.9314718246e-01f,     /* 0x3f317218 */
    lg2_h = 6.93145752e-01f,     /* 0x3f317200 */
    lg2_l = 1.42860654e-06f,     /* 0x35bfbe8c */
    ovt = 4.2995665694e-08f,     /* -(128-log2(ovfl+.5ulp)) */
    cp_ = 9.6179670095e-01f,     /* 0x3f76384f =2/(3ln2) */
    cp_h = 9.6191406250e-01f,    /* 0x3f764000 =12b cp */
    cp_l = -1.1736857402e-04f,   /* 0xb8f623c6 =tail of cp_h */
    ivln2 = 1.4426950216e+00f,   /* 0x3fb8aa3b =1/ln2 */
    ivln2_h = 1.4426879883e+00f, /* 0x3fb8aa00 =16b 1/ln2*/
    ivln2_l = 7.0526075433e-06f; /* 0x36eca570 =1/ln2 tail*/

static const KDfloat64KHR
    /* |cos(x) - c(x)| < 2**-34.1 (~[-5.37e-11, 5.295e-11]). */
    C0 = -4.9999999725103100e-01, /* -0x1ffffffd0c5e81.0p-54 */
    C1 = 4.1666623323739063e-02,  /*  0x155553e1053a42.0p-57 */
    C2 = -1.3886763774609929e-03, /* -0x16c087e80f1e27.0p-62 */
    C3 = 2.4390448796277409e-05,  /*  0x199342e0ee5069.0p-68 */
    /* |sin(x)/x - s(x)| < 2**-37.5 (~[-4.89e-12, 4.824e-12]). */
    S1 = -1.6666666641626524e-01, /* -0x15555554cbac77.0p-55 */
    S2 = 8.3333293858894632e-03,  /* 0x111110896efbb2.0p-59 */
    S3 = -1.9839334836096632e-04, /* -0x1a00f9e2cae774.0p-65 */
    S4 = 2.7183114939898219e-06,  /* 0x16cd878c3b46a7.0p-71 */
    /* pio2_1:   first 25 bits of pi/2 */
    pio2_1 = 1.57079631090164184570e+00, /* 0x3FF921FB, 0x50000000 */
    /* pio2_1t:  pi/2 - pio2_1  */
    pio2_1t = 1.58932547735281966916e-08, /* 0x3E5110b4, 0x611A6263 */
    two54 = 1.80143985094819840000e+16,   /* 0x43500000, 0x00000000 */
    twom54 = 5.55111512312578270212e-17,  /* 0x3C900000, 0x00000000 */
    two24 = 1.67772160000000000000e+07,   /* 0x41700000, 0x00000000 */
    twon24 = 5.96046447753906250000e-08;  /* 0x3E700000, 0x00000000 */

static const KDfloat32 halF[2] = {
    0.5f,
    -0.5f,
};
static const KDfloat32 ln2HI[2] = {
    6.9314575195e-01f,  /* 0x3f317200 */
    -6.9314575195e-01f, /* 0xbf317200 */
};
static const KDfloat32 ln2LO[2] = {
    1.4286067653e-06f,  /* 0x35bfbe8e */
    -1.4286067653e-06f, /* 0xb5bfbe8e */
};
static const KDfloat32 bp[] = {
    1.0f, 1.5f,
};
static const KDfloat32 dp_h[] = {
    0.0f, 5.84960938e-01f, /* 0x3f15c000 */
};
static const KDfloat32 dp_l[] = {
    0.0f, 1.56322085e-06f, /* 0x35d1cfdc */
};
static const KDfloat32 Zero[] = {
    0.0f, -0.0f,
};
static const KDfloat32 atanhi[] = {
    4.6364760399e-01f, /* atan(0.5)hi 0x3eed6338 */
    7.8539812565e-01f, /* atan(1.0)hi 0x3f490fda */
    9.8279368877e-01f, /* atan(1.5)hi 0x3f7b985e */
    1.5707962513e+00f, /* atan(inf)hi 0x3fc90fda */
};
static const KDfloat32 atanlo[] = {
    5.0121582440e-09f, /* atan(0.5)lo 0x31ac3769 */
    3.7748947079e-08f, /* atan(1.0)lo 0x33222168 */
    3.4473217170e-08f, /* atan(1.5)lo 0x33140fb4 */
    7.5497894159e-08f, /* atan(inf)lo 0x33a22168 */
};

/*
 * Table of constants for 2/pi, 396 Hex digits (476 decimal) of 2/pi
 *
 *      integer array, contains the (24*i)-th to (24*i+23)-th
 *      bit of 2/pi after binary point. The corresponding
 *      floating value is
 *
 *          ipio2[i] * 2^(-24(i+1)).
 *
 * NB: This table must have at least (e0-3)/24 + jk terms.
 *     For quad precision (e0 <= 16360, jk = 6), this is 686.
 */
static const KDint32 ipio2[] = {
    0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62,
    0x95993C, 0x439041, 0xFE5163, 0xABDEBB, 0xC561B7, 0x246E3A,
    0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129,
    0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C, 0x7026B4, 0x5F7E41,
    0x3991D6, 0x398353, 0x39F49C, 0x845F8B, 0xBDF928, 0x3B1FF8,
    0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D, 0x367ECF,
    0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5,
    0xF17B3D, 0x0739F7, 0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08,
    0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3,
    0x91615E, 0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880,
    0x4D7327, 0x310606, 0x1556CA, 0x73A8C9, 0x60E27B, 0xC08C6B,
#if LDBL_MAX_EXP > 1024
#if LDBL_MAX_EXP > 16384
#error "ipio2 table needs to be expanded"
#endif
    0x47C419, 0xC367CD, 0xDCE809, 0x2A8359, 0xC4768B, 0x961CA6,
    0xDDAF44, 0xD15719, 0x053EA5, 0xFF0705, 0x3F7E33, 0xE832C2,
    0xDE4F98, 0x327DBB, 0xC33D26, 0xEF6B1E, 0x5EF89F, 0x3A1F35,
    0xCAF27F, 0x1D87F1, 0x21907C, 0x7C246A, 0xFA6ED5, 0x772D30,
    0x433B15, 0xC614B5, 0x9D19C3, 0xC2C4AD, 0x414D2C, 0x5D000C,
    0x467D86, 0x2D71E3, 0x9AC69B, 0x006233, 0x7CD2B4, 0x97A7B4,
    0xD55537, 0xF63ED7, 0x1810A3, 0xFC764D, 0x2A9D64, 0xABD770,
    0xF87C63, 0x57B07A, 0xE71517, 0x5649C0, 0xD9D63B, 0x3884A7,
    0xCB2324, 0x778AD6, 0x23545A, 0xB91F00, 0x1B0AF1, 0xDFCE19,
    0xFF319F, 0x6A1E66, 0x615799, 0x47FBAC, 0xD87F7E, 0xB76522,
    0x89E832, 0x60BFE6, 0xCDC4EF, 0x09366C, 0xD43F5D, 0xD7DE16,
    0xDE3B58, 0x929BDE, 0x2822D2, 0xE88628, 0x4D58E2, 0x32CAC6,
    0x16E308, 0xCB7DE0, 0x50C017, 0xA71DF3, 0x5BE018, 0x34132E,
    0x621283, 0x014883, 0x5B8EF5, 0x7FB0AD, 0xF2E91E, 0x434A48,
    0xD36710, 0xD8DDAA, 0x425FAE, 0xCE616A, 0xA4280A, 0xB499D3,
    0xF2A606, 0x7F775C, 0x83C2A3, 0x883C61, 0x78738A, 0x5A8CAF,
    0xBDD76F, 0x63A62D, 0xCBBFF4, 0xEF818D, 0x67C126, 0x45CA55,
    0x36D9CA, 0xD2A828, 0x8D61C2, 0x77C912, 0x142604, 0x9B4612,
    0xC459C4, 0x44C5C8, 0x91B24D, 0xF31700, 0xAD43D4, 0xE54929,
    0x10D5FD, 0xFCBE00, 0xCC941E, 0xEECE70, 0xF53E13, 0x80F1EC,
    0xC3E7B3, 0x28F8C7, 0x940593, 0x3E71C1, 0xB3092E, 0xF3450B,
    0x9C1288, 0x7B20AB, 0x9FB52E, 0xC29247, 0x2F327B, 0x6D550C,
    0x90A772, 0x1FE76B, 0x96CB31, 0x4A1679, 0xE27941, 0x89DFF4,
    0x9794E8, 0x84E6E2, 0x973199, 0x6BED88, 0x365F5F, 0x0EFDBB,
    0xB49A48, 0x6CA467, 0x427271, 0x325D8D, 0xB8159F, 0x09E5BC,
    0x25318D, 0x3974F7, 0x1C0530, 0x010C0D, 0x68084B, 0x58EE2C,
    0x90AA47, 0x02E774, 0x24D6BD, 0xA67DF7, 0x72486E, 0xEF169F,
    0xA6948E, 0xF691B4, 0x5153D1, 0xF20ACF, 0x339820, 0x7E4BF5,
    0x6863B2, 0x5F3EDD, 0x035D40, 0x7F8985, 0x295255, 0xC06437,
    0x10D86D, 0x324832, 0x754C5B, 0xD4714E, 0x6E5445, 0xC1090B,
    0x69F52A, 0xD56614, 0x9D0727, 0x50045D, 0xDB3BB4, 0xC576EA,
    0x17F987, 0x7D6B49, 0xBA271D, 0x296996, 0xACCCC6, 0x5414AD,
    0x6AE290, 0x89D988, 0x50722C, 0xBEA404, 0x940777, 0x7030F3,
    0x27FC00, 0xA871EA, 0x49C266, 0x3DE064, 0x83DD97, 0x973FA3,
    0xFD9443, 0x8C860D, 0xDE4131, 0x9D3992, 0x8C70DD, 0xE7B717,
    0x3BDF08, 0x2B3715, 0xA0805C, 0x93805A, 0x921110, 0xD8E80F,
    0xAF806C, 0x4BFFDB, 0x0F9038, 0x761859, 0x15A562, 0xBBCB61,
    0xB989C7, 0xBD4010, 0x04F2D2, 0x277549, 0xF6B6EB, 0xBB22DB,
    0xAA140A, 0x2F2689, 0x768364, 0x333B09, 0x1A940E, 0xAA3A51,
    0xC2A31D, 0xAEEDAF, 0x12265C, 0x4DC26D, 0x9C7A2D, 0x9756C0,
    0x833F03, 0xF6F009, 0x8C402B, 0x99316D, 0x07B439, 0x15200C,
    0x5BC3D8, 0xC492F5, 0x4BADC6, 0xA5CA4E, 0xCD37A7, 0x36A9E6,
    0x9492AB, 0x6842DD, 0xDE6319, 0xEF8C76, 0x528B68, 0x37DBFC,
    0xABA1AE, 0x3115DF, 0xA1AE00, 0xDAFB0C, 0x664D64, 0xB705ED,
    0x306529, 0xBF5657, 0x3AFF47, 0xB9F96A, 0xF3BE75, 0xDF9328,
    0x3080AB, 0xF68C66, 0x15CB04, 0x0622FA, 0x1DE4D9, 0xA4B33D,
    0x8F1B57, 0x09CD36, 0xE9424E, 0xA4BE13, 0xB52333, 0x1AAAF0,
    0xA8654F, 0xA5C1D2, 0x0F3F0B, 0xCD785B, 0x76F923, 0x048B7B,
    0x721789, 0x53A6C6, 0xE26E6F, 0x00EBEF, 0x584A9B, 0xB7DAC4,
    0xBA66AA, 0xCFCF76, 0x1D02D1, 0x2DF1B1, 0xC1998C, 0x77ADC3,
    0xDA4886, 0xA05DF7, 0xF480C6, 0x2FF0AC, 0x9AECDD, 0xBC5C3F,
    0x6DDED0, 0x1FC790, 0xB6DB2A, 0x3A25A3, 0x9AAF00, 0x9353AD,
    0x0457B6, 0xB42D29, 0x7E804B, 0xA707DA, 0x0EAA76, 0xA1597B,
    0x2A1216, 0x2DB7DC, 0xFDE5FA, 0xFEDB89, 0xFDBE89, 0x6C76E4,
    0xFCA906, 0x70803E, 0x156E85, 0xFF87FD, 0x073E28, 0x336761,
    0x86182A, 0xEABD4D, 0xAFE7B3, 0x6E6D8F, 0x396795, 0x5BBF31,
    0x48D784, 0x16DF30, 0x432DC7, 0x356125, 0xCE70C9, 0xB8CB30,
    0xFD6CBF, 0xA200A4, 0xE46C05, 0xA0DD5A, 0x476F21, 0xD21262,
    0x845CB9, 0x496170, 0xE0566B, 0x015299, 0x375550, 0xB7D51E,
    0xC4F133, 0x5F6E13, 0xE4305D, 0xA92E85, 0xC3B21D, 0x3632A1,
    0xA4B708, 0xD4B1EA, 0x21F716, 0xE4698F, 0x77FF27, 0x80030C,
    0x2D408D, 0xA0CD4F, 0x99A520, 0xD3A2B3, 0x0A5D2F, 0x42F9B4,
    0xCBDA11, 0xD0BE7D, 0xC1DB9B, 0xBD17AB, 0x81A2CA, 0x5C6A08,
    0x17552E, 0x550027, 0xF0147F, 0x8607E1, 0x640B14, 0x8D4196,
    0xDEBE87, 0x2AFDDA, 0xB6256B, 0x34897B, 0xFEF305, 0x9EBFB9,
    0x4F6A68, 0xA82A4A, 0x5AC44F, 0xBCF82D, 0x985AD7, 0x95C7F4,
    0x8D4D0D, 0xA63A20, 0x5F57A4, 0xB13F14, 0x953880, 0x0120CC,
    0x86DD71, 0xB6DEC9, 0xF560BF, 0x11654D, 0x6B0701, 0xACB08C,
    0xD0C0B2, 0x485551, 0x0EFB1E, 0xC37295, 0x3B06A3, 0x3540C0,
    0x7BDC06, 0xCC45E0, 0xFA294E, 0xC8CAD6, 0x41F3E8, 0xDE647C,
    0xD8649B, 0x31BED9, 0xC397A4, 0xD45877, 0xC5E369, 0x13DAF0,
    0x3C3ABA, 0x461846, 0x5F7555, 0xF5BDD2, 0xC6926E, 0x5D2EAC,
    0xED440E, 0x423E1C, 0x87C461, 0xE9FD29, 0xF3D6E7, 0xCA7C22,
    0x35916F, 0xC5E008, 0x8DD7FF, 0xE26A6E, 0xC6FDB0, 0xC10893,
    0x745D7C, 0xB2AD6B, 0x9D6ECD, 0x7B723E, 0x6A11C6, 0xA9CFF7,
    0xDF7329, 0xBAC9B5, 0x5100B7, 0x0DB2E2, 0x24BA74, 0x607DE5,
    0x8AD874, 0x2C150D, 0x0C1881, 0x94667E, 0x162901, 0x767A9F,
    0xBEFDFD, 0xEF4556, 0x367ED9, 0x13D9EC, 0xB9BA8B, 0xFC97C4,
    0x27A831, 0xC36EF1, 0x36C594, 0x56A8D8, 0xB5A8B4, 0x0ECCCF,
    0x2D8912, 0x34576F, 0x89562C, 0xE3CE99, 0xB920D6, 0xAA5E6B,
    0x9C2A3E, 0xCC5F11, 0x4A0BFD, 0xFBF4E1, 0x6D3B8E, 0x2C86E2,
    0x84D4E9, 0xA9B4FC, 0xD1EEEF, 0xC9352E, 0x61392F, 0x442138,
    0xC8D91B, 0x0AFC81, 0x6A4AFB, 0xD81C2F, 0x84B453, 0x8C994E,
    0xCC2254, 0xDC552A, 0xD6C6C0, 0x96190B, 0xB8701A, 0x649569,
    0x605A26, 0xEE523F, 0x0F117F, 0x11B5F4, 0xF5CBFC, 0x2DBC34,
    0xEEBC34, 0xCC5DE8, 0x605EDD, 0x9B8E67, 0xEF3392, 0xB817C9,
    0x9B5861, 0xBC57E1, 0xC68351, 0x103ED8, 0x4871DD, 0xDD1C2D,
    0xA118AF, 0x462C21, 0xD7F359, 0x987AD9, 0xC0549E, 0xFA864F,
    0xFC0656, 0xAE79E5, 0x362289, 0x22AD38, 0xDC9367, 0xAAE855,
    0x382682, 0x9BE7CA, 0xA40D51, 0xB13399, 0x0ED7A9, 0x480569,
    0xF0B265, 0xA7887F, 0x974C88, 0x36D1F9, 0xB39221, 0x4A827B,
    0x21CF98, 0xDC9F40, 0x5547DC, 0x3A74E1, 0x42EB67, 0xDF9DFE,
    0x5FD45E, 0xA4677B, 0x7AACBA, 0xA2F655, 0x23882B, 0x55BA41,
    0x086E59, 0x862A21, 0x834739, 0xE6E389, 0xD49EE5, 0x40FB49,
    0xE956FF, 0xCA0F1C, 0x8A59C5, 0x2BFA94, 0xC5C1D3, 0xCFC50F,
    0xAE5ADB, 0x86C547, 0x624385, 0x3B8621, 0x94792C, 0x876110,
    0x7B4C2A, 0x1A2C80, 0x12BF43, 0x902688, 0x893C78, 0xE4C4A8,
    0x7BDBE5, 0xC23AC4, 0xEAF426, 0x8A67F7, 0xBF920D, 0x2BA365,
    0xB1933D, 0x0B7CBD, 0xDC51A4, 0x63DD27, 0xDDE169, 0x19949A,
    0x9529A8, 0x28CE68, 0xB4ED09, 0x209F44, 0xCA984E, 0x638270,
    0x237C7E, 0x32B90F, 0x8EF5A7, 0xE75614, 0x08F121, 0x2A9DB5,
    0x4D7E6F, 0x5119A5, 0xABF9B5, 0xD6DF82, 0x61DD96, 0x023616,
    0x9F3AC4, 0xA1A283, 0x6DED72, 0x7A8D39, 0xA9B882, 0x5C326B,
    0x5B2746, 0xED3400, 0x7700D2, 0x55F4FC, 0x4D5901, 0x8071E0,
#endif
};

static const KDfloat64KHR PIo2[] = {
    1.57079625129699707031e+00, /* 0x3FF921FB, 0x40000000 */
    7.54978941586159635335e-08, /* 0x3E74442D, 0x00000000 */
    5.39030252995776476554e-15, /* 0x3CF84698, 0x80000000 */
    3.28200341580791294123e-22, /* 0x3B78CC51, 0x60000000 */
    1.27065575308067607349e-29, /* 0x39F01B83, 0x80000000 */
    1.22933308981111328932e-36, /* 0x387A2520, 0x40000000 */
    2.73370053816464559624e-44, /* 0x36E38222, 0x80000000 */
    2.16741683877804819444e-51, /* 0x3569F31D, 0x00000000 */
};

/*
 * A union which permits us to convert between a float and a 32 bit
 * int.
 */
typedef union {
    KDfloat32 value;
    KDuint word; /* FIXME: Assumes 32 bit int.  */
} ieee_float_shape_type;

/* Get a 32 bit int from a float.  */
#define GET_FLOAT_WORD(i, d)        \
    do                              \
    {                               \
        ieee_float_shape_type gf_u; \
        gf_u.value = (d);           \
        (i) = gf_u.word;            \
    } while(0)

/* Set a float from a 32 bit int.  */
#define SET_FLOAT_WORD(d, i)        \
    do                              \
    {                               \
        ieee_float_shape_type sf_u; \
        sf_u.word = (i);            \
        (d) = sf_u.value;           \
    } while(0)

#define STRICT_ASSIGN(type, lval, rval) ((lval) = (rval))

/*
 * A union which permits us to convert between a double and two 32 bit
 * ints.
 */

typedef union {
    KDfloat64KHR value;
    struct {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        /* Big endian */
        KDuint32 msw;
        KDuint32 lsw;
#else
        /* Little endian */
        KDuint32 lsw;
        KDuint32 msw;
#endif
    } parts;
    struct {
        KDuint64 w;
    } xparts;
} ieee_double_shape_type;

/* Get two 32 bit ints from a double.  */
#define EXTRACT_WORDS(ix0, ix1, d)   \
    do                               \
    {                                \
        ieee_double_shape_type ew_u; \
        ew_u.value = (d);            \
        (ix0) = ew_u.parts.msw;      \
        (ix1) = ew_u.parts.lsw;      \
    } while(0)

/* Set a double from two 32 bit ints.  */
#define INSERT_WORDS(d, ix0, ix1)    \
    do                               \
    {                                \
        ieee_double_shape_type iw_u; \
        iw_u.parts.msw = (ix0);      \
        iw_u.parts.lsw = (ix1);      \
        (d) = iw_u.value;            \
    } while(0)

/* Set the more significant 32 bits of a double from an int.  */
#define SET_HIGH_WORD(d, v)          \
    do                               \
    {                                \
        ieee_double_shape_type sh_u; \
        sh_u.value = (d);            \
        sh_u.parts.msw = (v);        \
        (d) = sh_u.value;            \
    } while(0)

/* Get the more significant 32 bit int from a double.  */
#define GET_HIGH_WORD(i, d)          \
    do                               \
    {                                \
        ieee_double_shape_type gh_u; \
        gh_u.value = (d);            \
        (i) = gh_u.parts.msw;        \
    } while(0)

static inline KDfloat32 __kdCosdf(KDfloat64KHR x)
{
    KDfloat64KHR r, w, z;
    /* Try to optimize for parallel evaluation. */
    z = x * x;
    w = z * z;
    r = C2 + z * C3;
    return (KDfloat32)(((1.0f + z * C0) + w * C1) + (w * z) * r);
}

static inline KDfloat32 __kdSindf(KDfloat64KHR x)
{
    KDfloat64KHR r, s, w, z;
    /* Try to optimize for parallel evaluation. */
    z = x * x;
    w = z * z;
    r = S3 + z * S4;
    s = z * x;
    return (KDfloat32)((x + s * (S1 + z * S2)) + s * w * r);
}

/* |tan(x)/x - t(x)| < 2**-25.5 (~[-2e-08, 2e-08]). */
static const KDfloat64KHR T[] = {
    3.3333139503079140e-01, /* 0x15554d3418c99f.0p-54 */
    1.3339200271297674e-01, /* 0x1112fd38999f72.0p-55 */
    5.3381237844567039e-02, /* 0x1b54c91d865afe.0p-57 */
    2.4528318116654728e-02, /* 0x191df3908c33ce.0p-58 */
    2.9743574335996730e-03, /* 0x185dadfcecf44e.0p-61 */
    9.4656478494367317e-03, /* 0x1362b9bf971bcd.0p-59 */
};
static inline KDfloat32 __kdTandf(KDfloat64KHR x, KDint iy)
{
    KDfloat64KHR z, r, w, s, t, u;
    z = x * x;
    /*
     * Split up the polynomial into small independent terms to give
     * opportunities for parallel evaluation.  The chosen splitting is
     * micro-optimized for Athlons (XP, X64).  It costs 2 multiplications
     * relative to Horner's method on sequential machines.
     *
     * We add the small terms from lowest degree up for efficiency on
     * non-sequential machines (the lowest degree terms tend to be ready
     * earlier).  Apart from this, we don't care about order of
     * operations, and don't need to to care since we have precision to
     * spare.  However, the chosen splitting is good for accuracy too,
     * and would give results as accurate as Horner's method if the
     * small terms were added from highest degree down.
     */
    r = T[4] + z * T[5];
    t = T[2] + z * T[3];
    w = z * z;
    s = z * x;
    u = T[0] + z * T[1];
    r = (x + s * u) + (s * w) * (t + w * r);
    if(iy == 1)
    {
        return (KDfloat32)r;
    }
    else
    {
        return (KDfloat32)(-1.0 / r);
    }
}

KDfloat64KHR __kdCopysign(KDfloat64KHR x, KDfloat64KHR y)
{
    KDuint32 hx, hy;
    GET_HIGH_WORD(hx, x);
    GET_HIGH_WORD(hy, y);
    SET_HIGH_WORD(x, (hx & KDINT_MAX) | (hy & 0x80000000));
    return x;
}

KDfloat32 __kdCopysignf(KDfloat32 x, KDfloat32 y)
{
    KDuint32 ix, iy;
    GET_FLOAT_WORD(ix, x);
    GET_FLOAT_WORD(iy, y);
    SET_FLOAT_WORD(x, (ix & KDINT_MAX) | (iy & 0x80000000));
    return x;
}

static KDfloat64KHR __kdScalbn(KDfloat64KHR x, KDint n)
{
    KDint32 k, hx, lx;
    EXTRACT_WORDS(hx, lx, x);
    k = (hx & 0x7ff00000) >> 20; /* extract exponent */
    if(k == 0)
    { /* 0 or subnormal x */
        if((lx | (hx & KDINT_MAX)) == 0)
        {
            return x;
        } /* +-0 */
        x *= two54;
        GET_HIGH_WORD(hx, x);
        k = ((hx & 0x7ff00000) >> 20) - 54;
        if(n < -50000)
        {
            return tiny * x; /*underflow*/
        }
    }
    if(k == 0x7ff)
    {
        return x + x;
    } /* NaN or Inf */
    k = k + n;
    if(k > 0x7fe)
    {
        return huge * __kdCopysign(huge, x); /* overflow  */
    }
    if(k > 0) /* normal result */
    {
        SET_HIGH_WORD(x, (hx & 0x800fffff) | (k << 20));
        return x;
    }
    if(k <= -54)
    {
        if(n > 50000) /* in case integer overflow in n+k */
        {
            return huge * __kdCopysign(huge, x); /*overflow*/
        }
        else
        {
            return tiny * __kdCopysign(tiny, x); /*underflow*/
        }
    }
    k += 54; /* subnormal result */
    SET_HIGH_WORD(x, (hx & 0x800fffff) | (k << 20));
    return x * twom54;
}

static KDfloat32 __kdScalbnf(KDfloat32 x, KDint n)
{
    KDint32 k, ix;
    GET_FLOAT_WORD(ix, x);
    k = (ix & KD_INFINITY) >> 23; /* extract exponent */
    if(k == 0)
    { /* 0 or subnormal x */
        if((ix & KDINT_MAX) == 0)
        {
            return x;
        } /* +-0 */
        x *= two25;
        GET_FLOAT_WORD(ix, x);
        k = ((ix & KD_INFINITY) >> 23) - 25;
        if(n < -50000)
        {
            return tiny * x; /*underflow*/
        }
    }
    if(k == 0xff)
    {
        return x + x;
    } /* NaN or Inf */
    k = k + n;
    if(k > 0xfe)
    {
        return huge * __kdCopysignf(huge, x); /* overflow  */
    }
    if(k > 0) /* normal result */
    {
        SET_FLOAT_WORD(x, (ix & 0x807fffff) | (k << 23));
        return x;
    }
    if(k <= -25)
    {
        if(n > 50000) /* in case integer overflow in n+k */
        {
            return huge * __kdCopysignf(huge, x); /*overflow*/
        }
        else
        {
            return tiny * __kdCopysignf(tiny, x); /*underflow*/
        }
    }
    k += 25; /* subnormal result */
    SET_FLOAT_WORD(x, (ix & 0x807fffff) | (k << 23));
    return x * twom25;
}

static KDint __kdIrint(KDfloat64KHR x)
{
#ifdef __SSE2__
    return _mm_cvtsd_si32(_mm_load_sd(&x));
#else
    if(x >= 0)
    {
        return (KDint)(x + 0.5);
    }
    return (KDint)(x - 0.5);
#endif
}

static const KDint init_jk[] = {3, 4, 4, 6}; /* initial value for jk */
static KDint __kdRemPio2(KDfloat64KHR *x, KDfloat64KHR *y, KDint e0, KDint nx, KDint prec)
{
    KDint32 jz, jx, jv, jp, jk, carry, n, iq[20], i, j, k, m, q0, ih;
    KDfloat64KHR z, fw, f[20], fq[20], q[20];
    /* initialize jk*/
    jk = init_jk[prec];
    jp = jk;
    /* determine jx,jv,q0, note that 3>q0 */
    jx = nx - 1;
    jv = (e0 - 3) / 24;
    if(jv < 0)
    {
        jv = 0;
    }
    q0 = e0 - 24 * (jv + 1);
    /* set up f[0] to f[jx+jk] where f[jx+jk] = ipio2[jv+jk] */
    j = jv - jx;
    m = jx + jk;
    for(i = 0; i <= m; i++, j++)
    {
        f[i] = (j < 0) ? 0.0f : (KDfloat64KHR)ipio2[j];
    }
    /* compute q[0],q[1],...q[jk] */
    for(i = 0; i <= jk; i++)
    {
        for(j = 0, fw = 0.0; j <= jx; j++)
        {
            fw += x[j] * f[jx + i - j];
        }
        q[i] = fw;
    }
    jz = jk;
    KDboolean recompute = 0;
    do
    {
        recompute = 0;
        /* distill q[] into iq[] reversingly */
        for(i = 0, j = jz, z = q[jz]; j > 0; i++, j--)
        {
            fw = (KDfloat64KHR)((KDint32)(twon24 * z));
            iq[i] = (KDint32)(z - two24 * fw);
            z = q[j - 1] + fw;
        }
        /* compute n */
        z = __kdScalbn(z, q0);            /* actual value of z */
        z -= 8.0 * kdFloorKHR(z * 0.125); /* trim off integer >= 8 */
        n = (KDint32)z;
        z -= (KDfloat64KHR)n;
        ih = 0;
        if(q0 > 0)
        { /* need iq[jz-1] to determine n */
            i = (iq[jz - 1] >> (24 - q0));
            n += i;
            iq[jz - 1] -= i << (24 - q0);
            ih = iq[jz - 1] >> (23 - q0);
        }
        else if(q0 == 0)
        {
            ih = iq[jz - 1] >> 23;
        }
        else if(z >= 0.5)
        {
            ih = 2;
        }
        if(ih > 0)
        { /* q > 0.5 */
            n += 1;
            carry = 0;
            for(i = 0; i < jz; i++)
            { /* compute 1-q */
                j = iq[i];
                if(carry == 0)
                {
                    if(j != 0)
                    {
                        carry = 1;
                        iq[i] = 0x1000000 - j;
                    }
                }
                else
                {
                    iq[i] = 0xffffff - j;
                }
            }
            if(q0 > 0)
            { /* rare case: chance is 1 in 12 */
                switch(q0)
                {
                    case 1:
                    {
                        iq[jz - 1] &= 0x7fffff;
                        break;
                    }
                    case 2:
                    {
                        iq[jz - 1] &= 0x3fffff;
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
            if(ih == 2)
            {
                z = 1.0f - z;
                if(carry != 0)
                {
                    z -= __kdScalbn(1.0f, q0);
                }
            }
        }
        /* check if recomputation is needed */
        if(z == 0.0f)
        {
            j = 0;
            for(i = jz - 1; i >= jk; i--)
            {
                j |= iq[i];
            }
            if(j == 0)
            { /* need recomputation */
                for(k = 1; iq[jk - k] == 0; k++)
                {
                } /* k = no. of terms needed */
                for(i = jz + 1; i <= jz + k; i++)
                { /* add q[jz+1] to q[jz+k] */
                    f[jx + i] = (KDfloat64KHR)ipio2[jv + i];
                    for(j = 0, fw = 0.0; j <= jx; j++)
                    {
                        fw += x[j] * f[jx + i - j];
                    }
                    q[i] = fw;
                }
                jz += k;
                recompute = 1;
            }
        }
    } while(recompute);
    /* chop off zero terms */
    if(z == 0.0)
    {
        jz -= 1;
        q0 -= 24;
        while(iq[jz] == 0)
        {
            jz--;
            q0 -= 24;
        }
    }
    else
    { /* break z into 24-bit if necessary */
        z = __kdScalbn(z, -q0);
        if(z >= two24)
        {
            fw = (KDfloat64KHR)((KDint32)(twon24 * z));
            iq[jz] = (KDint32)(z - two24 * fw);
            jz += 1;
            q0 += 24;
            iq[jz] = (KDint32)fw;
        }
        else
        {
            iq[jz] = (KDint32)z;
        }
    }
    /* convert integer "bit" chunk to floating-point value */
    fw = __kdScalbn(1.0f, q0);
    for(i = jz; i >= 0; i--)
    {
        q[i] = fw * (KDfloat64KHR)iq[i];
        fw *= twon24;
    }
    /* compute PIo2[0,...,jp]*q[jz,...,0] */
    for(i = jz; i >= 0; i--)
    {
        for(fw = 0.0, k = 0; k <= jp && k <= jz - i; k++)
        {
            fw += PIo2[k] * q[i + k];
        }
        fq[jz - i] = fw;
    }
    /* compress fq[] into y[] */
    switch(prec)
    {
        case 0:
        {
            fw = 0.0;
            for(i = jz; i >= 0; i--)
            {
                fw += fq[i];
            }
            y[0] = (ih == 0) ? fw : -fw;
            break;
        }
        case 1:
        case 2:
        {
            fw = 0.0;
            for(i = jz; i >= 0; i--)
            {
                fw += fq[i];
            }
            STRICT_ASSIGN(KDfloat64KHR, fw, fw);
            y[0] = (ih == 0) ? fw : -fw;
            fw = fq[0] - fw;
            for(i = 1; i <= jz; i++)
            {
                fw += fq[i];
            }
            y[1] = (ih == 0) ? fw : -fw;
            break;
        }
        case 3: /* painful */
        {
            for(i = jz; i > 0; i--)
            {
                fw = fq[i - 1] + fq[i];
                fq[i] += fq[i - 1] - fw;
                fq[i - 1] = fw;
            }
            for(i = jz; i > 1; i--)
            {
                fw = fq[i - 1] + fq[i];
                fq[i] += fq[i - 1] - fw;
                fq[i - 1] = fw;
            }
            for(fw = 0.0, i = jz; i >= 2; i--)
            {
                fw += fq[i];
            }
            if(ih == 0)
            {
                y[0] = fq[0];
                y[1] = fq[1];
                y[2] = fw;
            }
            else
            {
                y[0] = -fq[0];
                y[1] = -fq[1];
                y[2] = -fw;
            }
        }
        default:
        {
            break;
        }
    }
    return n & 7;
}

static inline KDint __kdRemPio2f(KDfloat32 x, KDfloat64KHR *y)
{
    KDfloat64KHR w, r, fn;
    KDfloat64KHR tx[1], ty[1];
    KDfloat32 z;
    KDint32 e0, n, ix, hx;
    GET_FLOAT_WORD(hx, x);
    ix = hx & KDINT_MAX;
    /* 33+53 bit pi is good enough for medium size */
    if(ix < 0x4dc90fdb)
    { /* |x| ~< 2^28*(pi/2), medium size */
        /* Use a specialized rint() to get fn.  Assume round-to-nearest. */
        STRICT_ASSIGN(KDfloat64KHR, fn, x * KD_2_PI_KHR + 6.7553994410557440e+15);
        fn = fn - 6.7553994410557440e+15;
        n = __kdIrint(fn);
        r = x - fn * pio2_1;
        w = fn * pio2_1t;
        *y = r - w;
        return n;
    }
    /*
     * all other (large) arguments
     */
    if(ix >= KD_INFINITY)
    { /* x is inf or NaN */
        *y = x - x;
        return 0;
    }
    /* set z = scalbn(|x|,ilogb(|x|)-23) */
    e0 = (ix >> 23) - 150; /* e0 = ilogb(|x|)-23; */
    SET_FLOAT_WORD(z, ix - ((KDint32)(e0 << 23)));
    tx[0] = z;
    n = __kdRemPio2(tx, ty, e0, 1, 0);
    if(hx < 0)
    {
        *y = -ty[0];
        return -n;
    }
    *y = ty[0];
    return n;
}

/* kdAcosf: Arc cosine function. */
KD_API KDfloat32 KD_APIENTRY kdAcosf(KDfloat32 x)
{
    KDfloat32 z, p, q, r, w, s, c, df;
    KDint32 hx, ix;
    GET_FLOAT_WORD(hx, x);
    ix = hx & KDINT_MAX;
    if(ix >= 0x3f800000)
    { /* |x| >= 1 */
        if(ix == 0x3f800000)
        { /* |x| == 1 */
            if(hx > 0)
            {
                return 0.0; /* acos(1) = 0 */
            }
            else
            {
                return KD_PI_F + 2.0f * pio2_lo;
            } /* acos(-1)= pi */
        }
        return (x - x) / (x - x); /* acos(|x|>1) is NaN */
    }
    if(ix < 0x3f000000)
    { /* |x| < 0.5 */
        if(ix <= 0x32800000)
        {
            return pio2_hi + pio2_lo;
        } /*if|x|<2**-26*/
        z = x * x;
        p = z * (pS0 + z * (pS1 + z * pS2));
        q = 1.0f + z * qS1;
        r = p / q;
        return pio2_hi - (x - (pio2_lo - x * r));
    }
    else if(hx < 0)
    { /* x < -0.5 */
        z = (1.0f + x) * 0.5f;
        p = z * (pS0 + z * (pS1 + z * pS2));
        q = 1.0f + z * qS1;
        s = kdSqrtf(z);
        r = p / q;
        w = r * s - pio2_lo;
        return KD_PI_F - 2.0f * (s + w);
    }
    else
    { /* x > 0.5 */
        KDint32 idf;
        z = (1.0f - x) * 0.5f;
        s = kdSqrtf(z);
        df = s;
        GET_FLOAT_WORD(idf, df);
        SET_FLOAT_WORD(df, idf & 0xfffff000);
        c = (z - df * df) / (s + df);
        p = z * (pS0 + z * (pS1 + z * pS2));
        q = 1.0f + z * qS1;
        r = p / q;
        w = r * s + c;
        return 2.0f * (df + w);
    }
}

/* kdAsinf: Arc sine function. */
KD_API KDfloat32 KD_APIENTRY kdAsinf(KDfloat32 x)
{
    KDfloat32 t, w, p, q, s;
    KDint32 hx, ix;
    GET_FLOAT_WORD(hx, x);
    ix = hx & KDINT_MAX;
    if(ix >= 0x3f800000)
    { /* |x| >= 1 */
        if(ix == 0x3f800000)
        { /* |x| == 1 */
            return x * KD_PI_2_F;
        }                         /* asin(+-1) = +-pi/2 with inexact */
        return (x - x) / (x - x); /* asin(|x|>1) is NaN */
    }
    else if(ix < 0x3f000000)
    { /* |x|<0.5 */
        if(ix < 0x39800000)
        { /* |x| < 2**-12 */
            if(huge + x > 1.0f)
            {
                return x;
            } /* return x with inexact if x!=0*/
        }
        t = x * x;
        p = t * (pS0 + t * (pS1 + t * pS2));
        q = 1.0f + t * qS1;
        w = p / q;
        return x + x * w;
    }
    /* 1> |x|>= 0.5 */
    w = 1.0f - kdFabsf(x);
    t = w * 0.5f;
    p = t * (pS0 + t * (pS1 + t * pS2));
    q = 1.0f + t * qS1;
    s = kdSqrtf(t);
    w = p / q;
    t = KD_PI_2_F - 2.0f * (s + s * w);
    if(hx > 0)
    {
        return t;
    }
    else
    {
        return -t;
    }
}

/* kdAtanf: Arc tangent function. */
static const KDfloat32 aT[] = {
    3.3333328366e-01f,
    -1.9999158382e-01f,
    1.4253635705e-01f,
    -1.0648017377e-01f,
    6.1687607318e-02f,
};
KD_API KDfloat32 KD_APIENTRY kdAtanf(KDfloat32 x)
{
    KDfloat32 w, s1, s2, z;
    KDint32 ix, hx, id;
    GET_FLOAT_WORD(hx, x);
    ix = hx & KDINT_MAX;
    if(ix >= 0x4c800000)
    { /* if |x| >= 2**26 */
        if(ix > KD_INFINITY)
        {
            return x + x;
        } /* NaN */
        if(hx > 0)
        {
            return atanhi[3] + *(volatile KDfloat32 *)&atanlo[3];
        }
        else
        {
            return -atanhi[3] - *(volatile KDfloat32 *)&atanlo[3];
        }
    }
    if(ix < 0x3ee00000)
    { /* |x| < 0.4375 */
        if(ix < 0x39800000)
        { /* |x| < 2**-12 */
            if(huge + x > 1.0f)
            {
                return x;
            } /* raise inexact */
        }
        id = -1;
    }
    else
    {
        x = kdFabsf(x);
        if(ix < 0x3f980000)
        { /* |x| < 1.1875 */
            if(ix < 0x3f300000)
            { /* 7/16 <=|x|<11/16 */
                id = 0;
                x = (2.0f * x - 1.0f) / (2.0f + x);
            }
            else
            { /* 11/16<=|x|< 19/16 */
                id = 1;
                x = (x - 1.0f) / (x + 1.0f);
            }
        }
        else
        {
            if(ix < 0x401c0000)
            { /* |x| < 2.4375 */
                id = 2;
                x = (x - 1.5f) / (1.0f + 1.5f * x);
            }
            else
            { /* 2.4375 <= |x| < 2**26 */
                id = 3;
                x = -1.0f / x;
            }
        }
    }
    /* end of argument reduction */
    z = x * x;
    w = z * z;
    /* break sum from i=0 to 10 aT[i]z**(i+1) into odd and even poly */
    s1 = z * (aT[0] + w * (aT[2] + w * aT[4]));
    s2 = w * (aT[1] + w * aT[3]);
    if(id < 0)
    {
        return x - x * (s1 + s2);
    }
    else
    {
        z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
        return (hx < 0) ? -z : z;
    }
}

/* kdAtan2f: Arc tangent function. */
KD_API KDfloat32 KD_APIENTRY kdAtan2f(KDfloat32 y, KDfloat32 x)
{
    KDfloat32 z;
    KDint32 k, m, hx, hy, ix, iy;
    GET_FLOAT_WORD(hx, x);
    ix = hx & KDINT_MAX;
    GET_FLOAT_WORD(hy, y);
    iy = hy & KDINT_MAX;
    if((ix > KD_INFINITY) || (iy > KD_INFINITY))
    { /* x or y is NaN */
        return x + y;
    }
    if(hx == 0x3f800000)
    {
        return kdAtanf(y);
    }                                        /* x=1.0 */
    m = ((hy >> 31) & 1) | ((hx >> 30) & 2); /* 2*sign(x)+sign(y) */

    /* when y = 0 */
    if(iy == 0)
    {
        switch(m)
        {
            case 0:
            case 1:
            {
                return y; /* atan(+-0,+anything)=+-0 */
            }
            case 2:
            {
                return KD_PI_F + tiny; /* atan(+0,-anything) = pi */
            }
            case 3:
            {
                return -KD_PI_F - tiny; /* atan(-0,-anything) =-pi */
            }
            default:
            {
                break;
            }
        }
    }
    /* when x = 0 */
    if(ix == 0)
    {
        return (hy < 0) ? -KD_PI_2_F - tiny : KD_PI_2_F + tiny;
    }

    /* when x is INF */
    if(ix == KD_INFINITY)
    {
        if(iy == KD_INFINITY)
        {
            switch(m)
            {
                case 0:
                {
                    return KD_PI_4_F + tiny; /* atan(+INF,+INF) */
                }
                case 1:
                {
                    return -KD_PI_4_F - tiny; /* atan(-INF,+INF) */
                }
                case 2:
                {
                    return 3.0f * KD_PI_4_F + tiny; /*atan(+INF,-INF)*/
                }
                case 3:
                {
                    return -3.0f * KD_PI_4_F - tiny; /*atan(-INF,-INF)*/
                }
                default:
                {
                    break;
                }
            }
        }
        else
        {
            switch(m)
            {
                case 0:
                {
                    return 0.0f; /* atan(+...,+INF) */
                }
                case 1:
                {
                    return -0.0f; /* atan(-...,+INF) */
                }
                case 2:
                {
                    return KD_PI_F + tiny; /* atan(+...,-INF) */
                }
                case 3:
                {
                    return -KD_PI_F - tiny; /* atan(-...,-INF) */
                }
                default:
                {
                    break;
                }
            }
        }
    }
    /* when y is INF */
    if(iy == KD_INFINITY)
    {
        return (hy < 0) ? -KD_PI_2_F - tiny : KD_PI_2_F + tiny;
    }

    /* compute y/x */
    k = (iy - ix) >> 23;
    if(k > 26)
    { /* |y/x| >  2**26 */
        z = KD_PI_2_F + 0.5f * pi_lo;
        m &= 1;
    }
    else if(k < -26 && hx < 0)
    {
        z = 0.0f; /* 0 > |y|/x > -2**-26 */
    }
    else
    {
        z = kdAtanf(kdFabsf(y / x)); /* safe to do y/x */
    }
    switch(m)
    {
        case 0:
        {
            return z; /* atan(+,+) */
        }
        case 1:
        {
            return -z; /* atan(-,+) */
        }
        case 2:
        {
            return KD_PI_F - (z - pi_lo); /* atan(+,-) */
        }
        default: /* case 3 */
        {
            return (z - pi_lo) - KD_PI_F; /* atan(-,-) */
        }
    }
}

/* kdCosf: Cosine function. */
KD_API KDfloat32 KD_APIENTRY kdCosf(KDfloat32 x)
{
    KDfloat64KHR y;
    KDint32 n, hx, ix;
    GET_FLOAT_WORD(hx, x);
    ix = hx & KDINT_MAX;
    if(ix <= 0x3f490fda)
    { /* |x| ~<= pi/4 */
        if(ix < 0x39800000)
        { /* |x| < 2**-12 */
            if(((int)x) == 0)
            {
                return 1.0f;
            }
        } /* 1 with inexact if x != 0 */
        return __kdCosdf(x);
    }
    if(ix <= 0x407b53d1)
    { /* |x| ~<= 5*pi/4 */
        if(ix > 0x4016cbe3)
        { /* |x|  ~> 3*pi/4 */
            return -__kdCosdf(x + (hx > 0 ? -(2 * KD_PI_2_KHR) : (2 * KD_PI_2_KHR)));
        }
        else
        {
            if(hx > 0)
            {
                return __kdSindf(KD_PI_2_KHR - x);
            }
            else
            {
                return __kdSindf(x + KD_PI_2_KHR);
            }
        }
    }
    if(ix <= 0x40e231d5)
    { /* |x| ~<= 9*pi/4 */
        if(ix > 0x40afeddf)
        { /* |x|  ~> 7*pi/4 */
            return __kdCosdf(x + (hx > 0 ? -(4 * KD_PI_2_KHR) : (4 * KD_PI_2_KHR)));
        }
        else
        {
            if(hx > 0)
            {
                return __kdSindf(x - (3 * KD_PI_2_KHR));
            }
            else
            {
                return __kdSindf(-(3 * KD_PI_2_KHR) - x);
            }
        }
    }
    /* cos(Inf or NaN) is NaN */
    else if(ix >= KD_INFINITY)
    {
        return x - x;
        /* general argument reduction needed */
    }
    else
    {
        n = __kdRemPio2f(x, &y);
        switch(n & 3)
        {
            case 0:
            {
                return __kdCosdf(y);
            }
            case 1:
            {
                return __kdSindf(-y);
            }
            case 2:
            {
                return -__kdCosdf(y);
            }
            default:
            {
                return __kdSindf(y);
            }
        }
    }
}

/* kdSinf: Sine function. */
KD_API KDfloat32 KD_APIENTRY kdSinf(KDfloat32 x)
{
    KDfloat64KHR y;
    KDint32 n, hx, ix;
    GET_FLOAT_WORD(hx, x);
    ix = hx & KDINT_MAX;
    if(ix <= 0x3f490fda)
    { /* |x| ~<= pi/4 */
        if(ix < 0x39800000)
        { /* |x| < 2**-12 */
            if(((KDint)x) == 0)
            {
                return x;
            }
        } /* x with inexact if x != 0 */
        return __kdSindf(x);
    }
    if(ix <= 0x407b53d1)
    { /* |x| ~<= 5*pi/4 */
        if(ix <= 0x4016cbe3)
        { /* |x| ~<= 3pi/4 */
            if(hx > 0)
            {
                return __kdCosdf(x - KD_PI_2_KHR);
            }
            else
            {
                return -__kdCosdf(x + KD_PI_2_KHR);
            }
        }
        else
        {
            return __kdSindf((hx > 0 ? (2 * KD_PI_2_KHR) : -(2 * KD_PI_2_KHR)) - x);
        }
    }
    if(ix <= 0x40e231d5)
    { /* |x| ~<= 9*pi/4 */
        if(ix <= 0x40afeddf)
        { /* |x| ~<= 7*pi/4 */
            if(hx > 0)
            {
                return -__kdCosdf(x - (3 * KD_PI_2_KHR));
            }
            else
            {
                return __kdCosdf(x + (3 * KD_PI_2_KHR));
            }
        }
        else
        {
            return __kdSindf(x + (hx > 0 ? -(4 * KD_PI_2_KHR) : (4 * KD_PI_2_KHR)));
        }
    }
    /* sin(Inf or NaN) is NaN */
    else if(ix >= KD_INFINITY)
    {
        return x - x;
        /* general argument reduction needed */
    }
    else
    {
        n = __kdRemPio2f(x, &y);
        switch(n & 3)
        {
            case 0:
            {
                return __kdSindf(y);
            }
            case 1:
            {
                return __kdCosdf(y);
            }
            case 2:
            {
                return __kdSindf(-y);
            }
            default:
            {
                return -__kdCosdf(y);
            }
        }
    }
}

/* kdTanf: Tangent function. */
KD_API KDfloat32 KD_APIENTRY kdTanf(KDfloat32 x)
{
    KDfloat64KHR y;
    KDint32 n, hx, ix;
    GET_FLOAT_WORD(hx, x);
    ix = hx & KDINT_MAX;
    if(ix <= 0x3f490fda)
    { /* |x| ~<= pi/4 */
        if(ix < 0x39800000)
        { /* |x| < 2**-12 */
            if(((int)x) == 0)
            {
                return x;
            }
        } /* x with inexact if x != 0 */
        return __kdTandf(x, 1);
    }
    if(ix <= 0x407b53d1)
    { /* |x| ~<= 5*pi/4 */
        if(ix <= 0x4016cbe3)
        { /* |x| ~<= 3pi/4 */
            return __kdTandf(x + (hx > 0 ? -KD_PI_2_KHR : KD_PI_2_KHR), -1);
        }
        else
        {
            return __kdTandf(x + (hx > 0 ? -(2 * KD_PI_2_KHR) : (2 * KD_PI_2_KHR)), 1);
        }
    }
    if(ix <= 0x40e231d5)
    { /* |x| ~<= 9*pi/4 */
        if(ix <= 0x40afeddf)
        { /* |x| ~<= 7*pi/4 */
            return __kdTandf(x + (hx > 0 ? -(3 * KD_PI_2_KHR) : (3 * KD_PI_2_KHR)), -1);
        }
        else
        {
            return __kdTandf(x + (hx > 0 ? -(4 * KD_PI_2_KHR) : (4 * KD_PI_2_KHR)), 1);
        }
    }
    /* tan(Inf or NaN) is NaN */
    else if(ix >= KD_INFINITY)
    {
        return x - x;
        /* general argument reduction needed */
    }
    else
    {
        n = __kdRemPio2f(x, &y);
        /* integer parameter: 1 -- n even; -1 -- n odd */
        return __kdTandf(y, 1 - ((n & 1) << 1));
    }
}

/* kdExpf: Exponential function. */
KD_API KDfloat32 KD_APIENTRY kdExpf(KDfloat32 x)
{
    KDfloat32 y, hi = 0.0f, lo = 0.0f, c, t, twopk;
    KDint32 k = 0, xsb;
    KDuint32 hx;
    GET_FLOAT_WORD(hx, x);
    xsb = (hx >> 31) & 1; /* sign bit of x */
    hx &= KDINT_MAX;      /* high word of |x| */
    /* filter out non-finite argument */
    if(hx >= 0x42b17218)
    { /* if |x|>=88.721... */
        if(hx > KD_INFINITY)
        {
            return x + x;
        } /* NaN */
        if(hx == KD_INFINITY)
        {
            return (xsb == 0) ? x : 0.0f; /* exp(+-inf)={inf,0} */
        }
        if(x > o_threshold)
        {
            return huge * huge; /* overflow */
        }
        if(x < u_threshold)
        {
            return twom100 * twom100; /* underflow */
        }
    }
    /* argument reduction */
    if(hx > 0x3eb17218)
    { /* if  |x| > 0.5 ln2 */
        if(hx < 0x3F851592)
        { /* and |x| < 1.5 ln2 */
            hi = x - ln2HI[xsb];
            lo = ln2LO[xsb];
            k = 1 - xsb - xsb;
        }
        else
        {
            k = (KDint32)(invln2 * x + halF[xsb]);
            t = (KDfloat32)k;
            hi = x - t * ln2HI[0]; /* t*ln2HI is exact here */
            lo = t * ln2LO[0];
        }
        STRICT_ASSIGN(KDfloat32, x, hi - lo);
    }
    else if(hx < 0x39000000)
    { /* when |x|<2**-14 */
        if(huge + x > 1.0f)
        {
            return 1.0f + x;
        } /* trigger inexact */
    }
    else
    {
        k = 0;
    }
    /* x is now in primary range */
    t = x * x;
    if(k >= -125)
    {
        SET_FLOAT_WORD(twopk, 0x3f800000 + (k << 23));
    }
    else
    {
        SET_FLOAT_WORD(twopk, 0x3f800000 + ((k + 100) << 23));
    }
    c = x - t * (P1 + t * P2);
    if(k == 0)
    {
        return 1.0f - ((x * c) / (c - 2.0f) - x);
    }
    else
    {
        y = 1.0f - ((lo - (x * c) / (2.0f - c)) - hi);
    }
    if(k >= -125)
    {
        if(k == 128)
        {
            return y * 2.0f * 1.7014118346046923e+38f;
        }
        return y * twopk;
    }
    else
    {
        return y * twopk * twom100;
    }
}

/* kdLogf: Natural logarithm function. */
static volatile float vzero = 0.0;
KD_API KDfloat32 KD_APIENTRY kdLogf(KDfloat32 x)
{
    KDfloat32 hfsq, f, s, z, R, w, t1, t2, dk;
    KDint32 k, ix, i, j;
    GET_FLOAT_WORD(ix, x);
    k = 0;
    if(ix < 0x00800000)
    { /* x < 2**-126  */
        if((ix & KD_INFINITY) == 0)
        {
            return -two25 / vzero;
        } /* log(+-0)=-inf */
        if(ix < 0)
        {
            return (x - x) / (x - x); /* log(-#) = NaN */
        }
        k -= 25;
        x *= two25; /* subnormal number, scale up x */
        GET_FLOAT_WORD(ix, x);
    }
    if(ix >= KD_INFINITY)
    {
        return x + x;
    }
    k += (ix >> 23) - 127;
    ix &= 0x007fffff;
    i = (ix + (0x95f64 << 3)) & 0x800000;
    SET_FLOAT_WORD(x, ix | (i ^ 0x3f800000)); /* normalize x or x/2 */
    k += (i >> 23);
    f = x - 1.0f;
    if((0x007fffff & (0x8000 + ix)) < 0xc000)
    { /* -2**-9 <= f < 2**-9 */
        if(f == 0.0f)
        {
            if(k == 0)
            {
                return 0.0f;
            }
            else
            {
                dk = (KDfloat32)k;
                return dk * ln2_hi + dk * ln2_lo;
            }
        }
        R = f * f * (0.5f - 0.33333333333333333f * f);
        if(k == 0)
        {
            return f - R;
        }
        else
        {
            dk = (KDfloat32)k;
            return dk * ln2_hi - ((R - dk * ln2_lo) - f);
        }
    }
    s = f / (2.0f + f);
    dk = (KDfloat32)k;
    z = s * s;
    i = ix - (0x6147a << 3);
    w = z * z;
    j = (0x6b851 << 3) - ix;
    t1 = w * (Lg2 + w * Lg4);
    t2 = z * (Lg1 + w * Lg3);
    i |= j;
    R = t2 + t1;
    if(i > 0)
    {
        hfsq = 0.5f * f * f;
        if(k == 0)
        {
            return f - (hfsq - s * (hfsq + R));
        }
        else
        {
            return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
        }
    }
    else
    {
        if(k == 0)
        {
            return f - s * (f - R);
        }
        else
        {
            return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
        }
    }
}

/* kdFabsf: Absolute value. */
KD_API KDfloat32 KD_APIENTRY kdFabsf(KDfloat32 x)
{
    KDuint32 ix;
    GET_FLOAT_WORD(ix, x);
    SET_FLOAT_WORD(x, ix & KDINT_MAX);
    return x;
}

/* kdPowf: Power function. */
KD_API KDfloat32 KD_APIENTRY kdPowf(KDfloat32 x, KDfloat32 y)
{
    KDfloat32 z, ax, z_h, z_l, p_h, p_l;
    KDfloat32 y1, t1, t2, r, s, sn, t, u, v, w;
    KDint32 i, j, k, yisint, n;
    KDint32 hx, hy, ix, iy, is;
    GET_FLOAT_WORD(hx, x);
    GET_FLOAT_WORD(hy, y);
    ix = hx & KDINT_MAX;
    iy = hy & KDINT_MAX;
    /* y==zero: x**0 = 1 */
    if(iy == 0)
    {
        return 1.0f;
    }
    /* x==1: 1**y = 1, even if y is NaN */
    if(hx == 0x3f800000)
    {
        return 1.0f;
    }
    /* y!=zero: result is NaN if either arg is NaN */
    if(ix > KD_INFINITY || iy > KD_INFINITY)
    {
        return (x + 0.0f) + (y + 0.0f);
    }
    /* determine if y is an odd int when x < 0
     * yisint = 0   ... y is not an integer
     * yisint = 1   ... y is an odd int
     * yisint = 2   ... y is an even int
     */
    yisint = 0;
    if(hx < 0)
    {
        if(iy >= 0x4b800000)
        {
            yisint = 2; /* even integer y */
        }
        else if(iy >= 0x3f800000)
        {
            k = (iy >> 23) - 0x7f; /* exponent */
            j = iy >> (23 - k);
            if((j << (23 - k)) == iy)
            {
                yisint = 2 - (j & 1);
            }
        }
    }
    /* special value of y */
    if(iy == KD_INFINITY)
    { /* y is +-inf */
        if(ix == 0x3f800000)
        {
            return 1.0f; /* (-1)**+-inf is NaN */
        }
        else if(ix > 0x3f800000)
        { /* (|x|>1)**+-inf = inf,0 */
            return (hy >= 0) ? y : 0.0f;
        }
        else
        { /* (|x|<1)**-,+inf = inf,0 */
            return (hy < 0) ? -y : 0.0f;
        }
    }
    if(iy == 0x3f800000)
    { /* y is  +-1 */
        if(hy < 0)
        {
            return 1.0f / x;
        }
        else
        {
            return x;
        }
    }
    if(hy == 0x40000000)
    {
        return x * x;
    } /* y is  2 */
    if(hy == 0x3f000000)
    {               /* y is  0.5 */
        if(hx >= 0) /* x >= +0 */
        {
            return kdSqrtf(x);
        }
    }
    ax = kdFabsf(x);
    /* special value of x */
    if(ix == KD_INFINITY || ix == 0 || ix == 0x3f800000)
    {
        z = ax; /*x is +-0,+-inf,+-1*/
        if(hy < 0)
        {
            z = 1.0f / z; /* z = (1/|x|) */
        }
        if(hx < 0)
        {
            if(((ix - 0x3f800000) | yisint) == 0)
            {
                z = (z - z) / (z - z); /* (-1)**non-int is NaN */
            }
            else if(yisint == 1)
            {
                z = -z; /* (x<0)**odd = -(|x|**odd) */
            }
        }
        return z;
    }
    n = ((KDuint32)hx >> 31) - 1;
    /* (x<0)**(non-int) is NaN */
    if((n | yisint) == 0)
    {
        return (x - x) / (x - x);
    }
    sn = 1.0f; /* s (sign of result -ve**odd) = -1 else = 1 */
    if((n | (yisint - 1)) == 0)
    {
        sn = -1.0f; /* (-ve)**(odd int) */
    }
    /* |y| is huge */
    if(iy > 0x4d000000)
    { /* if |y| > 2**27 */
        /* over/underflow if x is not close to one */
        if(ix < 0x3f7ffff8)
        {
            return (hy < 0) ? sn * huge * huge : sn * tiny * tiny;
        }
        if(ix > 0x3f800007)
        {
            return (hy > 0) ? sn * huge * huge : sn * tiny * tiny;
        }
        /* now |1-x| is tiny <= 2**-20, suffice to compute
       log(x) by x-x^2/2+x^3/3-x^4/4 */
        t = ax - 1; /* t has 20 trailing zeros */
        w = (t * t) * (0.5f - t * (0.333333333333f - t * 0.25f));
        u = ivln2_h * t; /* ivln2_h has 16 sig. bits */
        v = t * ivln2_l - w * ivln2;
        t1 = u + v;
        GET_FLOAT_WORD(is, t1);
        SET_FLOAT_WORD(t1, is & 0xfffff000);
        t2 = v - (t1 - u);
    }
    else
    {
        KDfloat32 s2, s_h, s_l, t_h, t_l;
        n = 0;
        /* take care subnormal number */
        if(ix < 0x00800000)
        {
            ax *= two24f;
            n -= 24;
            GET_FLOAT_WORD(ix, ax);
        }
        n += ((ix) >> 23) - 0x7f;
        j = ix & 0x007fffff;
        /* determine interval */
        ix = j | 0x3f800000; /* normalize ix */
        if(j <= 0x1cc471)
        {
            k = 0; /* |x|<sqrt(3/2) */
        }
        else if(j < 0x5db3d7)
        {
            k = 1; /* |x|<sqrt(3)   */
        }
        else
        {
            k = 0;
            n += 1;
            ix -= 0x00800000;
        }
        SET_FLOAT_WORD(ax, ix);
        /* compute s = s_h+s_l = (x-1)/(x+1) or (x-1.5)/(x+1.5) */
        u = ax - bp[k]; /* bp[0]=1.0, bp[1]=1.5 */
        v = 1.0f / (ax + bp[k]);
        s = u * v;
        s_h = s;
        GET_FLOAT_WORD(is, s_h);
        SET_FLOAT_WORD(s_h, is & 0xfffff000);
        /* t_h=ax+bp[k] High */
        is = ((ix >> 1) & 0xfffff000) | 0x20000000;
        SET_FLOAT_WORD(t_h, is + 0x00400000 + (k << 21));
        t_l = ax - (t_h - bp[k]);
        s_l = v * ((u - s_h * t_h) - s_h * t_l);
        /* compute log(ax) */
        s2 = s * s;
        r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
        r += s_l * (s_h + s);
        s2 = s_h * s_h;
        t_h = 3.0f + s2 + r;
        GET_FLOAT_WORD(is, t_h);
        SET_FLOAT_WORD(t_h, is & 0xfffff000);
        t_l = r - ((t_h - 3.0f) - s2);
        /* u+v = s*(1+...) */
        u = s_h * t_h;
        v = s_l * t_h + t_l * s;
        /* 2/(3log2)*(s+...) */
        p_h = u + v;
        GET_FLOAT_WORD(is, p_h);
        SET_FLOAT_WORD(p_h, is & 0xfffff000);
        p_l = v - (p_h - u);
        z_h = cp_h * p_h; /* cp_h+cp_l = 2/(3*log2) */
        z_l = cp_l * p_h + p_l * cp_ + dp_l[k];
        /* log2(ax) = (s+..)*2/(3*log2) = n + dp_h + z_h + z_l */
        t = (KDfloat32)n;
        t1 = (((z_h + z_l) + dp_h[k]) + t);
        GET_FLOAT_WORD(is, t1);
        SET_FLOAT_WORD(t1, is & 0xfffff000);
        t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);
    }
    /* split up y into y1+y2 and compute (y1+y2)*(t1+t2) */
    GET_FLOAT_WORD(is, y);
    SET_FLOAT_WORD(y1, is & 0xfffff000);
    p_l = (y - y1) * t1 + y * t2;
    p_h = y1 * t1;
    z = p_l + p_h;
    GET_FLOAT_WORD(j, z);
    if(j > 0x43000000) /* if z > 128 */
    {
        return sn * huge * huge; /* overflow */
    }
    else if(j == 0x43000000)
    { /* if z == 128 */
        if(p_l + ovt > z - p_h)
        {
            return sn * huge * huge; /* overflow */
        }
    }
    else if((j & KDINT_MAX) > 0x43160000) /* z <= -150 */
    {
        return sn * tiny * tiny; /* underflow */
    }
    else if((j & KDUINT_MAX) == 0xc3160000)
    { /* z == -150 */
        if(p_l <= z - p_h)
        {
            return sn * tiny * tiny; /* underflow */
        }
    }
    /*
     * compute 2**(p_h+p_l)
     */
    i = j & KDINT_MAX;
    k = (i >> 23) - 0x7f;
    n = 0;
    if(i > 0x3f000000)
    { /* if |z| > 0.5, set n = [z+0.5] */
        n = j + (0x00800000 >> (k + 1));
        k = ((n & KDINT_MAX) >> 23) - 0x7f; /* new k for n */
        SET_FLOAT_WORD(t, n & ~(0x007fffff >> k));
        n = ((n & 0x007fffff) | 0x00800000) >> (23 - k);
        if(j < 0)
        {
            n = -n;
        }
        p_h -= t;
    }
    t = p_l + p_h;
    GET_FLOAT_WORD(is, t);
    SET_FLOAT_WORD(t, is & 0xffff8000);
    u = t * lg2_h;
    v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
    z = u + v;
    w = v - (z - u);
    t = z * z;
    t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    r = (z * t1) / (t1 - 2.0f) - (w + z * w);
    z = 1.0f - (r - z);
    GET_FLOAT_WORD(j, z);
    j += (n << 23);
    if((j >> 23) <= 0)
    {
        z = __kdScalbnf(z, n); /* subnormal output */
    }
    else
    {
        SET_FLOAT_WORD(z, j);
    }
    return sn * z;
}

/* kdSqrtf: Square root function. */
KD_API KDfloat32 KD_APIENTRY kdSqrtf(KDfloat32 x)
{
#ifdef __SSE2__
    KDfloat32 result = 0.0f;
    _mm_store_ss(&result, _mm_sqrt_ss(_mm_load_ss(&x)));
    return result;
#else
    KDfloat32 z;
    KDint32 sign = (KDint32)0x80000000;
    KDint32 ix, s, q, m, t, i;
    KDuint32 r;
    GET_FLOAT_WORD(ix, x);
    /* take care of Inf and NaN */
    if((ix & KD_INFINITY) == KD_INFINITY)
    {
        return x * x + x; /* sqrt(NaN)=NaN, sqrt(+inf)=+inf, sqrt(-inf)=sNaN */
    }
    /* take care of zero */
    if(ix <= 0)
    {
        if((ix & (~sign)) == 0)
        {
            return x; /* sqrt(+-0) = +-0 */
        }
        else if(ix < 0)
        {
            return (x - x) / (x - x);
        } /* sqrt(-ve) = sNaN */
    }
    /* normalize x */
    m = (ix >> 23);
    if(m == 0)
    { /* subnormal x */
        for(i = 0; (ix & 0x00800000) == 0; i++)
        {
            ix <<= 1;
        }
        m -= i - 1;
    }
    m -= 127; /* unbias exponent */
    ix = (ix & 0x007fffff) | 0x00800000;
    if(m & 1)
    { /* odd m, double x to make it even */
        ix += ix;
    }
    m >>= 1; /* m = [m/2] */
    /* generate sqrt(x) bit by bit */
    ix += ix;
    q = s = 0;      /* q = sqrt(x) */
    r = 0x01000000; /* r = moving bit from right to left */
    while(r != 0)
    {
        t = s + r;
        if(t <= ix)
        {
            s = t + r;
            ix -= t;
            q += r;
        }
        ix += ix;
        r >>= 1;
    }
    /* use floating add to find out rounding direction */
    if(ix != 0)
    {
        z = 1.0f - tiny; /* trigger inexact flag */
        if(z >= 1.0f)
        {
            z = 1.0f + tiny;
            if(z > 1.0f)
            {
                q += 2;
            }
            else
            {
                q += (q & 1);
            }
        }
    }
    ix = (q >> 1) + 0x3f000000;
    ix += (m << 23);
    SET_FLOAT_WORD(z, ix);
    return z;
#endif
}

/* kdCeilf: Return ceiling value. */
KD_API KDfloat32 KD_APIENTRY kdCeilf(KDfloat32 x)
{
#ifdef __SSE4_1__
    KDfloat32 result = 0.0f;
    _mm_store_ss(&result, _mm_ceil_ss(_mm_load_ss(&result), _mm_load_ss(&x)));
    return result;
#else
    KDint32 i0, j0;
    KDuint32 i;
    GET_FLOAT_WORD(i0, x);
    j0 = ((i0 >> 23) & 0xff) - 0x7f;
    if(j0 < 23)
    {
        if(j0 < 0)
        { /* raise inexact if x != 0 */
            if(huge + x > 0.0f)
            { /* return 0*sign(x) if |x|<1 */
                if(i0 < 0)
                {
                    i0 = 0x80000000;
                }
                else if(i0 != 0)
                {
                    i0 = 0x3f800000;
                }
            }
        }
        else
        {
            i = (0x007fffff) >> j0;
            if((i0 & i) == 0)
            {
                return x;
            } /* x is integral */
            if(huge + x > 0.0f)
            { /* raise inexact flag */
                if(i0 > 0)
                {
                    i0 += (0x00800000) >> j0;
                }
                i0 &= (~i);
            }
        }
    }
    else
    {
        if(j0 == 0x80)
        {
            return x + x; /* inf or NaN */
        }
        else
        {
            return x;
        } /* x is integral */
    }
    SET_FLOAT_WORD(x, i0);
    return x;
#endif
}

/* kdFloorf: Return floor value. */
KD_API KDfloat32 KD_APIENTRY kdFloorf(KDfloat32 x)
{
#ifdef __SSE4_1__
    KDfloat32 result = 0.0f;
    _mm_store_ss(&result, _mm_floor_ss(_mm_load_ss(&result), _mm_load_ss(&x)));
    return result;
#else
    KDint32 i0, j0;
    KDuint32 i;
    GET_FLOAT_WORD(i0, x);
    j0 = ((i0 >> 23) & 0xff) - 0x7f;
    if(j0 < 23)
    {
        if(j0 < 0)
        { /* raise inexact if x != 0 */
            if(huge + x > 0.0f)
            { /* return 0*sign(x) if |x|<1 */
                if(i0 >= 0)
                {
                    i0 = 0;
                }
                else if((i0 & KDINT_MAX) != 0)
                {
                    i0 = 0xbf800000;
                }
            }
        }
        else
        {
            i = (0x007fffff) >> j0;
            if((i0 & i) == 0)
            {
                return x;
            } /* x is integral */
            if(huge + x > 0.0f)
            { /* raise inexact flag */
                if(i0 < 0)
                {
                    i0 += (0x00800000) >> j0;
                }
                i0 &= (~i);
            }
        }
    }
    else
    {
        if(j0 == 0x80)
        {
            return x + x; /* inf or NaN */
        }
        else
        {
            return x;
        } /* x is integral */
    }
    SET_FLOAT_WORD(x, i0);
    return x;
#endif
}

/* kdRoundf: Round value to nearest integer. */
KD_API KDfloat32 KD_APIENTRY kdRoundf(KDfloat32 x)
{
#ifdef __SSE4_1__
    KDfloat32 result = 0.0f;
    _mm_store_ss(&result, _mm_round_ss(_mm_load_ss(&result), _mm_load_ss(&x), _MM_FROUND_TO_NEAREST_INT));
    return result;
#else
    KDfloat32 t;
    KDuint32 hx;
    GET_FLOAT_WORD(hx, x);
    if((hx & KDINT_MAX) == KD_INFINITY)
    {
        return (x + x);
    }
    if(!(hx & 0x80000000))
    {
        t = kdFloorf(x);
        if(t - x <= -0.5f)
        {
            t += 1;
        }
        return (t);
    }
    else
    {
        t = kdFloorf(-x);
        if(t + x <= -0.5f)
        {
            t += 1;
        }
        return (-t);
    }
#endif
}

/* kdInvsqrtf: Inverse square root function. */
KD_API KDfloat32 KD_APIENTRY kdInvsqrtf(KDfloat32 x)
{
#ifdef __SSE__
    KDfloat32 result = 0.0f;
    _mm_store_ss(&result, _mm_rsqrt_ss(_mm_load_ss(&x)));
    result = (0.5f * (result + 1.0f / (x * result)));
    return result;
#else
    return 1.0f / kdSqrtf(x);
#endif
}

/* kdFmodf: Calculate floating point remainder. */
KD_API KDfloat32 KD_APIENTRY kdFmodf(KDfloat32 x, KDfloat32 y)
{
    KDint32 n, hx, hy, hz, ix, iy, sx, i;
    GET_FLOAT_WORD(hx, x);
    GET_FLOAT_WORD(hy, y);
    sx = hx & 0x80000000; /* sign of x */
    hx ^= sx;             /* |x| */
    hy &= KDINT_MAX;      /* |y| */
    /* purge off exception values */
    /* y=0,or x not finite */
    if(hy == 0 || (hx >= KD_INFINITY) || (hy > KD_INFINITY))
    { /* or y is NaN */
        return (x * y) / (x * y);
    }
    if(hx < hy)
    {
        return x;
    } /* |x|<|y| return x */
    if(hx == hy)
    {
        return Zero[(KDuint32)sx >> 31]; /* |x|=|y| return x*0*/
    }
    /* determine ix = ilogb(x) */
    if(hx < 0x00800000)
    { /* subnormal x */
        for(ix = -126, i = (hx << 8); i > 0; i <<= 1)
        {
            ix -= 1;
        }
    }
    else
    {
        ix = (hx >> 23) - 127;
    }
    /* determine iy = ilogb(y) */
    if(hy < 0x00800000)
    { /* subnormal y */
        for(iy = -126, i = (hy << 8); i >= 0; i <<= 1)
        {
            iy -= 1;
        }
    }
    else
    {
        iy = (hy >> 23) - 127;
    }
    /* set up {hx,lx}, {hy,ly} and align y to x */
    if(ix >= -126)
    {
        hx = 0x00800000 | (0x007fffff & hx);
    }
    else
    { /* subnormal x, shift x to normal */
        n = -126 - ix;
        hx = hx << n;
    }
    if(iy >= -126)
    {
        hy = 0x00800000 | (0x007fffff & hy);
    }
    else
    { /* subnormal y, shift y to normal */
        n = -126 - iy;
        hy = hy << n;
    }
    /* fix point fmod */
    n = ix - iy;
    while(n--)
    {
        hz = hx - hy;
        if(hz < 0)
        {
            hx = hx + hx;
        }
        else
        {
            if(hz == 0) /* return sign(x)*0 */
            {
                return Zero[(KDuint32)sx >> 31];
            }
            hx = hz + hz;
        }
    }
    hz = hx - hy;
    if(hz >= 0)
    {
        hx = hz;
    }
    /* convert back to floating value and restore the sign */
    if(hx == 0) /* return sign(x)*0 */
    {
        return Zero[(KDuint32)sx >> 31];
    }
    while(hx < 0x00800000)
    { /* normalize x */
        hx = hx + hx;
        iy -= 1;
    }
    if(iy >= -126)
    { /* normalize output */
        hx = ((hx - 0x00800000) | ((iy + 127) << 23));
        SET_FLOAT_WORD(x, hx | sx);
    }
    else
    { /* subnormal output */
        n = -126 - iy;
        hx >>= n;
        SET_FLOAT_WORD(x, hx | sx);
        x *= 1.0f; /* create necessary signal */
    }
    return x; /* exact output */
}

KD_API KDfloat64KHR KD_APIENTRY kdSqrtKHR(KDfloat64KHR x)
{
#ifdef __SSE2__
    KDfloat64KHR result = 0.0;
    _mm_store_sd(&result, _mm_sqrt_sd(_mm_load_sd(&result), _mm_load_sd(&x)));
    return result;
#else
    KDfloat64KHR z;
    KDint32 sign = (KDint)0x80000000;
    KDint32 ix0, s0, q, m, t, i;
    KDuint32 r, t1, s1, ix1, q1;

    EXTRACT_WORDS(ix0, ix1, x);

    /* take care of Inf and NaN */
    if((ix0 & 0x7ff00000) == 0x7ff00000)
    {
        return x * x + x; /* sqrt(NaN)=NaN, sqrt(+inf)=+inf
                       sqrt(-inf)=sNaN */
    }
    /* take care of zero */
    if(ix0 <= 0)
    {
        if(((ix0 & (~sign)) | ix1) == 0)
        {
            return x; /* sqrt(+-0) = +-0 */
        }
        else if(ix0 < 0)
        {
            return (x - x) / (x - x);
        } /* sqrt(-ve) = sNaN */
    }
    /* normalize x */
    m = (ix0 >> 20);
    if(m == 0)
    { /* subnormal x */
        while(ix0 == 0)
        {
            m -= 21;
            ix0 |= (ix1 >> 11);
            ix1 <<= 21;
        }
        for(i = 0; (ix0 & 0x00100000) == 0; i++)
        {
            ix0 <<= 1;
        }
        /* Bit shifting by 32 is undefined behaviour*/
        if(i > 0)
        {
            m -= i - 1;
            ix0 |= (ix1 >> (32 - i));
            ix1 <<= i;
        }
    }
    m -= 1023; /* unbias exponent */
    ix0 = (ix0 & 0x000fffff) | 0x00100000;
    if(m & 1)
    { /* odd m, double x to make it even */
        ix0 += ix0 + ((ix1 & sign) >> 31);
        ix1 += ix1;
    }
    m >>= 1; /* m = [m/2] */

    /* generate sqrt(x) bit by bit */
    ix0 += ix0 + ((ix1 & sign) >> 31);
    ix1 += ix1;
    q = q1 = s0 = s1 = 0; /* [q,q1] = sqrt(x) */
    r = 0x00200000;       /* r = moving bit from right to left */

    while(r != 0)
    {
        t = s0 + r;
        if(t <= ix0)
        {
            s0 = t + r;
            ix0 -= t;
            q += r;
        }
        ix0 += ix0 + ((ix1 & sign) >> 31);
        ix1 += ix1;
        r >>= 1;
    }

    r = sign;
    while(r != 0)
    {
        t1 = s1 + r;
        t = s0;
        if((t < ix0) || ((t == ix0) && (t1 <= ix1)))
        {
            s1 = t1 + r;
            if(((t1 & sign) == (KDuint32)sign) && (s1 & sign) == 0)
            {
                s0 += 1;
            }
            ix0 -= t;
            if(ix1 < t1)
            {
                ix0 -= 1;
            }
            ix1 -= t1;
            q1 += r;
        }
        ix0 += ix0 + ((ix1 & sign) >> 31);
        ix1 += ix1;
        r >>= 1;
    }

    /* use floating add to find out rounding direction */
    if((ix0 | ix1) != 0)
    {
        z = 1.0f - tiny; /* trigger inexact flag */
        if(z >= 1.0f)
        {
            z = 1.0f + tiny;
            if(q1 == (KDuint32)KDUINT_MAX)
            {
                q1 = 0;
                q += 1;
            }
            else if(z > 1.0f)
            {
                if(q1 == (KDuint32)0xfffffffe)
                {
                    q += 1;
                }
                q1 += 2;
            }
            else
            {
                q1 += (q1 & 1);
            }
        }
    }
    ix0 = (q >> 1) + 0x3fe00000;
    ix1 = q1 >> 1;
    if((q & 1) == 1)
    {
        ix1 |= sign;
    }
    ix0 += (m << 20);
    INSERT_WORDS(z, ix0, ix1);
    return z;
#endif
}

KD_API KDfloat64KHR KD_APIENTRY kdFloorKHR(KDfloat64KHR x)
{
#ifdef __SSE4_1__
    KDfloat64KHR result = 0.0;
    _mm_store_sd(&result, _mm_floor_sd(_mm_load_sd(&result), _mm_load_sd(&x)));
    return result;
#else
    KDint32 i0, i1, j0;
    KDuint32 i, j;
    EXTRACT_WORDS(i0, i1, x);
    j0 = ((i0 >> 20) & 0x7ff) - 0x3ff;
    if(j0 < 20)
    {
        if(j0 < 0)
        { /* raise inexact if x != 0 */
            if(huge + x > 0.0)
            { /* return 0*sign(x) if |x|<1 */
                if(i0 >= 0)
                {
                    i0 = i1 = 0;
                }
                else if(((i0 & KDINT_MAX) | i1) != 0)
                {
                    i0 = 0xbff00000;
                    i1 = 0;
                }
            }
        }
        else
        {
            i = (0x000fffff) >> j0;
            if(((i0 & i) | i1) == 0)
            {
                return x;
            } /* x is integral */
            if(huge + x > 0.0)
            { /* raise inexact flag */
                if(i0 < 0)
                {
                    i0 += (0x00100000) >> j0;
                }
                i0 &= (~i);
                i1 = 0;
            }
        }
    }
    else if(j0 > 51)
    {
        if(j0 == 0x400)
        {
            return x + x; /* inf or NaN */
        }
        else
        {
            return x;
        } /* x is integral */
    }
    else
    {
        i = ((KDuint32)(KDUINT_MAX)) >> (j0 - 20);
        if((i1 & i) == 0)
        {
            return x;
        } /* x is integral */
        if(huge + x > 0.0)
        { /* raise inexact flag */
            if(i0 < 0)
            {
                if(j0 == 20)
                {
                    i0 += 1;
                }
                else
                {
                    j = i1 + (1 << (52 - j0));
                    if((KDint32)j < i1)
                    {
                        i0 += 1; /* got a carry */
                    }
                    i1 = j;
                }
            }
            i1 &= (~i);
        }
    }
    INSERT_WORDS(x, i0, i1);
    return x;
#endif
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/******************************************************************************
 * String and memory functions
 *
 * Notes:
 * - Based on the BSD libc developed at the University of California, Berkeley
 * - kdStrcpy_s/kdStrncat_s based on strlcpy/strlcat by Todd C. Miller
 ******************************************************************************/
/******************************************************************************
 * Copyright (c) 1990, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 ******************************************************************************/
/******************************************************************************
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

/*
 * Copy a block of memory, handling overlap.
 * This is the routine that actually implements kdMemcpy, and kdMemmove.
 */
typedef KDint word; /* "word" used for optimal copy speed */
#define wsize sizeof(word)
#define wmask (wsize - 1)
static void *__kdBcopy(void *dst0, const void *src0, size_t length)
{
    KDint8 *dst = dst0;
    const KDint8 *src = src0;
    KDsize t;

    if(length == 0 || dst == src)
    { /* nothing to do */
        return (dst0);
    }
    if((KDuintptr)dst < (KDuintptr)src)
    {
        /*
         * Copy forward.
         */
        t = (KDuintptr)src; /* only need low bits */
        if((t | (KDuintptr)dst) & wmask)
        {
            /*
             * Try to align operands.  This cannot be done
             * unless the low bits match.
             */
            if((t ^ (KDuintptr)dst) & wmask || length < wsize)
            {
                t = length;
            }
            else
            {
                t = wsize - (t & wmask);
            }
            length -= t;
            do
            {
                *dst++ = *src++;
            } while(--t);
        }
        /*
         * Copy whole words, then mop up any trailing bytes.
         */
        t = length / wsize;
        if(t)
        {
            do
            {
                *(word *)dst = *(word *)src;
                src += wsize;
                dst += wsize;
            } while(--t);
        }
        t = length & wmask;
        if(t)
        {
            do
            {
                *dst++ = *src++;
            } while(--t);
        }
    }
    else
    {
        /*
         * Copy backwards.  Otherwise essentially the same.
         * Alignment works as before, except that it takes
         * (t&wmask) bytes to align, not wsize-(t&wmask).
         */
        src += length;
        dst += length;
        t = (KDuintptr)src;
        if((t | (KDuintptr)dst) & wmask)
        {
            if((t ^ (KDuintptr)dst) & wmask || length <= wsize)
            {
                t = length;
            }
            else
            {
                t &= wmask;
            }
            length -= t;
            do
            {
                *--dst = *--src;
            } while(--t);
        }
        t = length / wsize;
        if(t)
        {
            do
            {
                src -= wsize;
                dst -= wsize;
                *(word *)dst = *(word *)src;
            } while(--t);
        }
        t = length & wmask;
        if(t)
        {
            do
            {
                *--dst = *--src;
            } while(--t);
        }
    }
    return (dst0);
}
#undef wmask
#undef wsize

/* kdMemchr: Scan memory for a byte value. */
KD_API void *KD_APIENTRY kdMemchr(const void *src, KDint byte, KDsize len)
{
    if(len != 0)
    {
        const KDuint8 *p = src;
        do
        {
            if(*p++ == (KDuint8)byte)
            {
                return ((void *)(p - 1));
            }
        } while(--len != 0);
    }
    return KD_NULL;
}

/* kdMemcmp: Compare two memory regions. */
KD_API KDint KD_APIENTRY kdMemcmp(const void *src1, const void *src2, KDsize len)
{
    if(len != 0)
    {
        const KDuint8 *p1 = src1, *p2 = src2;
        do
        {
            if(*p1++ != *p2++)
            {
                return (*--p1 - *--p2);
            }
        } while(--len != 0);
    }
    return 0;
}

/* kdMemcpy: Copy a memory region, no overlapping. */
KD_API void *KD_APIENTRY kdMemcpy(void *buf, const void *src, KDsize len)
{
    return __kdBcopy(buf, src, len);
}

/* kdMemmove: Copy a memory region, overlapping allowed. */
KD_API void *KD_APIENTRY kdMemmove(void *buf, const void *src, KDsize len)
{
    return __kdBcopy(buf, src, len);
}

/* kdMemset: Set bytes in memory to a value. */
KD_API void *KD_APIENTRY kdMemset(void *buf, KDint byte, KDsize len)
{
    KDuint8 *p = (KDuint8 *)buf;
    while(len--)
    {
        *p++ = (KDuint8)byte;
    }
    return buf;
}

/* kdStrchr: Scan string for a byte value. */
KD_API KDchar *KD_APIENTRY kdStrchr(const KDchar *str, KDint ch)
{
    KDchar c;
    c = (KDchar)ch;
    for(;; ++str)
    {
        if(*str == c)
        {
            return ((KDchar *)str);
        }
        if(*str == '\0')
        {
            return KD_NULL;
        }
    }
}

/* kdStrcmp: Compares two strings. */
KD_API KDint KD_APIENTRY kdStrcmp(const KDchar *str1, const KDchar *str2)
{
    while(*str1 == *str2++)
    {
        if(*str1++ == '\0')
        {
            return 0;
        }
    }
    return (KDint)(*(KDuint8 *)str1 - *(KDuint8 *)(str2 - 1));
}

/* kdStrlen: Determine the length of a string. */
/*
 * Portable strlen() for 32-bit and 64-bit systems.
 *
 * Rationale: it is generally much more efficient to do word length
 * operations and avoid branches on modern computer systems, as
 * compared to byte-length operations with a lot of branches.
 *
 * The expression:
 *
 *  ((x - 0x01....01) & ~x & 0x80....80)
 *
 * would evaluate to a non-zero value if any of the bytes in the
 * original word is zero.
 *
 * On multi-issue processors, we can divide the above expression into:
 *  a)  (x - 0x01....01)
 *  b) (~x & 0x80....80)
 *  c) a & b
 *
 * Where, a) and b) can be partially computed in parallel.
 *
 * The algorithm above is found on "Hacker's Delight" by
 * Henry S. Warren, Jr.
 */

/* Helper function to return string length if we caught the zero byte. */
#define testbyte(x)                 \
    do                              \
    {                               \
        if(p[x] == '\0')            \
        {                           \
            return (p - str + (x)); \
        }                           \
    } while(0)

KD_API KDsize KD_APIENTRY kdStrlen(const KDchar *str)
{
/* Magic numbers for the algorithm */
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64_)
    const KDuint64 mask01 = 0x0101010101010101;
    const KDuint64 mask80 = 0x8080808080808080;
#elif defined(__i386) || defined(_M_IX86) || defined(__arm__) || defined(_M_ARM) || defined(__EMSCRIPTEN__)
    const KDuint64 mask01 = 0x01010101;
    const KDuint64 mask80 = 0x80808080;
#else
#error Unsupported arch
#endif
    const KDchar *p;
    const KDuint64 *lp;
    KDint64 va, vb;

    /*
     * Before trying the hard (unaligned byte-by-byte access) way
     * to figure out whether there is a nul character, try to see
     * if there is a nul character is within this accessible word
     * first.
     *
     * p and (p & ~(sizeof(KDint64) - 1)) must be equally accessible since
     * they always fall in the same memory page, as long as page
     * boundaries is integral multiple of word size.
     */
    lp = (const KDuint64 *)((KDuintptr)str & ~(sizeof(KDint64) - 1));
    va = (*lp - mask01);
    vb = ((~*lp) & mask80);
    lp++;
    if(va & vb)
    {
        /* Check if we have \0 in the first part */
        for(p = str; p < (const KDchar *)lp; p++)
        {
            if(*p == '\0')
            {
                return (p - str);
            }
        }
    }

    /* Scan the rest of the string using word sized operation */
    for(;; lp++)
    {
        va = (*lp - mask01);
        vb = ((~*lp) & mask80);
        if(va & vb)
        {
            p = (const KDchar *)(lp);
            testbyte(0);
            testbyte(1);
            testbyte(2);
            testbyte(3);
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64_)
            testbyte(4);
            testbyte(5);
            testbyte(6);
            testbyte(7);
#endif
        }
    }
}
#undef testbyte

/* kdStrnlen: Determine the length of a string. */
KD_API KDsize KD_APIENTRY kdStrnlen(const KDchar *str, KDsize maxlen)
{
    KDsize len;
    for(len = 0; len < maxlen; len++, str++)
    {
        if(!*str)
        {
            break;
        }
    }
    return len;
}

/* kdStrncat_s: Concatenate two strings. */
KD_API KDint KD_APIENTRY kdStrncat_s(KDchar *buf, KDsize buflen, const KDchar *src, KD_UNUSED KDsize srcmaxlen)
{
    KDchar *d = buf;
    const KDchar *s = src;
    KDsize n = buflen;
    KDsize dlen;

    /* Find the end of dst and adjust bytes left but don't go past end */
    while(n-- != 0 && *d != '\0')
    {
        d++;
    }
    dlen = d - buf;
    n = buflen - dlen;

    if(n == 0)
    {
        return (KDint)(dlen + kdStrlen(s));
    }
    while(*s != '\0')
    {
        if(n != 1)
        {
            *d++ = *s;
            n--;
        }
        s++;
    }
    *d = '\0';

    return (KDint)(dlen + (s - src)); /* count does not include NUL */
}

/* kdStrncmp: Compares two strings with length limit. */
KD_API KDint KD_APIENTRY kdStrncmp(const KDchar *str1, const KDchar *str2, KDsize maxlen)
{
    if(maxlen == 0)
    {
        return 0;
    }
    do
    {
        if(*str1 != *str2++)
        {
            return (*(const KDuint8 *)str1 - *(const KDuint8 *)(str2 - 1));
        }
        if(*str1++ == '\0')
        {
            break;
        }
    } while(--maxlen != 0);
    return 0;
}


/* kdStrcpy_s: Copy a string with an overrun check. */
KD_API KDint KD_APIENTRY kdStrcpy_s(KDchar *buf, KDsize buflen, const KDchar *src)
{
    KDchar *d = buf;
    const KDchar *s = src;
    KDsize n = buflen;

    /* Copy as many bytes as will fit */
    if(n != 0)
    {
        while(--n != 0)
        {
            if((*d++ = *s++) == '\0')
            {
                break;
            }
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if(n == 0)
    {
        if(buflen != 0)
        {
            *d = '\0';
        } /* NUL-terminate dst */
        while(*s++)
        {
            ;
        }
    }

    return (KDint)(s - src - 1); /* count does not include NUL */
}

/* kdStrncpy_s: Copy a string with an overrun check. */
KD_API KDint KD_APIENTRY kdStrncpy_s(KDchar *buf, KDsize buflen, const KDchar *src, KD_UNUSED KDsize srclen)
{
    if(buflen != 0)
    {
        KDchar *d = buf;
        const KDchar *s = src;

        do
        {
            if((*d++ = *s++) == '\0')
            {
                /* NUL pad the remaining n-1 bytes */
                while(--buflen != 0)
                {
                    *d++ = '\0';
                }
                break;
            }
        } while(--buflen != 0);
    }
    else
    {
        return -1;
    }
    return 0;
}

/* kdStrstrVEN: Locate substring. */
KD_API KDchar *KD_APIENTRY kdStrstrVEN(const KDchar *str1, const KDchar *str2)
{
    KDchar c, sc;
    KDsize len;

    if((c = *str2++) != '\0')
    {
        len = kdStrlen(str2);
        do
        {
            do
            {
                if((sc = *str1++) == '\0')
                {
                    return (NULL);
                }
            } while(sc != c);
        } while(kdStrncmp(str1, str2, len) != 0);
        str1--;
    }
    return (KDchar *)str1;
}


/******************************************************************************
 * Time functions
 ******************************************************************************/

/* kdGetTimeUST: Get the current unadjusted system time. */
KD_API KDust KD_APIENTRY kdGetTimeUST(void)
{
#if defined(_WIN32)
    FILETIME filetime;
    ULARGE_INTEGER largeuint;
    /* 100-nanosecond intervals */
    GetSystemTimeAsFileTime(&filetime);
    largeuint.LowPart = filetime.dwLowDateTime;
    largeuint.HighPart = filetime.dwHighDateTime;
    return largeuint.QuadPart * 100;
#elif defined(__linux__)
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_nsec;
#else
    return clock();
#endif
}

/* kdTime: Get the current wall clock time. */
KD_API KDtime KD_APIENTRY kdTime(KDtime *timep)
{
#if defined(_WIN32)
    FILETIME filetime;
    ULARGE_INTEGER largeuint;
    GetSystemTimeAsFileTime(&filetime);
    largeuint.LowPart = filetime.dwLowDateTime;
    largeuint.HighPart = filetime.dwHighDateTime;
    /* See RtlTimeToSecondsSince1970 */
    KDtime time = (KDtime)((largeuint.QuadPart / 10000000) - 11644473600LL);
    (*timep) = time;
    return time;
#else
    return time((time_t *)timep);
#endif
}

/* kdGmtime_r, kdLocaltime_r: Convert a seconds-since-epoch time into broken-down time. */
static KDboolean __kdIsleap(KDint32 year)
{
    return (!((year) % 4) && (((year) % 100) || !((year) % 400)));
}
KD_API KDTm *KD_APIENTRY kdGmtime_r(const KDtime *timep, KDTm *result)
{
    KDint32 secs_per_day = 3600 * 24;
    KDint32 days_in_secs = (KDint32)(*timep % secs_per_day);
    KDint32 days = (KDint32)(*timep / secs_per_day);
    result->tm_sec = days_in_secs % 60;
    result->tm_min = (days_in_secs % 3600) / 60;
    result->tm_hour = days_in_secs / 3600;
    result->tm_wday = (days + 4) % 7;

    KDint32 year = 1970;
    while(days >= (__kdIsleap(year) ? 366 : 365))
    {
        days -= (__kdIsleap(year) ? 366 : 365);
        year++;
    }
    result->tm_year = year - 1900;
    result->tm_yday = days;
    result->tm_mon = 0;

    const KDint months[2][12] = {
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};

    while(days >= months[__kdIsleap(year)][result->tm_mon])
    {
        days -= months[__kdIsleap(year)][result->tm_mon];
        result->tm_mon++;
    }
    result->tm_mday = days + 1;
    result->tm_isdst = 0;

    return result;
}
KD_API KDTm *KD_APIENTRY kdLocaltime_r(const KDtime *timep, KDTm *result)
{
    /* No timezone support */
    return kdGmtime_r(timep, result);
}

/* kdUSTAtEpoch: Get the UST corresponding to KDtime 0. */
KD_API KDust KD_APIENTRY kdUSTAtEpoch(void)
{
    /* TODO: Implement */
    kdAssert(0);
    return 0;
}

/******************************************************************************
 * Timer functions
 ******************************************************************************/

/* kdSetTimer: Set timer. */
typedef struct {
    KDint64 interval;
    KDint periodic;
    void *eventuserptr;
    KDThread *destination;
} __KDTimerPayload;
static void *__kdTimerHandler(void *arg)
{
    __KDTimerPayload *payload = (__KDTimerPayload *)arg;
    for(;;)
    {
        kdThreadSleepVEN(payload->interval);

        /* Post event to the original thread */
        KDEvent *timerevent = kdCreateEvent();
        timerevent->type = KD_EVENT_TIMER;
        timerevent->userptr = payload->eventuserptr;
        kdPostThreadEvent(timerevent, payload->destination);

        /* Abort if this is a oneshot timer*/
        if(payload->periodic == KD_TIMER_ONESHOT)
        {
            break;
        }

        /* Check for quit event send by kdCancelTimer */
        const KDEvent *event = kdWaitEvent(-1);
        if(event)
        {
            if(event->type == KD_EVENT_QUIT)
            {
                break;
            }
            kdDefaultEvent(event);
        }
    }
    return 0;
}
struct KDTimer {
    KDThread *thread;
    KDThread *originthr;
    __KDTimerPayload *payload;
};
KD_API KDTimer *KD_APIENTRY kdSetTimer(KDint64 interval, KDint periodic, void *eventuserptr)
{
    if(periodic != KD_TIMER_ONESHOT && periodic != KD_TIMER_PERIODIC_AVERAGE && periodic != KD_TIMER_PERIODIC_MINIMUM)
    {
        kdAssert(0);
    }

    __KDTimerPayload *payload = (__KDTimerPayload *)kdMalloc(sizeof(__KDTimerPayload));
    if(payload == KD_NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    payload->interval = interval;
    payload->periodic = periodic;
    payload->eventuserptr = eventuserptr;
    payload->destination = kdThreadSelf();

    KDTimer *timer = (KDTimer *)kdMalloc(sizeof(KDTimer));
    if(timer == KD_NULL)
    {
        kdFree(payload);
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    timer->thread = kdThreadCreate(KD_NULL, __kdTimerHandler, payload);
    if(timer->thread == KD_NULL)
    {
        kdFree(timer);
        kdFree(payload);
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    timer->originthr = kdThreadSelf();
    timer->payload = payload;
    return timer;
}

/* kdCancelTimer: Cancel and free a timer. */
KD_API KDint KD_APIENTRY kdCancelTimer(KDTimer *timer)
{
    if(timer->originthr != kdThreadSelf())
    {
        kdSetError(KD_EINVAL);
        return -1;
    }
    /* Post quit event to the timer thread */
    KDEvent *event = kdCreateEvent();
    event->type = KD_EVENT_QUIT;
    kdPostThreadEvent(event, timer->thread);
    kdThreadJoin(timer->thread, KD_NULL);
    kdFree(timer->payload);
    kdFree(timer);
    return 0;
}

/******************************************************************************
 * File system
 ******************************************************************************/

/* kdFopen: Open a file from the file system. */
struct KDFile {
#if defined(_WIN32)
    HANDLE file;
#else
    FILE *file;
#endif
    const KDchar *pathname;
    KDboolean eof;
    KDboolean error;
};
KD_API KDFile *KD_APIENTRY kdFopen(const KDchar *pathname, const KDchar *mode)
{
    KDFile *file = (KDFile *)kdMalloc(sizeof(KDFile));
    file->pathname = pathname;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    DWORD access = 0;
    DWORD create = 0;
    switch(mode[0])
    {
        case 'w':
        {
            access = GENERIC_WRITE;
            create = CREATE_ALWAYS;
            break;
        }
        case 'r':
        {
            access = GENERIC_READ;
            create = OPEN_EXISTING;
            break;
        }
        case 'a':
        {
            access = GENERIC_READ | GENERIC_WRITE;
            create = OPEN_ALWAYS;
            break;
        }
        default:
        {
            kdSetError(KD_EINVAL);
            return KD_NULL;
        }
    }
    if(mode[1] == '+' || mode[2] == '+')
    {
        access = GENERIC_READ | GENERIC_WRITE;
    }
    file->file = CreateFile(pathname, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, create, 0, NULL);
    if(file->file != INVALID_HANDLE_VALUE)
    {
        if(mode[0] == 'a')
        {
            SetFilePointer(file, 0, NULL, FILE_END);
        }
    }
    else
    {
        error = GetLastError();
#else
    file->file = fopen(pathname, mode);
    if(file->file == NULL)
    {
        kdFree(file->file);
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EACCES | KD_EINVAL | KD_EIO | KD_EISDIR |
            KD_EMFILE | KD_ENAMETOOLONG | KD_ENOENT | KD_ENOMEM | KD_ENOSPC);
        return KD_NULL;
    }
    file->eof = 0;
    return file;
}

/* kdFclose: Close an open file. */
KD_API KDint KD_APIENTRY kdFclose(KDFile *file)
{
    KDint retval = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    retval = CloseHandle(file->file);
    if(retval == 0)
    {
        error = GetLastError();
#else
    retval = fclose(file->file);
    if(retval == EOF)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EFBIG | KD_EIO | KD_ENOMEM | KD_ENOSPC);
        return KD_EOF;
    }
    kdFree(file);
    return 0;
}

/* kdFflush: Flush an open file. */
KD_API KDint KD_APIENTRY kdFflush(KDFile *file)
{
#if !defined(_WIN32)
    KDint retval = fflush(file->file);
    if(retval == EOF)
    {
        kdSetErrorPlatformVEN(errno, KD_EFBIG | KD_EIO | KD_ENOMEM | KD_ENOSPC);
        return KD_EOF;
    }
#endif
    return 0;
}

/* kdFread: Read from a file. */
KD_API KDsize KD_APIENTRY kdFread(void *buffer, KDsize size, KDsize count, KDFile *file)
{
    KDsize retval = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    DWORD bytesread = 0;
    retval = ReadFile(file->file, buffer, (DWORD)(count * size), &bytesread, NULL) ? bytesread / size : 0;
    if(retval != size)
    {
        error = GetLastError();
#else
    retval = fread(buffer, size, count, file->file);
    if(retval != size)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EFBIG | KD_EIO | KD_ENOMEM | KD_ENOSPC);
        return kdFerror(file);
    }
    return retval;
}

/* kdFwrite: Write to a file. */
KD_API KDsize KD_APIENTRY kdFwrite(const void *buffer, KDsize size, KDsize count, KDFile *file)
{
    KDsize retval = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    DWORD byteswritten = 0;
    retval = WriteFile(file->file, buffer, (DWORD)(count * size), &byteswritten, NULL) ? byteswritten / size : 0;
    if(retval != size)
    {
        error = GetLastError();
#else
    retval = fwrite(buffer, size, count, file->file);
    if(retval != size)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EBADF | KD_EFBIG | KD_ENOMEM | KD_ENOSPC);
        return kdFerror(file);
    }
    return retval;
}

/* kdGetc: Read next byte from an open file. */
KD_API KDint KD_APIENTRY kdGetc(KDFile *file)
{
    KDint retval = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    if(ReadFile(file->file, &retval, 1, (DWORD[]){0}, NULL) == TRUE)
    {
        error = GetLastError();
#else
    retval = fgetc(file->file);
    if(retval == EOF)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EFBIG | KD_EIO | KD_ENOMEM | KD_ENOSPC);
        return kdFerror(file);
    }
    return retval;
}

/* kdPutc: Write a byte to an open file. */
KD_API KDint KD_APIENTRY kdPutc(KDint c, KDFile *file)
{
    KDint retval = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    if(WriteFile(file->file, &retval , 1, (DWORD[]){0}, NULL) == TRUE)
    {
        error = GetLastError();
#else
    retval = fputc(retval , file->file);
    if(retval == EOF)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EBADF | KD_EFBIG | KD_ENOMEM | KD_ENOSPC);
        return kdFerror(file);
    }
    c = retval;
    return c;
}

/* kdFgets: Read a line of text from an open file. */
KD_API KDchar *KD_APIENTRY kdFgets(KDchar *buffer, KDsize buflen, KDFile *file)
{
    KDchar *line = buffer;
    for(KDsize i = buflen; i > 1; --i)
    {
        KDint character = kdGetc(file);
        if(character == KD_EOF)
        {
            if(i == buflen - 1)
            {
                return KD_NULL;
            }
            break;
        }
        *line++ = (KDchar)character;
        if(character == '\n')
        {
            break;
        }
    }
    return line;
}

/* kdFEOF: Check for end of file. */
KD_API KDint KD_APIENTRY kdFEOF(KDFile *file)
{
#if defined(_WIN32)
    if(file->eof == 1)
#else
    KDint error = feof(file->file);
    if(error != 0)
#endif
    {
        return KD_EOF;
    }
    return 0;
}

/* kdFerror: Check for an error condition on an open file. */
KD_API KDint KD_APIENTRY kdFerror(KDFile *file)
{
#if defined(_WIN32)
    if(file->error == 1)
#else
    if(ferror(file->file) != 0)
#endif
    {
        return KD_EOF;
    }
    return 0;
}

/* kdClearerr: Clear a file's error and end-of-file indicators. */
KD_API void KD_APIENTRY kdClearerr(KDFile *file)
{
#if defined(_WIN32)
    file->eof = 0;
    file->error = 0;
#else
    clearerr(file->file);
#endif
}

/* TODO: Cleanup */ 
typedef struct {
#if defined(_MSC_VER)
    KDint seekorigin_kd;
#else
    KDuint seekorigin_kd;
#endif
#if defined(_WIN32)
    DWORD seekorigin;
#else
    KDint seekorigin;
#endif
} __KDSeekOrigin;

#if defined(_WIN32)
static __KDSeekOrigin seekorigins[] = {{KD_SEEK_SET, FILE_BEGIN}, {KD_SEEK_CUR, FILE_CURRENT}, {KD_SEEK_END, FILE_END}};
#else
static __KDSeekOrigin seekorigins[] = {{KD_SEEK_SET, SEEK_SET}, {KD_SEEK_CUR, SEEK_CUR}, {KD_SEEK_END, SEEK_END}};
#endif

/* kdFseek: Reposition the file position indicator in a file. */
KD_API KDint KD_APIENTRY kdFseek(KDFile *file, KDoff offset, KDfileSeekOrigin origin)
{
    KDPlatformErrorVEN error = 0;
    for(KDuint i = 0; i < sizeof(seekorigins) / sizeof(seekorigins[0]); i++)
    {
        if(seekorigins[i].seekorigin_kd == origin)
        {
#if defined(_WIN32)
            DWORD retval = SetFilePointer(file->file, (LONG)offset, NULL, seekorigins[i].seekorigin);
            if(retval == INVALID_SET_FILE_POINTER)
            {
                error = GetLastError();
#else
            KDint retval = fseek(file->file, (KDint32)offset, seekorigins[i].seekorigin);
            if(retval != 0)
            {
                error = errno;
#endif
                kdSetErrorPlatformVEN(error, KD_EFBIG | KD_EINVAL | KD_EIO | KD_ENOMEM | KD_ENOSPC | KD_EOVERFLOW);
                return -1;
            }
            break;
        }
    }
    return 0;
}

/* kdFtell: Get the file position of an open file. */
KD_API KDoff KD_APIENTRY kdFtell(KDFile *file)
{
    KDoff position = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    position = (KDoff)SetFilePointer(file->file, 0, NULL, FILE_CURRENT);
    if(position == INVALID_SET_FILE_POINTER)
    {
        error = GetLastError();
#else
    position = ftell(file->file);
    if(position == -1)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EOVERFLOW);
        return -1;
    }
    return position;
}

/* kdMkdir: Create new directory. */
KD_API KDint KD_APIENTRY kdMkdir(const KDchar *pathname)
{
    KDint retval = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    retval = CreateDirectory(pathname, NULL);
    if(retval == 0)
    {
        error = GetLastError();
#else
    retval = mkdir(pathname, S_IRWXU);
    if(retval == -1)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EACCES | KD_EEXIST | KD_EIO | KD_ENAMETOOLONG | KD_ENOENT| KD_ENOMEM | KD_ENOSPC);
        return -1;
    }
    return 0;
}

/* kdRmdir: Delete a directory. */
KD_API KDint KD_APIENTRY kdRmdir(const KDchar *pathname)
{
    KDint retval = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    retval = RemoveDirectory(pathname);
    if(retval == 0)
    {
        error = GetLastError();
#else
    retval = rmdir(pathname);
    if(retval == -1)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EACCES | KD_EBUSY | KD_EEXIST | KD_EINVAL | KD_EIO | KD_ENAMETOOLONG | KD_ENOENT| KD_ENOMEM);
        return -1;
    }
    return 0;
}

/* kdRename: Rename a file. */
KD_API KDint KD_APIENTRY kdRename(const KDchar *src, const KDchar *dest)
{
    KDint retval = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    retval = MoveFile(src, dest);
    if(retval == 0)
    {
        error = GetLastError();
#else
    retval = rename(src, dest);
    if(retval == -1)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EACCES | KD_EBUSY | KD_EEXIST | KD_EINVAL | KD_EIO | KD_ENAMETOOLONG | KD_ENOENT| KD_ENOMEM);
        return -1;
    }
    return 0;
}

/* kdRemove: Delete a file. */
KD_API KDint KD_APIENTRY kdRemove(const KDchar *pathname)
{
    KDint retval = 0;
    KDPlatformErrorVEN error = 0;
#if defined(_WIN32)
    retval = DeleteFile(pathname);
    if(retval == 0)
    {
        error = GetLastError();
#else
    retval = remove(pathname);
    if(retval == -1)
    {
        error = errno;
#endif
        kdSetErrorPlatformVEN(error, KD_EACCES | KD_EBUSY | KD_EIO | KD_ENAMETOOLONG | KD_ENOENT| KD_ENOMEM);
        return -1;
    }
    return 0;
}

/* kdTruncate: Truncate or extend a file. */
KD_API KDint KD_APIENTRY kdTruncate(KD_UNUSED const KDchar *pathname, KD_UNUSED KDoff length)
{
#if defined(_WIN32)
    WIN32_FIND_DATA data;
    HANDLE file = FindFirstFile(pathname, &data);
    BOOL error = SetFileValidData(file, (LONGLONG)length);
    FindClose(file);
    if(error == 0)
    {
        return -1;
    }
    else
    {
        return 0;
    }
#else
    return truncate(pathname, length);
#endif
}

/* kdStat, kdFstat: Return information about a file. */
KD_API KDint KD_APIENTRY kdStat(const KDchar *pathname, struct KDStat *buf)
{
    KDint retval = -1;
#if defined(_WIN32)
    retval = 0;
    WIN32_FIND_DATA data;
    FindFirstFile(pathname, &data);
    if(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        buf->st_mode = 0x4000;
    }
    else if(data.dwFileAttributes & FILE_ATTRIBUTE_NORMAL)
    {
        buf->st_mode = 0x8000;
    }
    else
    {
        kdAssert(0);
    }
    LARGE_INTEGER size;
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    buf->st_size = size.QuadPart;

    ULARGE_INTEGER time;
    time.LowPart = data.ftLastWriteTime.dwLowDateTime;
    time.HighPart = data.ftLastWriteTime.dwHighDateTime;
    /* See RtlTimeToSecondsSince1970 */
    buf->st_mtime = (KDtime)((time.QuadPart / 10000000) - 11644473600LL);
#else
    struct stat posixstat = {0};
    retval = stat(pathname, &posixstat);
    if(posixstat.st_mode & S_IFDIR)
    {
        buf->st_mode = 0x4000;
    }
    else if(posixstat.st_mode & S_IFREG)
    {
        buf->st_mode = 0x8000;
    }
    else
    {
        kdAssert(0);
    }
    buf->st_size = posixstat.st_size;
#if defined(__ANDROID__)
    buf->st_mtime = posixstat.st_mtime;
#elif defined(__APPLE__)
    buf->st_mtime = posixstat.st_mtimespec.tv_sec;
#else
    buf->st_mtime = posixstat.st_mtim.tv_sec;
#endif
#endif
    return retval;
}

KD_API KDint KD_APIENTRY kdFstat(KDFile *file, struct KDStat *buf)
{
    return kdStat(file->pathname, buf);
}

/* kdAccess: Determine whether the application can access a file or directory. */
KD_API KDint KD_APIENTRY kdAccess(const KDchar *pathname, KDint amode)
{
    KDint retval = -1;
#if defined(_WIN32)
    WIN32_FIND_DATA data;
    HANDLE error = FindFirstFile(pathname, &data);
    if(error != INVALID_HANDLE_VALUE)
    {
        if(data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
        {
            if(amode & KD_X_OK || amode & KD_R_OK)
            {
                return 0;
            }
        }
        else
        {
            return 0;
        }
    }
#else
    typedef struct __KDAccessMode {
        KDint accessmode_kd;
        KDint accessmode_posix;
    } __KDAccessMode;
    __KDAccessMode accessmodes[] = {{KD_R_OK, R_OK}, {KD_W_OK, W_OK}, {KD_X_OK, X_OK}};
    KDint accessmode = 0;
    for(KDuint i = 0; i < sizeof(accessmodes) / sizeof(accessmodes[0]); i++)
    {
        if(accessmodes[i].accessmode_kd & amode)
        {
            accessmode |= accessmodes[i].accessmode_posix;
        }
    }
    retval = access(pathname, accessmode);
#endif
    return retval;
}

/* kdOpenDir: Open a directory ready for listing. */
struct KDDir {
#if defined(_WIN32)
    HANDLE dir;
#else
    DIR *dir;
#endif
};
KD_API KDDir *KD_APIENTRY kdOpenDir(const KDchar *pathname)
{
    KDDir *dir = (KDDir *)kdMalloc(sizeof(KDDir));
#if defined(_WIN32)
    WIN32_FIND_DATA data;
    dir->dir = FindFirstFile(pathname, &data);
#else
    dir->dir = opendir(pathname);
#endif
    return dir;
}

/* kdReadDir: Return the next file in a directory. */
KD_API KDDirent *KD_APIENTRY kdReadDir(KDDir *dir)
{
    KDDirent *lastdirent = kdThreadSelf()->lastdirent;
#if defined(_WIN32)
    WIN32_FIND_DATA data;
    FindNextFile(dir->dir, &data);
    lastdirent->d_name = data.cFileName;
#else
    struct dirent *posixdirent = readdir(dir->dir);
    lastdirent->d_name = posixdirent->d_name;
#endif
    return lastdirent;
}

/* kdCloseDir: Close a directory. */
KD_API KDint KD_APIENTRY kdCloseDir(KDDir *dir)
{
#if defined(_WIN32)
    FindClose(dir->dir);
#else
    closedir(dir->dir);
#endif
    kdFree(dir);
    return 0;
}

/* kdGetFree: Get free space on a drive. */
KD_API KDoff KD_APIENTRY kdGetFree(const KDchar *pathname)
{
    const KDchar *temp = pathname;
#if defined(_WIN32)
    KDuint64 freespace = 0;
    GetDiskFreeSpaceEx(temp, (PULARGE_INTEGER)&freespace, KD_NULL, KD_NULL);
    return freespace;
#else
    struct statfs buf = {0};
    statfs(temp, &buf);
    return (buf.f_bsize / 1024L) * buf.f_bavail;
#endif
}

/******************************************************************************
 * Network sockets
 ******************************************************************************/

/* kdNameLookup: Look up a hostname. */
KD_API KDint KD_APIENTRY kdNameLookup(KD_UNUSED KDint af, KD_UNUSED const KDchar *hostname, KD_UNUSED void *eventuserptr)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdNameLookupCancel: Selectively cancels ongoing kdNameLookup operations. */
KD_API void KD_APIENTRY kdNameLookupCancel(KD_UNUSED void *eventuserptr)
{
    kdSetError(KD_EOPNOTSUPP);
}

/* kdSocketCreate: Creates a socket. */
struct KDSocket {
    KDint placebo;
};
KD_API KDSocket *KD_APIENTRY kdSocketCreate(KD_UNUSED KDint type, KD_UNUSED void *eventuserptr)
{
    kdSetError(KD_EOPNOTSUPP);
    return KD_NULL;
}

/* kdSocketClose: Closes a socket. */
KD_API KDint KD_APIENTRY kdSocketClose(KD_UNUSED KDSocket *socket)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdSocketBind: Bind a socket. */
KD_API KDint KD_APIENTRY kdSocketBind(KD_UNUSED KDSocket *socket, KD_UNUSED const struct KDSockaddr *addr, KD_UNUSED KDboolean reuse)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdSocketGetName: Get the local address of a socket. */
KD_API KDint KD_APIENTRY kdSocketGetName(KD_UNUSED KDSocket *socket, KD_UNUSED struct KDSockaddr *addr)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdSocketConnect: Connects a socket. */
KD_API KDint KD_APIENTRY kdSocketConnect(KD_UNUSED KDSocket *socket, KD_UNUSED const KDSockaddr *addr)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdSocketListen: Listen on a socket. */
KD_API KDint KD_APIENTRY kdSocketListen(KD_UNUSED KDSocket *socket, KD_UNUSED KDint backlog)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdSocketAccept: Accept an incoming connection. */
KD_API KDSocket *KD_APIENTRY kdSocketAccept(KD_UNUSED KDSocket *socket, KD_UNUSED KDSockaddr *addr, KD_UNUSED void *eventuserptr)
{
    kdSetError(KD_EOPNOTSUPP);
    return KD_NULL;
}

/* kdSocketSend, kdSocketSendTo: Send data to a socket. */
KD_API KDint KD_APIENTRY kdSocketSend(KD_UNUSED KDSocket *socket, KD_UNUSED const void *buf, KD_UNUSED KDint len)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

KD_API KDint KD_APIENTRY kdSocketSendTo(KD_UNUSED KDSocket *socket, KD_UNUSED const void *buf, KD_UNUSED KDint len, KD_UNUSED const KDSockaddr *addr)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdSocketRecv, kdSocketRecvFrom: Receive data from a socket. */
KD_API KDint KD_APIENTRY kdSocketRecv(KD_UNUSED KDSocket *socket, KD_UNUSED void *buf, KD_UNUSED KDint len)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

KD_API KDint KD_APIENTRY kdSocketRecvFrom(KD_UNUSED KDSocket *socket, KD_UNUSED void *buf, KD_UNUSED KDint len, KD_UNUSED KDSockaddr *addr)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdHtonl: Convert a 32-bit integer from host to network byte order. */
KD_API KDuint32 KD_APIENTRY kdHtonl(KD_UNUSED KDuint32 hostlong)
{
    kdSetError(KD_EOPNOTSUPP);
    return 0;
}

/* kdHtons: Convert a 16-bit integer from host to network byte order. */
KD_API KDuint16 KD_APIENTRY kdHtons(KD_UNUSED KDuint16 hostshort)
{
    kdSetError(KD_EOPNOTSUPP);
    return (KDuint16)0;
}

/* kdNtohl: Convert a 32-bit integer from network to host byte order. */
KD_API KDuint32 KD_APIENTRY kdNtohl(KD_UNUSED KDuint32 netlong)
{
    kdSetError(KD_EOPNOTSUPP);
    return 0;
}

/* kdNtohs: Convert a 16-bit integer from network to host byte order. */
KD_API KDuint16 KD_APIENTRY kdNtohs(KD_UNUSED KDuint16 netshort)
{
    kdSetError(KD_EOPNOTSUPP);
    return (KDuint16)0;
}

/* kdInetAton: Convert a &#8220;dotted quad&#8221; format address to an integer. */
KD_API KDint KD_APIENTRY kdInetAton(KD_UNUSED const KDchar *cp, KD_UNUSED KDuint32 *inp)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdInetNtop: Convert a network address to textual form. */
KD_API const KDchar *KD_APIENTRY kdInetNtop(KD_UNUSED KDuint af, KD_UNUSED const void *src, KD_UNUSED KDchar *dst, KD_UNUSED KDsize cnt)
{
    kdSetError(KD_EOPNOTSUPP);
    return KD_NULL;
}

/******************************************************************************
 * Input/output
 ******************************************************************************/
/* kdStateGeti, kdStateGetl, kdStateGetf: get state value(s) */
KD_API KDint KD_APIENTRY kdStateGeti(KD_UNUSED KDint startidx, KD_UNUSED KDuint numidxs, KD_UNUSED KDint32 *buffer)
{
    kdSetError(KD_EIO);
    return -1;
}

KD_API KDint KD_APIENTRY kdStateGetl(KD_UNUSED KDint startidx, KD_UNUSED KDuint numidxs, KD_UNUSED KDint64 *buffer)
{
    kdSetError(KD_EIO);
    return -1;
}

KD_API KDint KD_APIENTRY kdStateGetf(KD_UNUSED KDint startidx, KD_UNUSED KDuint numidxs, KD_UNUSED KDfloat32 *buffer)
{
    kdSetError(KD_EIO);
    return -1;
}


/* kdOutputSeti, kdOutputSetf: set outputs */
KD_API KDint KD_APIENTRY kdOutputSeti(KD_UNUSED KDint startidx, KD_UNUSED KDuint numidxs, KD_UNUSED const KDint32 *buffer)
{
    kdSetError(KD_EIO);
    return -1;
}

KD_API KDint KD_APIENTRY kdOutputSetf(KD_UNUSED KDint startidx, KD_UNUSED KDuint numidxs, KD_UNUSED const KDfloat32 *buffer)
{
    kdSetError(KD_EIO);
    return -1;
}

/******************************************************************************
 * Windowing
 ******************************************************************************/
#ifdef KD_WINDOW_SUPPORTED
/* kdCreateWindow: Create a window. */
#if defined(KD_WINDOW_WIN32)
LRESULT CALLBACK windowcallback(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch(msg)
    {
        case WM_CLOSE:
        case WM_DESTROY:
        case WM_QUIT:
        {
            PostQuitMessage(0);
            break;
        }
        default:
        {
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }
    }
    return 0;
}
#endif
KD_API KDWindow *KD_APIENTRY kdCreateWindow(KD_UNUSED EGLDisplay display, KD_UNUSED EGLConfig config, KD_UNUSED void *eventuserptr)
{
    if(__kd_window != KD_NULL)
    {
        /* One window only */
        kdSetError(KD_EPERM);
        return KD_NULL;
    }

    KDWindow *window = (KDWindow *)kdMalloc(sizeof(KDWindow));
    if(window == KD_NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    if(eventuserptr == KD_NULL)
    {
        window->eventuserptr = window;
    }
    else
    {
        window->eventuserptr = eventuserptr;
    }
    window->originthr = kdThreadSelf();
#if defined(KD_WINDOW_ANDROID)
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &window->format);
#elif defined(KD_WINDOW_WIN32)
    WNDCLASS windowclass = {0};
    HINSTANCE instance = GetModuleHandle(KD_NULL);
    GetClassInfo(instance, "", &windowclass);
    windowclass.lpszClassName = "OpenKODE";
    windowclass.lpfnWndProc = windowcallback;
    windowclass.hInstance = instance;
    windowclass.hbrBackground = (HBRUSH)COLOR_BACKGROUND;
    RegisterClass(&windowclass);
    window->nativewindow = CreateWindow("OpenKODE", "OpenKODE", WS_POPUP | WS_VISIBLE, 0, 0,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        KD_NULL, KD_NULL, instance, KD_NULL);
    /* Activate raw input */
    RAWINPUTDEVICE device[2];
    /* Mouse */
    device[0].usUsagePage = 1;
    device[0].usUsage = 2;
    device[0].dwFlags = RIDEV_NOLEGACY;
    device[0].hwndTarget = window->nativewindow;
    /* Keyboard */
    device[1].usUsagePage = 1;
    device[1].usUsage = 6;
    device[1].dwFlags = RIDEV_NOLEGACY;
    device[1].hwndTarget = window->nativewindow;
    RegisterRawInputDevices(device, 2, sizeof(RAWINPUTDEVICE));
#elif defined(KD_WINDOW_X11)
    XInitThreads();
    window->nativedisplay = XOpenDisplay(NULL);
    window->nativewindow = XCreateSimpleWindow(window->nativedisplay,
        XRootWindow(window->nativedisplay, XDefaultScreen(window->nativedisplay)), 0, 0,
        (KDuint)XWidthOfScreen(XDefaultScreenOfDisplay(window->nativedisplay)),
        (KDuint)XHeightOfScreen(XDefaultScreenOfDisplay(window->nativedisplay)), 0,
        XBlackPixel(window->nativedisplay, XDefaultScreen(window->nativedisplay)),
        XWhitePixel(window->nativedisplay, XDefaultScreen(window->nativedisplay)));
    XStoreName(window->nativedisplay, window->nativewindow, "OpenKODE");
    Atom wm_del_win_msg = XInternAtom(window->nativedisplay, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(window->nativedisplay, window->nativewindow, &wm_del_win_msg, 1);
    Atom mwm_prop_hints = XInternAtom(window->nativedisplay, "_MOTIF_WM_HINTS", True);
    const KDuint8 mwm_hints[5] = {2, 0, 0, 0, 0};
    XChangeProperty(window->nativedisplay, window->nativewindow, mwm_prop_hints, mwm_prop_hints, 32, 0, (const KDuint8 *)&mwm_hints, 5);
    Atom netwm_prop_hints = XInternAtom(window->nativedisplay, "_NET_WM_STATE", False);
    Atom netwm_hints[3];
    netwm_hints[0] = XInternAtom(window->nativedisplay, "_NET_WM_STATE_FULLSCREEN", False);
    netwm_hints[1] = XInternAtom(window->nativedisplay, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    netwm_hints[2] = XInternAtom(window->nativedisplay, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    netwm_hints[2] = XInternAtom(window->nativedisplay, "_NET_WM_STATE_FOCUSED", False);
    XChangeProperty(window->nativedisplay, window->nativewindow, netwm_prop_hints, 4, 32, 0, (const KDuint8 *)&netwm_hints, 3);
#endif
    __kd_window = window;
    return window;
}

/* kdDestroyWindow: Destroy a window. */
KD_API KDint KD_APIENTRY kdDestroyWindow(KDWindow *window)
{
    if(window->originthr != kdThreadSelf())
    {
        kdSetError(KD_EINVAL);
        return -1;
    }
#if defined(KD_WINDOW_WIN32)
    DestroyWindow(window->nativewindow);
#elif defined(KD_WINDOW_X11)
    XCloseDisplay(window->nativedisplay);
#endif
    kdFree(window);
    __kd_window = KD_NULL;
    return 0;
}

/* kdSetWindowPropertybv, kdSetWindowPropertyiv, kdSetWindowPropertycv: Set a window property to request a change in the on-screen representation of the window. */
KD_API KDint KD_APIENTRY kdSetWindowPropertybv(KD_UNUSED KDWindow *window, KD_UNUSED KDint pname, KD_UNUSED const KDboolean *param)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}
KD_API KDint KD_APIENTRY kdSetWindowPropertyiv(KD_UNUSED KDWindow *window, KDint pname, KD_UNUSED const KDint32 *param)
{
    if(pname == KD_WINDOWPROPERTY_SIZE)
    {
#if defined(KD_WINDOW_X11)
        XMoveResizeWindow(window->nativedisplay, window->nativewindow, 0, 0, (KDuint)param[0], (KDuint)param[1]);
        XFlush(window->nativedisplay);
        KDEvent *event = kdCreateEvent();
        event->type = KD_EVENT_WINDOWPROPERTY_CHANGE;
        kdPostThreadEvent(event, kdThreadSelf());
        return 0;
#endif
    }
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}
KD_API KDint KD_APIENTRY kdSetWindowPropertycv(KD_UNUSED KDWindow *window, KDint pname, KD_UNUSED const KDchar *param)
{
    if(pname == KD_WINDOWPROPERTY_CAPTION)
    {
#if defined(KD_WINDOW_X11)
        XStoreName(window->nativedisplay, window->nativewindow, param);
        XFlush(window->nativedisplay);
        KDEvent *event = kdCreateEvent();
        event->type = KD_EVENT_WINDOWPROPERTY_CHANGE;
        kdPostThreadEvent(event, kdThreadSelf());
        return 0;
#endif
    }
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdGetWindowPropertybv, kdGetWindowPropertyiv, kdGetWindowPropertycv: Get the current value of a window property. */
KD_API KDint KD_APIENTRY kdGetWindowPropertybv(KD_UNUSED KDWindow *window, KD_UNUSED KDint pname, KD_UNUSED KDboolean *param)
{
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}
KD_API KDint KD_APIENTRY kdGetWindowPropertyiv(KD_UNUSED KDWindow *window, KDint pname, KD_UNUSED KDint32 *param)
{
    if(pname == KD_WINDOWPROPERTY_SIZE)
    {
#if defined(KD_WINDOW_ANDROID)
        param[0] = ANativeWindow_getWidth(window->nativewindow);
        param[1] = ANativeWindow_getHeight(window->nativewindow);
        return 0;
#elif defined(KD_WINDOW_X11)
        param[0] = XWidthOfScreen(XDefaultScreenOfDisplay(window->nativedisplay));
        param[1] = XHeightOfScreen(XDefaultScreenOfDisplay(window->nativedisplay));
        return 0;
#endif
    }
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}
KD_API KDint KD_APIENTRY kdGetWindowPropertycv(KD_UNUSED KDWindow *window, KDint pname, KD_UNUSED KDchar *param, KD_UNUSED KDsize *size)
{
    if(pname == KD_WINDOWPROPERTY_CAPTION)
    {
#if defined(KD_WINDOW_X11)
        XFetchName(window->nativedisplay, window->nativewindow, &param);
        return 0;
#endif
    }
    kdSetError(KD_EOPNOTSUPP);
    return -1;
}

/* kdRealizeWindow: Realize the window as a displayable entity and get the native window handle for passing to EGL. */
KD_API KDint KD_APIENTRY kdRealizeWindow(KDWindow *window, EGLNativeWindowType *nativewindow)
{
#if defined(KD_WINDOW_ANDROID)
    for(;;)
    {
        kdThreadMutexLock(__kd_androidwindow_mutex);
        if(__kd_androidwindow != KD_NULL)
        {
            window->nativewindow = __kd_androidwindow;
            kdThreadMutexUnlock(__kd_androidwindow_mutex);
            break;
        }
        kdThreadMutexUnlock(__kd_androidwindow_mutex);
    }
    ANativeWindow_setBuffersGeometry(window->nativewindow, 0, 0, window->format);
#elif defined(KD_WINDOW_X11)
    XMapWindow(window->nativedisplay, window->nativewindow);
    XFlush(window->nativedisplay);
#endif
    *nativewindow = window->nativewindow;
    return 0;
}
#endif

/******************************************************************************
 * Assertions and logging
 ******************************************************************************/

/* kdHandleAssertion: Handle assertion failure. */
KD_API void KD_APIENTRY kdHandleAssertion(const KDchar *condition, const KDchar *filename, KDint linenumber)
{
#define messagelimit 4096
    KDchar message[messagelimit] = "";
    KDchar line[128] = "";
    kdLtostr(line, 128, linenumber);
    kdStrncat_s(message, messagelimit, "---Assertion---\n", messagelimit);
    kdStrncat_s(message, messagelimit, "Condition: ", messagelimit);
    kdStrncat_s(message, messagelimit, condition, messagelimit);
    kdStrncat_s(message, messagelimit, "\n", messagelimit);
    kdStrncat_s(message, messagelimit, "File: ", messagelimit);
    kdStrncat_s(message, messagelimit, filename, messagelimit);
    kdStrncat_s(message, messagelimit, "(", messagelimit);
    kdStrncat_s(message, messagelimit, line, messagelimit);
    kdStrncat_s(message, messagelimit, ")", messagelimit);
    kdLogMessage(message);
#undef messagelimit
    kdExit(EXIT_FAILURE);
}

/* kdLogMessage: Output a log message. */
#ifndef KD_NDEBUG
KD_API void KD_APIENTRY kdLogMessage(const KDchar *string)
{
    KDsize stringsize = kdStrlen(string) + 2;
    KDchar *newstring = kdMalloc(sizeof(KDchar) * stringsize);
    kdMemset(newstring, 0, stringsize);
    kdStrncat_s(newstring, stringsize, string, stringsize);
    if(newstring[(stringsize - 3)] != '\n')
    {
        kdStrncat_s(newstring, stringsize, "\n", stringsize);
    }
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_INFO, __kdAppName(KD_NULL), newstring);
#elif defined(__linux__) && !defined(__TINYC__)
    syscall(SYS_write, 1, newstring, kdStrlen(newstring));
#elif defined(_WIN32)
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), newstring, (DWORD)kdStrlen(newstring), (DWORD[]){0}, NULL);
#else
    printf("%s", newstring);
    fflush(stdout);
#endif
    kdFree(newstring);
}
#endif

/******************************************************************************
 * Extensions
 ******************************************************************************/

/******************************************************************************
 * Atomics
 ******************************************************************************/

#if defined(KD_ATOMIC_C11)
struct KDAtomicIntVEN {
    atomic_int value;
};
struct KDAtomicPtrVEN {
    atomic_uintptr_t value;
};
#elif defined(KD_ATOMIC_WIN32) || defined(KD_ATOMIC_BUILTIN) || defined(KD_ATOMIC_SYNC) || defined(KD_ATOMIC_MUTEX)
struct KDAtomicIntVEN {
    KDint value;
#if defined(KD_ATOMIC_MUTEX)
    KDThreadMutex *mutex;
#endif
};
struct KDAtomicPtrVEN {
    void *value;
#if defined(KD_ATOMIC_MUTEX)
    KDThreadMutex *mutex;
#endif
};
#endif

KD_API KDAtomicIntVEN *KD_APIENTRY kdAtomicIntCreateVEN(KDint value)
{
    KDAtomicIntVEN *object = (KDAtomicIntVEN *)kdMalloc(sizeof(KDAtomicIntVEN));
#if defined(KD_ATOMIC_C11)
    atomic_init(&object->value, value);
#elif defined(KD_ATOMIC_WIN32) || defined(KD_ATOMIC_BUILTIN) || defined(KD_ATOMIC_SYNC) || defined(KD_ATOMIC_MUTEX)
    object->value = value;
#if defined(KD_ATOMIC_MUTEX)
    object->mutex = kdThreadMutexCreate(KD_NULL);
#endif
#endif
    return object;
}

KD_API KDAtomicPtrVEN *KD_APIENTRY kdAtomicPtrCreateVEN(void *value)
{
    KDAtomicPtrVEN *object = (KDAtomicPtrVEN *)kdMalloc(sizeof(KDAtomicPtrVEN));
#if defined(KD_ATOMIC_C11)
    atomic_init(&object->value, (KDuintptr)value);
#elif defined(KD_ATOMIC_WIN32) || defined(KD_ATOMIC_BUILTIN) || defined(KD_ATOMIC_SYNC) || defined(KD_ATOMIC_MUTEX)
    object->value = value;
#if defined(KD_ATOMIC_MUTEX)
    object->mutex = kdThreadMutexCreate(KD_NULL);
#endif
#endif
    return object;
}

KD_API KDint KD_APIENTRY kdAtomicIntFreeVEN(KDAtomicIntVEN *object)
{
#if defined(KD_ATOMIC_MUTEX)
    kdThreadMutexFree(object->mutex);
#endif
    kdFree(object);
    return 0;
}

KD_API KDint KD_APIENTRY kdAtomicPtrFreeVEN(KDAtomicPtrVEN *object)
{
#if defined(KD_ATOMIC_MUTEX)
    kdThreadMutexFree(object->mutex);
#endif
    kdFree(object);
    return 0;
}

KD_API KDint KD_APIENTRY kdAtomicIntLoadVEN(KDAtomicIntVEN *object)
{
#if defined(KD_ATOMIC_C11)
    return atomic_load(&object->value);
#elif defined(KD_ATOMIC_WIN32) || defined(KD_ATOMIC_SYNC) || defined(KD_ATOMIC_MUTEX)
    KDint value = 0;
    do
    {
        value = object->value;
    } while(!kdAtomicIntCompareExchangeVEN(object, value, value));
    return value;
#elif defined(KD_ATOMIC_BUILTIN)
    return __atomic_load_n(&object->value, __ATOMIC_SEQ_CST);
#endif
}

KD_API void *KD_APIENTRY kdAtomicPtrLoadVEN(KDAtomicPtrVEN *object)
{
#if defined(KD_ATOMIC_C11)
    return (void *)atomic_load(&object->value);
#elif defined(KD_ATOMIC_WIN32) || defined(KD_ATOMIC_SYNC) || defined(KD_ATOMIC_MUTEX)
    void *value = 0;
    do
    {
        value = object->value;
    } while(!kdAtomicPtrCompareExchangeVEN(object, value, value));
    return value;
#elif defined(KD_ATOMIC_BUILTIN)
    return __atomic_load_n(&object->value, __ATOMIC_SEQ_CST);
#endif
}

KD_API void KD_APIENTRY kdAtomicIntStoreVEN(KDAtomicIntVEN *object, KDint value)
{
#if defined(KD_ATOMIC_C11)
    atomic_store(&object->value, value);
#elif defined(KD_ATOMIC_WIN32)
    _InterlockedExchange((long *)&object->value, (long)value);
#elif defined(KD_ATOMIC_BUILTIN)
    __atomic_store_n(&object->value, value, __ATOMIC_SEQ_CST);
#elif defined(KD_ATOMIC_SYNC)
    __sync_lock_test_and_set(&object->value, value);
#elif defined(KD_ATOMIC_MUTEX)
    kdThreadMutexLock(object->mutex);
    object->value = value;
    kdThreadMutexUnlock(object->mutex);
#endif
}

KD_API void KD_APIENTRY kdAtomicPtrStoreVEN(KDAtomicPtrVEN *object, void *value)
{
#if defined(KD_ATOMIC_C11)
    atomic_store(&object->value, (KDuintptr)value);
#elif defined(KD_ATOMIC_WIN32) && defined(_M_IX86)
    _InterlockedExchange((long *)&object->value, (long)value);
#elif defined(KD_ATOMIC_WIN32)
    _InterlockedExchangePointer(&object->value, value);
#elif defined(KD_ATOMIC_BUILTIN)
    __atomic_store_n(&object->value, value, __ATOMIC_SEQ_CST);
#elif defined(KD_ATOMIC_SYNC)
    KD_UNUSED void *unused = __sync_lock_test_and_set(&object->value, value);
#elif defined(KD_ATOMIC_MUTEX)
    kdThreadMutexLock(object->mutex);
    object->value = value;
    kdThreadMutexUnlock(object->mutex);
#endif
}

KD_API KDint KD_APIENTRY kdAtomicIntFetchAddVEN(KDAtomicIntVEN *object, KDint value)
{
#if defined(KD_ATOMIC_C11)
    return atomic_fetch_add(&object->value, value);
#elif defined(KD_ATOMIC_WIN32)
    return _InterlockedExchangeAdd((long *)&object->value, (long)value);
#elif defined(KD_ATOMIC_BUILTIN)
    return __atomic_add_fetch(&object->value, value, __ATOMIC_SEQ_CST);
#elif defined(KD_ATOMIC_SYNC)
    return __sync_fetch_and_add(&object->value, value);
#elif defined(KD_ATOMIC_MUTEX)
    kdThreadMutexLock(object->mutex);
    KDint retval = object->value;
    object->value = object->value + value;
    kdThreadMutexUnlock(object->mutex);
    return retval;
#endif
}

KD_API KDint KD_APIENTRY kdAtomicIntFetchSubVEN(KDAtomicIntVEN *object, KDint value)
{
#if defined(KD_ATOMIC_C11)
    return atomic_fetch_sub(&object->value, value);
#elif defined(KD_ATOMIC_WIN32)
    return _InterlockedExchangeAdd((long *)&object->value, (long)-value);
#elif defined(KD_ATOMIC_BUILTIN)
    return __atomic_sub_fetch(&object->value, value, __ATOMIC_SEQ_CST);
#elif defined(KD_ATOMIC_SYNC)
    return __sync_fetch_and_sub(&object->value, value);
#elif defined(KD_ATOMIC_MUTEX)
    KDint retval = 0;
    kdThreadMutexLock(object->mutex);
    retval = object->value;
    object->value = object->value - value;
    kdThreadMutexUnlock(object->mutex);
    return retval;
#endif
}

KD_API KDboolean KD_APIENTRY kdAtomicIntCompareExchangeVEN(KDAtomicIntVEN *object, KDint expected, KDint desired)
{
#if defined(KD_ATOMIC_C11)
    return atomic_compare_exchange_weak(&object->value, &expected, desired);
#elif defined(KD_ATOMIC_WIN32)
    return (_InterlockedCompareExchange((long *)&object->value, (long)desired, (long)expected) == (long)expected);
#elif defined(KD_ATOMIC_BUILTIN)
    return __atomic_compare_exchange_n(&object->value, &expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#elif defined(KD_ATOMIC_SYNC)
    return __sync_bool_compare_and_swap(&object->value, expected, desired);
#elif defined(KD_ATOMIC_MUTEX)
    KDboolean retval = 0;
    kdThreadMutexLock(object->mutex);
    if(object->value == expected)
    {
        object->value = desired;
        retval = 1;
    }
    kdThreadMutexUnlock(object->mutex);
    return retval;
#endif
}

KD_API KDboolean KD_APIENTRY kdAtomicPtrCompareExchangeVEN(KDAtomicPtrVEN *object, void *expected, void *desired)
{
#if defined(KD_ATOMIC_C11)
    return atomic_compare_exchange_weak(&object->value, (KDuintptr *)&expected, (KDuintptr)desired);
#elif defined(KD_ATOMIC_WIN32) && defined(_M_IX86)
    return (_InterlockedCompareExchange((long *)&object->value, (long)desired, (long)expected) == (long)expected);
#elif defined(KD_ATOMIC_WIN32)
    return (_InterlockedCompareExchangePointer(&object->value, desired, expected) == expected);
#elif defined(KD_ATOMIC_BUILTIN)
    return __atomic_compare_exchange_n(&object->value, &expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#elif defined(KD_ATOMIC_SYNC)
    return __sync_bool_compare_and_swap(&object->value, expected, desired);
#elif defined(KD_ATOMIC_MUTEX)
    KDboolean retval = 0;
    kdThreadMutexLock(object->mutex);
    if(object->value == expected)
    {
        object->value = desired;
        retval = 1;
    }
    kdThreadMutexUnlock(object->mutex);
    return retval;
#endif
}

/******************************************************************************
 * Queue (threadsafe)
 ******************************************************************************/

typedef struct __KDQueueNode __KDQueueNode;
struct __KDQueueNode {
    __KDQueueNode *next;
    __KDQueueNode *prev;
    void *value;
};

struct KDQueueVEN {
    KDThreadMutex *mutex;
    __KDQueueNode *head;
    __KDQueueNode *tail;
    KDsize size;
};

KD_API KDQueueVEN *KD_APIENTRY kdQueueCreateVEN(KD_UNUSED KDsize maxsize)
{
    KDQueueVEN *queue = (KDQueueVEN *)kdMalloc(sizeof(KDQueueVEN));
    queue->mutex = kdThreadMutexCreate(KD_NULL);
    queue->head = KD_NULL;
    queue->tail = KD_NULL;
    queue->size = 0;
    return queue;
}

KD_API KDint KD_APIENTRY kdQueueFreeVEN(KDQueueVEN *queue)
{
    kdThreadMutexFree(queue->mutex);
    kdFree(queue);
    return 0;
}

KD_API KDsize KD_APIENTRY kdQueueSizeVEN(KDQueueVEN *queue)
{
    kdThreadMutexLock(queue->mutex);
    KDsize size = queue->size;
    kdThreadMutexUnlock(queue->mutex);
    return size;
}

KD_API void KD_APIENTRY kdQueuePushHeadVEN(KDQueueVEN *queue, void *value)
{
    __KDQueueNode *node = (__KDQueueNode *)kdMalloc(sizeof(__KDQueueNode));
    node->value = value;
    node->prev = KD_NULL;
    node->next = KD_NULL;

    kdThreadMutexLock(queue->mutex);
    if((node->next = queue->head) != KD_NULL)
    {
        node->next->prev = node;
    }
    queue->head = node;
    if(!queue->tail)
    {
        queue->tail = node;
    }
    queue->size++;
    kdThreadMutexUnlock(queue->mutex);
}

KD_API void KD_APIENTRY kdQueuePushTailVEN(KDQueueVEN *queue, void *value)
{
    __KDQueueNode *node = (__KDQueueNode *)kdMalloc(sizeof(__KDQueueNode));
    node->value = value;
    node->prev = KD_NULL;
    node->next = KD_NULL;

    kdThreadMutexLock(queue->mutex);
    node->prev = queue->tail;
    if(node->prev != KD_NULL)
    {
        queue->tail->next = node;
    }
    queue->tail = node;
    if(!queue->head)
    {
        queue->head = node;
    }
    queue->size++;
    kdThreadMutexUnlock(queue->mutex);
}

KD_API void *KD_APIENTRY kdQueuePopHeadVEN(KDQueueVEN *queue)
{
    __KDQueueNode *node = KD_NULL;
    void *value = KD_NULL;

    kdThreadMutexLock(queue->mutex);
    if(queue->head)
    {
        node = queue->head;
        queue->head = node->next;
        if(queue->head != KD_NULL)
        {
            queue->head->prev = KD_NULL;
        }
        if(queue->tail == node)
        {
            queue->tail = KD_NULL;
        }
        queue->size--;
    }
    kdThreadMutexUnlock(queue->mutex);

    if(node)
    {
        value = node->value;
        kdFree(node);
    }

    return value;
}

KD_API void *KD_APIENTRY kdQueuePopTailVEN(KDQueueVEN *queue)
{
    __KDQueueNode *node = KD_NULL;
    void *value = KD_NULL;

    kdThreadMutexLock(queue->mutex);
    if(queue->head)
    {
        node = queue->tail;
        queue->tail = node->prev;
        if(queue->tail != KD_NULL)
        {
            queue->tail->next = KD_NULL;
        }
        if(queue->head == node)
        {
            queue->head = KD_NULL;
        }
        queue->size--;
    }
    kdThreadMutexUnlock(queue->mutex);

    if(node)
    {
        value = node->value;
        kdFree(node);
    }

    return value;
}
