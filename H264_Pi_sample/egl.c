/***************************************************************************
*
*   egl.c
*
*   OpenGL display output for H.264 decoded frames. Note this implementation
*   redraws the mouse cursor as a separate layer that sits on top of main
*   frame layer. This needs to be reworked as the mouse can be somewhat
*   buggy at times.
*
*   Author: Muhammad Dawood (muhammad.dawood@citrix.com)
*   Copyright 2013-2014 Citrix Systems, Inc.  All Rights Reserved.
*
****************************************************************************/

#include "video_gl.h"
#include "citrix_rgb.h"
#include <pthread.h>

#define WINDOW_READ_TIME_MS 500

//#define WATERMARK

BOOL window_hidden = FALSE;

static const GLbyte quadx[1*4*3] = {
   -1, -1,  1,
   1, -1,  1,
   -1,  1,  1,
   1,  1,  1
};

static const GLfloat texCoords[1 * 4 * 2] = {
   0.f,  1.f,
   1.f,  1.f,
   0.f,  0.f,
   1.f,  0.f
};

static void hide_egl_display(OMXH264_decoder *decoder)
{
    VC_RECT_T dst;

    vc_dispmanx_rect_set(&dst, decoder->dest_x, decoder->dest_y, 1, 1);

    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);         
    vc_dispmanx_element_change_attributes(update, decoder->dispman_element, 1 << 2, 0, 0, &dst, NULL, 0, 0);
    vc_dispmanx_update_submit_sync(update);
}

void move_egl_display(OMXH264_decoder *decoder, BOOL force)
{
    XWindowAttributes xwa;
    Window temp;

    XGetWindowAttributes(decoder->disp, decoder->ica_window, &xwa);
    XTranslateCoordinates(decoder->disp, decoder->ica_window, xwa.root, 0, 0, &xwa.x, &xwa.y, &temp);

    if (xwa.x != decoder->dest_x || xwa.y != decoder->dest_y || force) {
        VC_RECT_T dst;

        decoder->dest_x = xwa.x;
        decoder->dest_y = xwa.y;

        vc_dispmanx_rect_set(&dst, decoder->dest_x, decoder->dest_y, decoder->width, decoder->height);

        DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);         
        vc_dispmanx_element_change_attributes(update, decoder->dispman_element, 1 << 2, 0, 0, &dst, NULL, 0, 0);
        vc_dispmanx_update_submit_sync(update);
    }
}

static void create_watermark(OMXH264_decoder *decoder)
{
    static VC_IMAGE_TYPE_T type = VC_IMAGE_ARGB8888;
    static VC_DISPMANX_ALPHA_T alpha = {DISPMANX_FLAGS_ALPHA_FROM_SOURCE, 255, 0};

    OMXH264_watermark *vars = &(decoder->watermark);

    if (!vars) {
        return;
    }

    VC_RECT_T src_rect;
    VC_RECT_T dst_rect;
    int stride;

    vars->width = citrix_rgb.width;
    vars->height = citrix_rgb.height;
    stride = vars->width * 4;

    vars->image = (void *)citrix_rgb.pixel_data;

    vars->resource = vc_dispmanx_resource_create(type, vars->width, vars->height, &vars->vc_image_ptr);

    vc_dispmanx_rect_set(&dst_rect, 0, 0, vars->width, vars->height);
    vc_dispmanx_resource_write_data(vars->resource, type, stride, vars->image, &dst_rect);

    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    vc_dispmanx_rect_set(&src_rect, 0, 0, vars->width << 16, vars->height << 16);
    vc_dispmanx_rect_set(&dst_rect, 16, 16, vars->width, vars->height);

    vars->element = vc_dispmanx_element_add(update, decoder->dispman_display,
                                                2000, /* layer */
                                                &dst_rect,
                                                vars->resource,
                                                &src_rect,
                                                DISPMANX_PROTECTION_NONE,
                                                &alpha,
                                                NULL,
                                                VC_IMAGE_ROT0);

    vc_dispmanx_update_submit_sync(update);
}

