/*
     droid vnc server - Android VNC server
     Copyright (C) 2009 Jose Pereira <onaips@gmail.com>

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 3 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
     */

#include <fcntl.h>

#include "flinger.h"
#include "screenFormat.h"

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <binder/ProcessState.h>

#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>

#include <ui/DisplayInfo.h>
#include <ui/GraphicTypes.h>
#include <ui/PixelFormat.h>

#include <system/graphics.h>

// TODO: Fix Skia.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic pop

#define COLORSPACE_UNKNOWN    0
#define COLORSPACE_SRGB       1
#define COLORSPACE_DISPLAY_P3 2

using namespace android;

size_t Bpp = 32;
static const int COMPONENT_YUV = 0xFF;
static screenFormat format;
static sp<IBinder> display;
static void *new_base = NULL;
uint32_t captureOrientation;
sp<GraphicBuffer> outBuffer;

struct PixelFormatInformation {
    enum {
        INDEX_ALPHA   = 0,
        INDEX_RED     = 1,
        INDEX_GREEN   = 2,
        INDEX_BLUE    = 3
    };

    enum { // components
        ALPHA   = 1,
        RGB     = 2,
        RGBA    = 3,
        L       = 4,
        LA      = 5,
        OTHER   = 0xFF
    };

    struct szinfo {
        uint8_t h;
        uint8_t l;
    };

    inline PixelFormatInformation() : version(sizeof(PixelFormatInformation)) { }
    size_t getScanlineSize(unsigned int width) const;
    size_t getSize(size_t ci) const {
        return (ci <= 3) ? (cinfo[ci].h - cinfo[ci].l) : 0;
    }
    size_t      version;
    PixelFormat format;
    size_t      bytesPerPixel;
    size_t      bitsPerPixel;
    union {
        szinfo      cinfo[4];
        struct {
            uint8_t     h_alpha;
            uint8_t     l_alpha;
            uint8_t     h_red;
            uint8_t     l_red;
            uint8_t     h_green;
            uint8_t     l_green;
            uint8_t     h_blue;
            uint8_t     l_blue;
        };
    };
    uint8_t     components;
    uint8_t     reserved0[3];
    uint32_t    reserved1;
};

struct Info {
    size_t      size;
    size_t      bitsPerPixel;
    struct {
        uint8_t     ah;
        uint8_t     al;
        uint8_t     rh;
        uint8_t     rl;
        uint8_t     gh;
        uint8_t     gl;
        uint8_t     bh;
        uint8_t     bl;
    };
    uint8_t     components;
};

static Info const sPixelFormatInfos[] = {
         { 0,  0, { 0, 0,   0, 0,   0, 0,   0, 0 }, 0 },
         { 4, 32, {32,24,   8, 0,  16, 8,  24,16 }, PixelFormatInformation::RGBA },
         { 4, 24, { 0, 0,   8, 0,  16, 8,  24,16 }, PixelFormatInformation::RGB  },
         { 3, 24, { 0, 0,   8, 0,  16, 8,  24,16 }, PixelFormatInformation::RGB  },
         { 2, 16, { 0, 0,  16,11,  11, 5,   5, 0 }, PixelFormatInformation::RGB  },
         { 4, 32, {32,24,  24,16,  16, 8,   8, 0 }, PixelFormatInformation::RGBA },
         { 2, 16, { 1, 0,  16,11,  11, 6,   6, 1 }, PixelFormatInformation::RGBA },
         { 2, 16, { 4, 0,  16,12,  12, 8,   8, 4 }, PixelFormatInformation::RGBA },
         { 1,  8, { 8, 0,   0, 0,   0, 0,   0, 0 }, PixelFormatInformation::ALPHA},
         { 1,  8, { 0, 0,   8, 0,   8, 0,   8, 0 }, PixelFormatInformation::L    },
         { 2, 16, {16, 8,   8, 0,   8, 0,   8, 0 }, PixelFormatInformation::LA   },
         { 1,  8, { 0, 0,   8, 5,   5, 2,   2, 0 }, PixelFormatInformation::RGB  },
};

static const Info* gGetPixelFormatTable(size_t* numEntries) {
    if (numEntries) {
        *numEntries = sizeof(sPixelFormatInfos)/sizeof(Info);
    }
    return sPixelFormatInfos;
}

status_t getPixelFormatInformation(PixelFormat format, PixelFormatInformation* info)
{
    if (format <= 0)
        return BAD_VALUE;

    if (info->version != sizeof(PixelFormatInformation))
        return INVALID_OPERATION;

    // YUV format from the HAL are handled here
    switch (format) {
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
        info->bitsPerPixel = 16;
        goto done;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YV12:
        info->bitsPerPixel = 12;
     done:
        info->format = format;
        info->components = COMPONENT_YUV;
        info->bytesPerPixel = 1;
        info->h_alpha = 0;
        info->l_alpha = 0;
        info->h_red = info->h_green = info->h_blue = 8;
        info->l_red = info->l_green = info->l_blue = 0;
        return NO_ERROR;
    }

    size_t numEntries;
    const Info *i = gGetPixelFormatTable(&numEntries) + format;
    bool valid = uint32_t(format) < numEntries;
    if (!valid) {
        return BAD_INDEX;
    }

    info->format = format;
    info->bytesPerPixel = i->size;
    info->bitsPerPixel  = i->bitsPerPixel;
    info->h_alpha       = i->ah;
    info->l_alpha       = i->al;
    info->h_red         = i->rh;
    info->l_red         = i->rl;
    info->h_green       = i->gh;
    info->l_green       = i->gl;
    info->h_blue        = i->bh;
    info->l_blue        = i->bl;
    info->components    = i->components;

    return NO_ERROR;
}

