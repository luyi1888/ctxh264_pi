/***************************************************************************
*
*   video_gl.h
*
*   H264_decode.h implementation for Raspberry Pi.
*
*   Author: Muhammad Dawood (muhammad.dawood@citrix.com)
*   Copyright 2013-2014 Citrix Systems, Inc.  All Rights Reserved.
*
****************************************************************************/
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XShm.h>

#include "bcm_host.h"
#include "ilclient.h"
#include "GLES/gl.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

#define X11_SUPPORT
#include "citrix.h"
#include "H264_decode.h"

typedef unsigned char BOOL;

#define TRUE        1
#define FALSE       0
#define TIMEOUT_MS  2000

#define max(a,b) (((a) > (b)) ? (a) : (b)) 
#define min(a,b) (((a) < (b)) ? (a) : (b))

typedef struct _comp_details {
    COMPONENT_T    *component;
    OMX_HANDLETYPE  handle;
    int             in_port;
    int             out_port;
} comp_details;

typedef struct _OMXH264_decoder {
    ILCLIENT_T      *client;
    TUNNEL_T        tunnel[2];

    comp_details    *image_decode;
    comp_details    *video_render;
    int             renderer_init;

    int             width;
    int             height;

} OMXH264_decoder;


bool v3_init();
H264_context v3_open_context(int width, int height, void *codec_data, int len, unsigned int options);
bool v3_start_frame(H264_context Ctx, unsigned int encoded_size, SIGNED_RECT dirty_rects[], unsigned int num_rects);
bool v3_decode_frame(H264_context Ctx, void* H264_data, int len, bool last);
bool v3_compose_with_fb(H264_context Ctx, struct image_buf *fb, SIGNED_RECT interesting_rects[], unsigned int num_rects);
bool v3_compose_with_rects(H264_context Ctx, struct image_buf text_rects[], unsigned int num_rects, bool last);
bool v3_push_frame(H264_context cxt, struct window_info windows[], unsigned int num_windows, bool wait, bool *pushed);
void v3_close_context(H264_context Ctx);
void v3_end ();