static void create_dispmanx_cursor(OMXH264_decoder *decoder, XFixesCursorImage *cursor)
{
    static VC_IMAGE_TYPE_T type = VC_IMAGE_ARGB8888;
    static VC_DISPMANX_ALPHA_T alpha = {DISPMANX_FLAGS_ALPHA_FROM_SOURCE, 255, 0};

    OMXH264_cursor *vars = &(decoder->cursor);

    if (!vars) {
        return;
    }

    if (vars->image) {
        /* Remove existing cursor. */

        free(vars->image);
        vars->image = NULL;

        DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
        vc_dispmanx_element_remove(update, vars->element);
        vc_dispmanx_update_submit_sync(update);
        vc_dispmanx_resource_delete(vars->resource);
    }

    if (cursor && cursor->width > 0 && cursor->height > 0) {
        VC_RECT_T src_rect;
        VC_RECT_T dst_rect;
        int stride = 0, x, y;

        vars->xhot = cursor->xhot;
        vars->yhot = cursor->yhot;
        vars->width = (cursor->width + 15) & ~15;
        vars->height = (cursor->height + 15) & ~15;
        stride = vars->width * 4;
    
        vars->image = malloc(vars->height * stride);

        memset(vars->image, 0, vars->height * stride);

        for (y = 0; y < cursor->height; y++) {
            unsigned long *ptr = (unsigned long *) ((unsigned char *)vars->image + (y * stride));
            for (x = 0; x < cursor->width; x++) {
                unsigned long value = *(cursor->pixels + (y * cursor->width) + x);
                *ptr++ = value;
            }
        }

        vars->resource = vc_dispmanx_resource_create(type, vars->width, vars->height, &vars->vc_image_ptr);

        vc_dispmanx_rect_set(&dst_rect, 0, 0, vars->width, vars->height);
        vc_dispmanx_resource_write_data(vars->resource, type, stride, vars->image, &dst_rect);

        DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
        vc_dispmanx_rect_set(&src_rect, 0, 0, vars->width << 16, vars->height << 16);
        vc_dispmanx_rect_set(&dst_rect, vars->lx - vars->xhot, vars->ly - vars->yhot, vars->width, vars->height);

        vars->element = vc_dispmanx_element_add(update, decoder->dispman_display,
                                                    2000, /* layer */
                                                    &dst_rect,
                                                    vars->resource,
                                                    &src_rect,
                                                    DISPMANX_PROTECTION_NONE,
                                                    &alpha,
                                                    NULL,
                                                    VC_IMAGE_ROT0);

        vc_dispmanx_update_submit_sync(update);
    }
}

static Window get_active_window(Display *disp)
{
    Atom a = XInternAtom(disp, "_NET_ACTIVE_WINDOW", False);
    Atom r;

    int format;
    unsigned long n, extra;
    unsigned char *data = 0;

    XGetWindowProperty(disp, XDefaultRootWindow(disp), a, 0, ~0, False,
                       AnyPropertyType, &r, &format, &n, &extra,
                       &data);

    if (data) {
        Window ret = *((Window *)data);
        XFree(data);
        return ret;
    }

    return 0;
}

static void *window_read(void *arg)
{
    OMXH264_decoder *decoder = (OMXH264_decoder *)arg;
    static GC gc = None;

    if (gc == None) {
        gc = XCreateGC(decoder->disp, DefaultRootWindow(decoder->disp), 0, 0);
        XSetForeground(decoder->disp, gc, 0x000000);
    }    

    for (;;) {
        if (decoder->terminate_readers) {
            /* Done. */
            break;
        }

        if (decoder->ica_parent) {
            Window active_window = get_active_window(decoder->disp);

            if (active_window != decoder->ica_parent) {
                if (!window_hidden) {
                    hide_egl_display(decoder);
                    window_hidden = TRUE;
                }
                XFillRectangle(decoder->disp, decoder->ica_window, gc, 0, 0, decoder->width, decoder->height);
            } else {
                window_hidden = FALSE;
                /* Force show the window. */
                move_egl_display(decoder, TRUE);
            }

            if (!window_hidden) {
                /* Check if the display needs moving. */
                move_egl_display(decoder, FALSE);
            }
        }

        usleep(WINDOW_READ_TIME_MS * 1000);
    }

    return 0;
}

