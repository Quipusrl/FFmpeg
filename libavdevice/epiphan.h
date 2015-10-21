/*
 * Epiphan Grabbers device
 * Copyright (c) 2015 Gianluigi Tiesi <sherpya@netfarm.it>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVDEVICE_EPIPHAN_H

#include <inttypes.h>

#if defined(_MSC_VER)
#define V2U_PACKED
#include <pshpack1.h>
#else
#define V2U_PACKED __attribute__((packed))
#endif

typedef uint32_t V2U_UINT32;
typedef int32_t V2U_INT32;
typedef V2U_INT32 V2U_BOOL;

typedef struct _V2URect {
    V2U_INT32 x;
    V2U_INT32 y;
    V2U_INT32 width;
    V2U_INT32 height;
} V2U_PACKED V2URect;

typedef struct _V2U_VideoMode {
    V2U_INT32 width;            /* screen width, pixels */
    V2U_INT32 height;           /* screen height, pixels */
    V2U_INT32 vfreq;            /* vertical refresh rate, mHz */
} V2U_PACKED V2U_VideoMode;

typedef struct _V2U_GrabFrame2  {
    void* pixbuf;               /* IN  should be filled by user process */
    V2U_UINT32 pixbuflen;       /* IN  should be filled by user process */
    V2U_UINT32 palette;         /* IN pixel format */
    V2URect crop;               /* IN/OUT cropping area; all zeros = full frame */
    V2U_VideoMode mode;         /* OUT VGA mode */
    V2U_UINT32 imagelen;        /* OUT size of the image stored in pixbuf */
    V2U_INT32 retcode;          /* OUT return/error code */
} V2U_PACKED V2U_GrabFrame2;

typedef struct _FrmGrabber FrmGrabber;

#ifdef _MSC_VER
#include <poppack.h>
#endif

#define V2U_GRABFRAME_FORMAT_NONE       0x00000000
#define V2U_GRABFRAME_FORMAT_RGB8       0x00000008 /* R2:G3:B3 */
#define V2U_GRABFRAME_FORMAT_RGB24      0x00000018
#define V2U_GRABFRAME_FORMAT_Y8         0x00000500
#define V2U_GRABFRAME_FORMAT_BGR24      0x00000800
#define V2U_GRABFRAME_FORMAT_I420       0x00000A00 /* Same as YUV420P */
#define V2U_GRABFRAME_FORMAT_ARGB32     0x00000B00

#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/frame.h"

typedef void (*imp_FrmGrab_Init)(void);
typedef void (*imp_FrmGrab_Deinit)(void);
typedef int (*imp_FrmGrabLocal_OpenAll)(FrmGrabber* grabbers[], int maxcount);
typedef FrmGrabber* (*imp_FrmGrabLocal_OpenSN)(const char* sn);
typedef void (*imp_FrmGrab_Close)(FrmGrabber* fg);
typedef V2U_BOOL (*imp_FrmGrab_Start)(FrmGrabber* fg);
typedef void (*imp_FrmGrab_Stop)(FrmGrabber* fg);
typedef V2U_GrabFrame2* (*imp_FrmGrab_Frame)(FrmGrabber* fg, V2U_UINT32 format, const V2URect* crop);
typedef void (*imp_FrmGrab_Release)(FrmGrabber* fg, V2U_GrabFrame2* frame);
typedef const char* (*imp_FrmGrab_GetId)(FrmGrabber* fg);
typedef const char* (*imp_FrmGrab_GetProductName)(FrmGrabber* fg);
typedef V2U_BOOL (*imp_FrmGrab_DetectVideoMode)(FrmGrabber* fg, V2U_VideoMode* vm);
typedef V2U_BOOL (*imp_FrmGrab_SetMaxFps)(FrmGrabber* fg, double maxFps);

typedef struct _FrmGrabLib
{
    imp_FrmGrab_Init FrmGrab_Init;
    imp_FrmGrab_Deinit FrmGrab_Deinit;
    imp_FrmGrabLocal_OpenAll FrmGrabLocal_OpenAll;
    imp_FrmGrabLocal_OpenSN FrmGrabLocal_OpenSN;
    imp_FrmGrab_Close FrmGrab_Close;
    imp_FrmGrab_Start FrmGrab_Start;
    imp_FrmGrab_Stop FrmGrab_Stop;
    imp_FrmGrab_Frame FrmGrab_Frame;
    imp_FrmGrab_Release FrmGrab_Release;
    imp_FrmGrab_GetId FrmGrab_GetId;
    imp_FrmGrab_GetProductName FrmGrab_GetProductName;
    imp_FrmGrab_DetectVideoMode FrmGrab_DetectVideoMode;
    imp_FrmGrab_SetMaxFps FrmGrab_SetMaxFps;
} FrmGrabLib;

struct epiphan_ctx {
    const AVClass *class;
    void *hLib;
    int list_devices;
    enum AVPixelFormat pixel_format;
    char *framerate;
    int width, height;
    int64_t curtime;
    int64_t frame_time;
    FrmGrabLib pfn;
    FrmGrabber *grabber;
    V2U_VideoMode videomode;
    V2U_GrabFrame2 *frame;
    V2U_UINT32 pixel_format_ep;
    AVFrame *source_frame, *scaled_frame;
    uint8_t *frame_buffer;
    int scaled_size;
    struct SwsContext *sws_context;
};

#define AVDEVICE_EPIPHAN_H
#endif /* AVDEVICE_EPIPHAN_H */
