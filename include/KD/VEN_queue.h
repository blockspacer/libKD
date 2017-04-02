
/*******************************************************
 * OpenKODE Core extension: VEN_queue
 *******************************************************/

#ifndef __kd_VEN_queue_h_
#define __kd_VEN_queue_h_
#include <KD/kd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MPMC, FIFO queue */
typedef struct KDQueueVEN KDQueueVEN;

KD_API KDQueueVEN* KD_APIENTRY kdQueueCreateVEN(KDsize size);
KD_API KDint KD_APIENTRY kdQueueFreeVEN(KDQueueVEN* queue);

KD_API KDsize KD_APIENTRY kdQueueSizeVEN(KDQueueVEN *queue);

KD_API KDint KD_APIENTRY kdQueuePushVEN(KDQueueVEN *queue, void *value);
KD_API void* KD_APIENTRY kdQueuePullVEN(KDQueueVEN *queue);

#ifdef __cplusplus
}
#endif

#endif /* __kd_VEN_queue_h_ */