static void *mouse_read(void *arg)
{
    OMXH264_decoder *decoder = (OMXH264_decoder *)arg;
    BOOL recreate = FALSE;    

    decoder->mouse_fd = open("/dev/input/mouse0", O_RDWR);

    if (-1 != decoder->mouse_fd) {
        fd_set set;

        for (;;) {
            FD_ZERO(&set);
            FD_SET(decoder->mouse_fd, &set);

            select(decoder->mouse_fd + 1, &set, NULL, NULL, NULL);

            if (FD_ISSET(decoder->mouse_fd, &set)) {
                /* Mouse cursor has moved or been clicked. */
                if (decoder->terminate_readers) {
                    /* Done. */
                    break;
                }

                static unsigned char waste[256];
                read(decoder->mouse_fd, waste, sizeof(waste));

                OMXH264_cursor *vars = &(decoder->cursor);

                /* Update our pointer position from X. */
                Window rr, cr;
                int x, y, win_x, win_y;
                unsigned int mr;
                
                XQueryPointer(decoder->disp, DefaultRootWindow(decoder->disp), &rr, &cr, &x, &y, &win_x, &win_y, &mr);
                if (vars->lx != x || vars->ly != y) {
                    vars->lx = x;
                    vars->ly = y;

                    int d_x = vars->lx - vars->xhot;
                    int d_y = vars->ly - vars->yhot;
                    
                    /* Check if the cursor shape has changed. */
                    XFixesCursorImage *new_cursor = XFixesGetCursorImage(decoder->disp);
                    if (new_cursor) {
                        if (!vars->X_cur || (new_cursor->cursor_serial != vars->X_cur->cursor_serial) || recreate) {
                            /* New cursor. Free existing cursor. */
                            if (vars->X_cur) {
                                XFree(vars->X_cur);
                            }
                            vars->X_cur = new_cursor;
                            /* Re-create. */
                            create_dispmanx_cursor(decoder, new_cursor);
                        } else {
                            XFree(new_cursor);
                        }
                    }
      
                    if (vars->image) {
                        VC_RECT_T dst;

                        vc_dispmanx_rect_set(&dst, d_x, d_y, vars->width, vars->height);

                        DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(10);
                        vc_dispmanx_element_change_attributes(update, vars->element, 1 << 2, 0, 0, &dst, NULL, 0, 0);
                        vc_dispmanx_update_submit_sync(update);
                    }

                    recreate = d_x < 0 || d_y < 0;
                }
            }
        }
        close(decoder->mouse_fd);
    }

    return 0;
}

