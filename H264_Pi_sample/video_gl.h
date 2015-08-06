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

typedef struct _OMXH264_watermark
{
    void                        *image;
    DISPMANX_RESOURCE_HANDLE_T  resource;
    DISPMANX_ELEMENT_HANDLE_T   element;
    uint32_t                    vc_image_ptr;
    int                         width, height;
} OMXH264_watermark;

typedef struct _OMXH264_cursor
{
    XFixesCursorImage           *X_cur;
    void                        *image;
    DISPMANX_RESOURCE_HANDLE_T  resource;
    DISPMANX_ELEMENT_HANDLE_T   element;
    uint32_t                    vc_image_ptr;
    int                         width, height;
    int                         xhot, yhot;
    int                         lx, ly;
} OMXH264_cursor;

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
    comp_details    *image_resize;   /* Seamless. */
    comp_details    *egl_render;
    EGLImageKHR     egl_image;
    int             renderer_init;

    /* Cursor support in EGL mode. */
    OMXH264_cursor  cursor;
    pthread_t       mouse_reader;
    int             mouse_fd;
    pthread_t       window_reader;
    int             terminate_readers;
    Display         *disp;
    Screen          *scr;
    Window          ica_window;
    Window          ica_parent;

    /* Watermark. */
    OMXH264_watermark watermark;

    /* XImage for seamless. */
    XImage          *fb;
    unsigned int    size;
    XShmSegmentInfo shm_info;
    int             shm_id;
    void            *old_ptr;

    /* Dirty rects. */
    SIGNED_RECT     dirty_rects[31];
    int             num_rects;

    int             dest_x;
    int             dest_y;
    int             width;
    int             height;
    int             stride;
    void		    *output_buffer;
    OMX_BUFFERHEADERTYPE        *outbuf;

    EGL_DISPMANX_WINDOW_T       nativewindow;

    DISPMANX_ELEMENT_HANDLE_T   dispman_element;
    DISPMANX_DISPLAY_HANDLE_T   dispman_display;
    DISPMANX_UPDATE_HANDLE_T    dispman_update;

    EGLDisplay      display;
    EGLSurface      surface;
    EGLContext      context;
    GLuint          tex;
} OMXH264_decoder;

void move_egl_display(OMXH264_decoder *decoder, BOOL force);
void init_ogl(OMXH264_decoder *decoder);
void deinit_ogl(OMXH264_decoder *decoder);

bool v3_init();
H264_context v3_open_context(int width, int height, void *codec_data, int len, unsigned int options);
bool v3_start_frame(H264_context Ctx, unsigned int encoded_size, SIGNED_RECT dirty_rects[], unsigned int num_rects);
bool v3_decode_frame(H264_context Ctx, void* H264_data, int len, bool last);
bool v3_compose_with_fb(H264_context Ctx, struct image_buf *fb, SIGNED_RECT interesting_rects[], unsigned int num_rects);
bool v3_compose_with_rects(H264_context Ctx, struct image_buf text_rects[], unsigned int num_rects, bool last);
bool v3_push_frame(H264_context cxt, struct window_info windows[], unsigned int num_windows, bool wait, bool *pushed);
void v3_close_context(H264_context Ctx);
void v3_end ();
