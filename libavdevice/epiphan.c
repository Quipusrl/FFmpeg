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

#include "config.h"
#include "epiphan.h"

#include "avdevice.h"
#include "libavformat/internal.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavutil/internal.h"

#ifdef _WIN32
#include <windows.h>
#define LIBEXT "dll"
#define dlopen(fname, f) ((void *) LoadLibraryA(fname))
#define dlclose(handle) FreeLibrary((HMODULE) handle)
#define dlsym(handle, name) GetProcAddress((HMODULE) handle, name)
#define dlerror() "LoadLibrary() failed"
#else
#include <dlfcn.h>
#ifdef __APPLE__
#define LIBEXT "dylib"
#else
#define LIBEXT "so"
#endif
#endif

#define MAX_GRABBERS 32

#define DLSYM(x) \
    do \
    { \
        ctx->pfn.x = ( imp_##x ) dlsym(ctx->hLib, AV_STRINGIFY(x)); \
        if (!ctx->pfn.x ) \
        { \
            av_log(avctx, AV_LOG_ERROR, "Unable to find symbol " AV_STRINGIFY(x) " in dynamic frmgrab." LIBEXT "\n"); \
            goto error; \
        } \
    } while (0)

static V2U_UINT32 epiphan_pixfmt(enum AVPixelFormat format) {
    switch (format) {
        case AV_PIX_FMT_RGB8:
            return V2U_GRABFRAME_FORMAT_RGB8;

        case AV_PIX_FMT_RGB24:
            return V2U_GRABFRAME_FORMAT_RGB24;

        case AV_PIX_FMT_GRAY8:
            return V2U_GRABFRAME_FORMAT_Y8;

        case AV_PIX_FMT_BGR24:
            return V2U_GRABFRAME_FORMAT_BGR24;

        case AV_PIX_FMT_YUV420P:
            return V2U_GRABFRAME_FORMAT_I420;

        case AV_PIX_FMT_ARGB:
            return V2U_GRABFRAME_FORMAT_ARGB32;
    }
    return V2U_GRABFRAME_FORMAT_NONE;
}

static int epiphan_read_close(AVFormatContext *s) {
    struct epiphan_ctx *ctx = s->priv_data;

    if (ctx->grabber) {
        ctx->pfn.FrmGrab_Release(ctx->grabber, ctx->frame);
        ctx->pfn.FrmGrab_Stop(ctx->grabber);
        ctx->pfn.FrmGrab_Close(ctx->grabber);
        ctx->pfn.FrmGrab_Deinit();
    }

    if (ctx->hLib)
        dlclose(ctx->hLib);

    return 0;
}

static int epiphan_read_header(AVFormatContext *avctx) {
    struct epiphan_ctx *ctx = avctx->priv_data;
    V2U_VideoMode vm;
    AVStream *st;
    AVCodecContext *codec;
    AVRational framerate_q;

    int ret = AVERROR(EIO);

    if (!(ctx->hLib = dlopen("frmgrab." LIBEXT, RTLD_NOW))) {
        av_log(avctx, AV_LOG_ERROR, "Unable to load frmgrab." LIBEXT ": %s\n", dlerror());
        return ret;
    }

    DLSYM(FrmGrab_Init);
    DLSYM(FrmGrab_Deinit);
    DLSYM(FrmGrabLocal_OpenAll);
    DLSYM(FrmGrabLocal_OpenSN);
    DLSYM(FrmGrab_Close);
    DLSYM(FrmGrab_Start);
    DLSYM(FrmGrab_Stop);
    DLSYM(FrmGrab_Frame);
    DLSYM(FrmGrab_Release);
    DLSYM(FrmGrab_GetId);
    DLSYM(FrmGrab_GetProductName);
    DLSYM(FrmGrab_DetectVideoMode);
    DLSYM(FrmGrab_SetMaxFps);

    ctx->pfn.FrmGrab_Init();

    if (ctx->list_devices) {
        FrmGrabber* grabbers[MAX_GRABBERS];
        int count = ctx->pfn.FrmGrabLocal_OpenAll(grabbers, MAX_GRABBERS);

        av_log(avctx, AV_LOG_INFO, "Epiphan grabber devices:\n");
        for (int i = 0; i < count; i++) {
            av_log(avctx, AV_LOG_INFO, "%s: \"%s\"\n", ctx->pfn.FrmGrab_GetProductName(grabbers[i]),
                   ctx->pfn.FrmGrab_GetId(grabbers[i]));
            ctx->pfn.FrmGrab_Close(grabbers[i]);
        }

        ret = AVERROR_EXIT;
        goto error;
    }

    if (!avctx->filename || !(ctx->grabber = ctx->pfn.FrmGrabLocal_OpenSN(avctx->filename))) {
        av_log(avctx, AV_LOG_INFO, "Unable to open the selected device\n");
        goto error;
    }

    if (ctx->pfn.FrmGrab_DetectVideoMode(ctx->grabber, &vm))
        av_log(avctx, AV_LOG_INFO, "Detected %dx%d %d.%d Hz\n", vm.width, vm.height,
                                                                (vm.vfreq + 50) / 1000,
                                                                ((vm.vfreq + 50) % 1000) / 100);
    else {
        av_log(avctx, AV_LOG_ERROR, "No signal detected\n");
        goto error;
    }

    ctx->pixel_format_ep = epiphan_pixfmt(ctx->pixel_format);

    if (ctx->pixel_format_ep == V2U_GRABFRAME_FORMAT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format '%s'\n", av_get_pix_fmt_name(ctx->pixel_format));
        goto error;
    }

    ctx->frame = NULL;
    ctx->pfn.FrmGrab_Start(ctx->grabber);

    ret = av_parse_video_rate(&framerate_q, ctx->framerate);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not parse framerate '%s'\n", ctx->framerate);
        goto error;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Setting max fps: %s\n", ctx->framerate);
    ctx->pfn.FrmGrab_SetMaxFps(ctx->grabber, av_q2d(framerate_q));

    st = avformat_new_stream(avctx, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    codec = st->codec;
    codec->time_base = av_inv_q(framerate_q);
    codec->codec_type = AVMEDIA_TYPE_VIDEO;
    codec->codec_id = AV_CODEC_ID_RAWVIDEO;
    codec->width  = vm.width;
    codec->height = vm.height;
    codec->pix_fmt = ctx->pixel_format;

    ctx->frame_time = av_rescale_q(1, codec->time_base, AV_TIME_BASE_Q);
    ctx->curtime = av_gettime();
    avpriv_set_pts_info(st, 64, framerate_q.den, framerate_q.num);

    ret = 0;

error:
    if (ret < 0)
        epiphan_read_close(avctx);

    return ret;
}

static int epiphan_read_packet(AVFormatContext *s, AVPacket *pkt) {
    struct epiphan_ctx *ctx = s->priv_data;
    int64_t delay;

    /* release the previous frame, FrmGrab_Release ignores NULL */
    ctx->pfn.FrmGrab_Release(ctx->grabber, ctx->frame);

    if (!(ctx->frame = ctx->pfn.FrmGrab_Frame(ctx->grabber, ctx->pixel_format_ep, NULL)))
        return AVERROR(EIO);

    av_init_packet(pkt);
    pkt->data = ctx->frame->pixbuf;
    pkt->size = ctx->frame->imagelen;
    pkt->flags |= AV_PKT_FLAG_KEY;

    /* looks like FrmGrab_SetMaxFps() does not work as expected */
    ctx->curtime += ctx->frame_time;
    delay = ctx->curtime - av_gettime();
    if (delay > 0)
        av_usleep(delay);

//    pkt->pts = ctx->curtime;

    return pkt->size;
}

#define OFFSET(x) offsetof(struct epiphan_ctx, x)
static const AVOption options[] = {
    { "pixel_format", "set video pixel format", OFFSET(pixel_format), AV_OPT_TYPE_PIXEL_FMT, {.i64 = AV_PIX_FMT_YUV420P}, -1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "framerate", "set video frame rate", OFFSET(framerate), AV_OPT_TYPE_STRING, {.str = "30"}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
    { "list_devices", "list available devices", OFFSET(list_devices), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, AV_OPT_FLAG_DECODING_PARAM, "list_devices" },
    { NULL },
};

static const AVClass epiphan_class = {
    .class_name = "epiphan indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

AVInputFormat ff_epiphan_demuxer = {
    .name           = "epiphan",
    .long_name      = NULL_IF_CONFIG_SMALL("Epiphan capture"),
    .priv_data_size = sizeof(struct epiphan_ctx),
    .read_header    = epiphan_read_header,
    .read_packet    = epiphan_read_packet,
    .read_close     = epiphan_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &epiphan_class,
};
