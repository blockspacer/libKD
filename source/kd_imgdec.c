// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

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

/******************************************************************************
 * Header workarounds
 ******************************************************************************/

/* clang-format off */
#if defined(__linux__) || defined(__EMSCRIPTEN__)
#   define _GNU_SOURCE /* O_CLOEXEC */
#endif

/******************************************************************************
 * KD includes
 ******************************************************************************/

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wpadded"
#endif
#include <KD/kd.h>
#include <KD/kdext.h>
#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#include "kd_internal.h"

/******************************************************************************
 * Platform includes
 ******************************************************************************/

#if defined(__unix__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
#   include <unistd.h>
#   include <fcntl.h> /* O_RDONLY etc.*/
#   include <sys/mman.h> /* mmap etc. */
#endif

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#   include <fileapi.h> /* FindFirstFileA etc. */
#endif

/******************************************************************************
 * Thirdparty includes
 ******************************************************************************/

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_STDIO
#define STBI_ASSERT         kdAssert
#define STBI_MALLOC         kdMalloc
#define STBI_REALLOC        kdRealloc
#define STBI_FREE           kdFree
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 6001)
#   pragma warning(disable : 6011)
#elif defined(__TINYC__)  
#   define STBI_NO_SIMD
#elif defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wcast-align"
#   pragma clang diagnostic ignored "-Wcast-qual"
#   pragma clang diagnostic ignored "-Wcomma"
#   pragma clang diagnostic ignored "-Wconversion"
#   pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#   pragma clang diagnostic ignored "-Wdouble-promotion"
#   pragma clang diagnostic ignored "-Wpadded"
#   pragma clang diagnostic ignored "-Wsign-conversion"
#endif
#include "stb_image.h"
#if defined(_MSC_VER)
#   pragma warning(pop)
#elif defined(__clang__)
#   pragma clang diagnostic pop
#endif
/* clang-format on */

/******************************************************************************
 * OpenKODE Core extension: KD_ATX_imgdec
 ******************************************************************************/
/******************************************************************************
 * OpenKODE Core extension: KD_ATX_imgdec_jpeg
 ******************************************************************************/
/******************************************************************************
 * OpenKODE Core extension: KD_ATX_imgdec_png
 ******************************************************************************/