void init_ogl(OMXH264_decoder *decoder)
{
    EGLBoolean result;
    EGLint num_config;

    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;

    static const EGLint attribute_list[] = {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 16,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
    };

    EGLConfig config;

    decoder->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    result = eglInitialize(decoder->display, NULL, NULL);
    if (EGL_FALSE == result) {
        printf("Couldn't initialize EGL display.\n");
        exit(1);
    }

    result = eglSaneChooseConfigBRCM(decoder->display, attribute_list, &config, 1, &num_config);
    if (EGL_FALSE == result) {
        printf("Couldn't find appropriate config.\n");
        exit(1);
    }

    decoder->context = eglCreateContext(decoder->display, config, EGL_NO_CONTEXT, NULL);
    if (decoder->context == EGL_NO_CONTEXT) {
        printf("Couldn't create EGL context.\n");
        exit(1);
    }

    dst_rect.x = decoder->dest_x;
    dst_rect.y = decoder->dest_y;
    dst_rect.width = decoder->width;
    dst_rect.height = decoder->height;
      
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = decoder->width << 16;
    src_rect.height = decoder->height << 16;        

    decoder->dispman_display = vc_dispmanx_display_open(0);
    decoder->dispman_update = vc_dispmanx_update_start(0);
         
    decoder->dispman_element = vc_dispmanx_element_add(decoder->dispman_update, 
                                    decoder->dispman_display,
                                    0, 
                                    &dst_rect, 
                                    0, 
                                    &src_rect, 
                                    DISPMANX_PROTECTION_NONE, 
                                    0, 
                                    0, 
                                    0);
      
    decoder->nativewindow.element = decoder->dispman_element;
    decoder->nativewindow.width = decoder->width;
    decoder->nativewindow.height = decoder->height;
    vc_dispmanx_update_submit_sync(decoder->dispman_update);
      
    decoder->surface = eglCreateWindowSurface(decoder->display, config, &decoder->nativewindow, NULL);
    if (decoder->surface == EGL_NO_SURFACE) {
        printf("Couldn't create EGL surface, err=%d.\n", eglGetError());
        exit(1);
    }

    result = eglMakeCurrent(decoder->display, decoder->surface, decoder->surface, decoder->context);
    if (EGL_FALSE == result) {
        printf("Couldn't bind context.\n");
        exit(1);
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_BYTE, 0, quadx);

    glGenTextures(1, &decoder->tex);
    glBindTexture(GL_TEXTURE_2D, decoder->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, decoder->width, decoder->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    /* Create EGL Image */
    decoder->egl_image = eglCreateImageKHR(decoder->display, decoder->context, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)decoder->tex, 0);

    if (!decoder->egl_image) {
        printf("Couldn't create EGL image.\n");
        exit(1);
    }

    glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_2D);

#ifdef WATERMARK
    create_watermark(decoder);
#endif

    /* Create mouse tracker. */
    pthread_create(&decoder->mouse_reader, 0, mouse_read, (void *)decoder);

    /* Create window tracker. */
    pthread_create(&decoder->window_reader, 0, window_read, (void *)decoder);
}

void deinit_ogl(OMXH264_decoder *decoder)
{
    if (decoder) {
        decoder->terminate_readers = 1;

        /* Shutdown mouse reader. */
        if (decoder->mouse_reader != (pthread_t)0) {
            static unsigned char tmp = 1;

            /* Write a byte to the mouse fd as a signal. */
            write(decoder->mouse_fd, &tmp, sizeof(tmp));
            /* Wait for termination. */
            pthread_join(decoder->mouse_reader, NULL);
        }

        if (decoder->window_reader != (pthread_t)0) {
            /* Wait for termination. */
            pthread_join(decoder->window_reader, NULL);
        }

        {
            OMXH264_cursor *vars = &(decoder->cursor);

            /* Remove cursor. */
            if (vars && vars->image) {
                free(vars->image);
                vars->image = NULL;

                DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
                vc_dispmanx_element_remove(update, vars->element);
                vc_dispmanx_update_submit_sync(update);
                vc_dispmanx_resource_delete(vars->resource);
            }
        }

#ifdef WATERMARK
        {
            OMXH264_watermark *vars = &(decoder->watermark);

            /* Remove watermark. */
            if (vars) {
                DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
                vc_dispmanx_element_remove(update, vars->element);
                vc_dispmanx_update_submit_sync(update);
                vc_dispmanx_resource_delete(vars->resource);
            }
        }
#endif

        /* Destroy image. */
        if (decoder->egl_image) {
            glDeleteTextures(1, &decoder->tex);
            eglDestroyImageKHR(decoder->display, decoder->egl_image);

            eglMakeCurrent(decoder->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroySurface(decoder->display, decoder->surface);
            eglDestroyContext(decoder->display, decoder->context);
            eglTerminate(decoder->display);
            decoder->egl_image = NULL;
        }

        DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);         
        vc_dispmanx_element_remove(update, decoder->dispman_element);
        vc_dispmanx_update_submit_sync(update);
        vc_dispmanx_display_close(decoder->dispman_display);
    }
}

