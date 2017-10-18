/******************************************************************************
 * libKD
 * zlib/libpng License
 ******************************************************************************
 * Copyright (c) 2014-2017 Kevin Schmidt
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

struct KDFile {
#if defined(_WIN32)
    HANDLE nativefile;
#else
    KDint nativefile;
    KDint8 padding[4];
#endif
    const KDchar *pathname;
    KDboolean eof;
    KDboolean error;
};

typedef struct _KDQueue _KDQueue;
typedef struct _KDCallback _KDCallback;
typedef struct _KDThreadInternal _KDThreadInternal;
struct KDThread {
    _KDThreadInternal *internal;
    _KDQueue *eventqueue;
    KDEvent *lastevent;
    KDint lasterror;
    KDint callbackindex;
    _KDCallback **callbacks;
    void *tlsptr;
};

typedef struct _KDImageATX _KDImageATX;
struct _KDImageATX {
    KDuint8 *buffer;
    KDsize size;
    KDint width;
    KDint height;
    KDint levels;
    KDint bpp;
    KDint format;
    KDboolean alpha;
};

KDThread *__kdThreadInit(void);
void __kdThreadInitOnce(void);
void __kdThreadFree(KDThread *thread);

void __kdCleanupThreadStorageKHR(void);

_KDQueue* __kdQueueCreate(KDsize size);
KDint __kdQueueFree(_KDQueue* queue);
KDsize __kdQueueSize(_KDQueue *queue);
KDint __kdQueuePush(_KDQueue *queue, void *value);
void* __kdQueuePull(_KDQueue *queue);

#if !defined(_WIN32)
KDssize __kdWrite(KDint fd, const void *buf, KDsize count);
KDssize __kdRead(KDint fd, const void *buf, KDsize count);
KDint __kdOpen(const KDchar *pathname, KDint flags, KDint mode);
#endif

extern KDThreadOnce __kd_threadinit_once;
extern KDThreadStorageKeyKHR __kd_threadlocal;
extern KDThreadMutex *__kd_tls_mutex;

#if !defined(_WIN32) && defined(KD_FREESTANDING)
extern KDint errno;
#endif