/* kdGetImageInfoATX, kdGetImageInfoFromStreamATX: Construct an informational image object based on an image in a file or stream. */
KD_API KDImageATX KD_APIENTRY kdGetImageInfoATX(const KDchar *pathname)
{
    _KDImageATX *image = (_KDImageATX *)kdMalloc(sizeof(_KDImageATX));
    if(image == KD_NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    image->levels = 0;

    KDStat st;
    if(kdStat(pathname, &st) == -1)
    {
        kdFree(image);
        kdSetError(KD_EIO);
        return KD_NULL;
    }
    image->size = (KDsize)st.st_size;

#if defined(__unix__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
    KDint fd = open(pathname, O_RDONLY | O_CLOEXEC, 0);
    if(fd == -1)
#elif(_WIN32)
    WIN32_FIND_DATA data;
    HANDLE fd = FindFirstFileA(pathname, &data);
    if(fd == INVALID_HANDLE_VALUE)
#endif
    {
        kdFree(image);
        kdSetError(KD_EIO);
        return KD_NULL;
    }

    void *filedata = KD_NULL;
#if defined(__unix__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
    filedata = mmap(KD_NULL, image->size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(filedata == MAP_FAILED)
#elif defined(_WIN32)
    HANDLE fm = CreateFileMapping(fd, KD_NULL, PAGE_READONLY, 0, 0, KD_NULL);
    if(fm)
    {
        filedata = MapViewOfFile(fm, FILE_MAP_READ, 0, 0, image->size);
    }
    if(filedata == KD_NULL)
#endif
    {
#if defined(__unix__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
        close(fd);
#elif defined(_WIN32)
        CloseHandle(fd);
#endif
        kdFree(image);
        kdSetError(KD_EIO);
        return KD_NULL;
    }

    KDint channels = 0;
    KDint error = stbi_info_from_memory(filedata, (KDint)image->size, &image->width, &image->height, &channels);
    switch(channels)
    {
        case(4):
        {
            image->format = KD_IMAGE_FORMAT_RGBA8888_ATX;
            image->alpha = KD_TRUE;
            break;
        }
        case(3):
        {
            image->format = KD_IMAGE_FORMAT_RGB888_ATX;
            image->alpha = KD_FALSE;
            break;
        }
        case(2):
        {
            image->format = KD_IMAGE_FORMAT_LUMALPHA88_ATX;
            image->alpha = KD_TRUE;
            break;
        }
        case(1):
        {
            image->format = KD_IMAGE_FORMAT_LUM8_ATX;
            image->alpha = KD_FALSE;
            break;
        }
        default:
        {
            error = 0;
            break;
        }
    }

#if defined(__unix__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
    munmap(filedata, image->size);
    close(fd);
#elif defined(_WIN32)
    UnmapViewOfFile(filedata);
    CloseHandle(fd);
#endif

    if(error == 0)
    {
        kdFree(image);
        kdSetError(KD_EILSEQ);
        return KD_NULL;
    }

    return image;
}

KD_API KDImageATX KD_APIENTRY kdGetImageInfoFromStreamATX(KDFile *file)
{
    KDImageATX image = kdGetImageInfoATX(file->pathname);
    return image;
}

/* kdGetImageATX, kdGetImageFromStreamATX: Read and decode an image from a file or stream, returning a decoded image object. */
KD_API KDImageATX KD_APIENTRY kdGetImageATX(const KDchar *pathname, KDint format, KDint flags)
{
    KDFile *file = kdFopen(pathname, "rb");
    if(file == KD_NULL)
    {
        kdSetError(KD_EIO);
        return KD_NULL;
    }
    KDImageATX image = kdGetImageFromStreamATX(file, format, flags);
    kdFclose(file);
    return image;
}

KD_API KDImageATX KD_APIENTRY kdGetImageFromStreamATX(KDFile *file, KDint format, KDint flags)
{
    _KDImageATX *image = (_KDImageATX *)kdMalloc(sizeof(_KDImageATX));
    if(image == KD_NULL)
    {
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    image->levels = 0;
    image->bpp= 8;

    KDStat st;
    if(kdFstat(file, &st) == -1)
    {
        kdFree(image);
        kdSetError(KD_EIO);
        return KD_NULL;
    }

    void *filedata = kdMalloc((KDsize)st.st_size);
    if(filedata == KD_NULL)
    {
        kdFree(image);
        kdSetError(KD_ENOMEM);
        return KD_NULL;
    }
    if(kdFread(filedata, 1, (KDsize)st.st_size, file) != (KDsize)st.st_size)
    {
        kdFree(filedata);
        kdFree(image);
        kdSetError(KD_EIO);
        return KD_NULL;
    }
    if(kdFseek(file, 0, KD_SEEK_SET) == -1)
    {
        kdFree(filedata);
        kdFree(image);
        kdSetError(KD_EIO);
        return KD_NULL;
    }

    KDint channels = 0;
    image->format = format;
    switch(image->format)
    {
        case(KD_IMAGE_FORMAT_RGBA8888_ATX):
        {
            channels = 4;
            image->alpha = KD_TRUE;
            break;
        }
        case(KD_IMAGE_FORMAT_RGB888_ATX):
        {
            channels = 3;
            image->alpha = KD_FALSE;
            break;
        }
        case(KD_IMAGE_FORMAT_LUMALPHA88_ATX):
        {
            channels = 2;
            image->alpha = KD_TRUE;
            break;
        }
        case(KD_IMAGE_FORMAT_ALPHA8_ATX):
        {
            channels = 1;
            image->alpha = KD_TRUE;
            break;
        }
        default:
        {
            kdFree(filedata);
            kdFree(image);
            kdSetError(KD_EINVAL);
            return KD_NULL;
        }
    }

    if(flags != 0)
    {
        kdFree(filedata);
        kdFree(image);
        kdSetError(KD_EINVAL);
        return KD_NULL;
    }

    image->buffer = stbi_load_from_memory(filedata, (KDint)st.st_size, &image->width, &image->height, (int[]){0}, channels);
    if(image->buffer == KD_NULL)
    {
        kdFree(filedata);
        kdFree(image);
        kdSetError(KD_EILSEQ);
        return KD_NULL;
    }
    kdFree(filedata);

    image->size = (KDsize)image->width * (KDsize)image->height * (KDsize)channels;
    return image;
}

/* kdFreeImageATX: Free image object. */
KD_API void KD_APIENTRY kdFreeImageATX(KDImageATX image)
{
    _KDImageATX *_image = image;
    if(_image->buffer)
    {
        kdFree(_image->buffer);
    }
    kdFree(_image);
}

KD_API void *KD_APIENTRY kdGetImagePointerATX(KDImageATX image, KDint attr)
{
    if(attr == KD_IMAGE_POINTER_BUFFER_ATX)
    {
        _KDImageATX *_image = image;
        return _image->buffer;
    }
    else
    {
        kdSetError(KD_EINVAL);
        return KD_NULL;
    }
}

/* kdGetImageIntATX, kdGetImageLevelIntATX: Get the value of an image integer attribute. */
KD_API KDint KD_APIENTRY kdGetImageIntATX(KDImageATX image, KDint attr)
{
    _KDImageATX *_image = image;
    switch(attr)
    {
        case(KD_IMAGE_WIDTH_ATX):
        {
            return _image->width;
        }
        case(KD_IMAGE_HEIGHT_ATX):
        {
            return _image->height;
        }
        case(KD_IMAGE_FORMAT_ATX):
        {
            return _image->format;
        }
        case(KD_IMAGE_STRIDE_ATX):
        {
            return 0;
        }
        case(KD_IMAGE_BITSPERPIXEL_ATX):
        {
            return _image->bpp;
        }
        case(KD_IMAGE_LEVELS_ATX):
        {
            return _image->levels;
        }
        case(KD_IMAGE_DATASIZE_ATX):
        {
            /* Specbug: Int is too small... */
            kdAssert(0);
            return (KDint)_image->size;
        }
        case(KD_IMAGE_BUFFEROFFSET_ATX):
        {
            return 0;
        }
        case(KD_IMAGE_ALPHA_ATX):
        {
            return _image->alpha;
        }
        default:
        {
            kdSetError(KD_EINVAL);
            return 0;
        }
    }
}

KD_API KDint KD_APIENTRY kdGetImageLevelIntATX(KDImageATX image, KDint attr, KDint level)
{
    if(level != 0)
    {
        kdSetError(KD_EINVAL);
        return 0;
    }
    return kdGetImageIntATX(image, attr);
}
