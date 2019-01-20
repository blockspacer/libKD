/******************************************************************************
 * libKD
 * zlib/libpng License
 ******************************************************************************
 * Copyright (c) 2014-2018 Kevin Schmidt
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arikdSinfg from the use of this software.
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
#include <KD/KHR_float64.h>
#include "test.h"

KDint KD_APIENTRY kdMain(KDint argc, const KDchar *const *argv)
{
    TEST_APPROXF(kdSqrtf(0.0f) , 0.0f);
    TEST_APPROXF(kdSqrtf(0.5f) , KD_SQRT1_2_F);
    TEST_APPROXF(kdSqrtf(1.0f) , 1.0f);
    TEST_APPROXF(kdSqrtf(2.0f) , 1.0f / KD_SQRT1_2_F);
    TEST_APPROXF(kdSqrtf(144.0f), 12.0f);

    TEST_APPROX(kdSqrtKHR(0.0) , 0.0);
    TEST_APPROX(kdSqrtKHR(0.5) , KD_SQRT1_2_KHR);
    TEST_APPROX(kdSqrtKHR(1.0) , 1.0);
    TEST_APPROX(kdSqrtKHR(2.0) , 1.0 / KD_SQRT1_2_KHR);
    TEST_APPROX(kdSqrtKHR(144.0), 12.0);
    return 0;
}