int screen_cap()
{
    status_t result = ScreenshotClient::capture(display, Rect(), 0, 0, INT32_MIN, INT32_MAX, false, captureOrientation, &outBuffer);
    if (result != NO_ERROR) {
        close_flinger();
        exit(-1);
    }
    return 0;
}

extern "C" screenFormat getscreenformat_flinger()
{
    //get format on PixelFormat struct
    PixelFormat f=outBuffer->getPixelFormat();

    PixelFormatInformation pf;
    getPixelFormatInformation(f,&pf);

    Bpp = bytesPerPixel(f);
    L("Bpp set to %d\n", Bpp);

    format.bitsPerPixel = bitsPerPixel(f);
    format.width        = outBuffer->getWidth();
    format.height       = outBuffer->getHeight();
    format.size         = format.bitsPerPixel*format.width*format.height/CHAR_BIT;
    format.redShift     = pf.l_red;
    format.redMax       = pf.h_red - pf.l_red;
    format.greenShift   = pf.l_green;
    format.greenMax     = pf.h_green - pf.l_green;
    format.blueShift    = pf.l_blue;
    format.blueMax      = pf.h_blue - pf.l_blue;
    format.alphaShift   = pf.l_alpha;
    format.alphaMax     = pf.h_alpha-pf.l_alpha;

    return format;
}


extern "C" int init_flinger()
{
    uint32_t width, height, stride;
    static const uint32_t ORIENTATION_MAP[] = {
        ISurfaceComposer::eRotateNone, // 0 == DISPLAY_ORIENTATION_0
        ISurfaceComposer::eRotate270, // 1 == DISPLAY_ORIENTATION_90
        ISurfaceComposer::eRotate180, // 2 == DISPLAY_ORIENTATION_180
        ISurfaceComposer::eRotate90, // 3 == DISPLAY_ORIENTATION_270
    };

    L("--Initializing Pie access method--\n");

    ProcessState::self()->setThreadPoolMaxThreadCount(0);
    ProcessState::self()->startThreadPool();

    int32_t displayId = ISurfaceComposer::eDisplayIdMain;
    display = SurfaceComposerClient::getBuiltInDisplay(displayId);
    if (display == NULL) {
        L("Unable to get handle for display %d\n", displayId);
        close_flinger();
        return -1;
    }

    Vector<DisplayInfo> configs;
    SurfaceComposerClient::getDisplayConfigs(display, &configs);
    int activeConfig = SurfaceComposerClient::getActiveConfig(display);
    if (static_cast<size_t>(activeConfig) >= configs.size()) {
        L("Active config %d not inside configs (size %zu)\n", activeConfig, configs.size());
        close_flinger();
        return -1;
    }
    uint8_t displayOrientation = configs[activeConfig].orientation;
    captureOrientation = ORIENTATION_MAP[displayOrientation];

    screen_cap();
    L("ScreenFormat: %d\n", outBuffer->getPixelFormat());

    width = outBuffer->getWidth();
    height = outBuffer->getHeight();
    stride = outBuffer->getStride();

    // allocate additional frame buffer if the source one is not continuous
    if (stride > width) {
        new_base = malloc(width * height* Bpp);
        if(new_base == NULL) {
            close_flinger();
            return -1;
        }
    }

    return 0;
}

extern "C" unsigned int *checkfb_flinger()
{
    void* base = 0;
    screen_cap();
    outBuffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN, &base);
    return (unsigned int*)base;
}

extern "C" unsigned int *readfb_flinger()
{
    void* base = 0;
    uint32_t width, height, stride;
    screen_cap();
    outBuffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN, &base);

    width = outBuffer->getWidth();
    height = outBuffer->getHeight();
    stride = outBuffer->getStride();

    if (stride > width) {
        void *tmp_ptr = new_base;

        for (size_t y = 0; y < height; y++) {
            memcpy(tmp_ptr, base, width * Bpp);
            // Pointer arithmetic on void pointers is frowned upon, apparently.
            tmp_ptr = (void *)((char *)tmp_ptr + width * Bpp);
            base = (void *)((char *)base + stride * Bpp);
        }
        return (unsigned int *)new_base;
    }
    return (unsigned int *)base;
}

extern "C" void close_flinger()
{
    display = NULL;
    captureOrientation = 0;

    if(outBuffer != 0) {
        outBuffer.clear();
    }
    if(new_base != NULL) {
        free(new_base);
    }
}
