/******************************************************************************
* Copyright (c) 2014 Kevin Schmidt
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

#include <KD/kd.h>
#include <stdlib.h> /* EXIT_FAILURE */

/* Test can communicate properly with event loops in different threads. */
#define THREAD_COUNT 8-1-1 /* POSIX minimum mqueues; -1 for the mainthread; -1 for safety */
void* test_func( void *arg)
{
    for(;;)
    {
        const KDEvent *event = kdWaitEvent(0);
        if(event)
        {
            KDboolean quit = 0;
            if(event->type == KD_EVENT_QUIT)
            {
                quit = 1;
            }

            kdDefaultEvent(event);
            if(quit)
            {
                break;
            }
        }
    }
    return 0;
}

KDint kdMain(KDint argc, const KDchar *const *argv)
{
    KDThread* threads[THREAD_COUNT] = {KD_NULL};
    for(KDint thread = 0 ; thread < THREAD_COUNT ;thread++)
    {
        threads[thread] = kdThreadCreate(KD_NULL, test_func, KD_NULL);
        if(threads[thread] == KD_NULL)
        {
            kdExit(EXIT_FAILURE);
        }
    }
    for(KDint thread = 0 ; thread < THREAD_COUNT ;thread++)
    {
        KDEvent *event = kdCreateEvent();
        event->type      = KD_EVENT_QUIT;
        if(kdPostThreadEvent(event, threads[thread]) == -1)
        {
            kdExit(EXIT_FAILURE);
        }
    }
    for(KDint thread = 0 ; thread < THREAD_COUNT ;thread++)
    {
        if(kdThreadJoin(threads[thread], KD_NULL) == -1)
        {
            kdExit(EXIT_FAILURE);
        }
    }
    return 0;
}