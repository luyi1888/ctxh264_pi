/***************************************************************************
*
*   video_gl.c
*
*   H264_decode.h implementation for Raspberry Pi.
*
*   Author: Muhammad Dawood (muhammad.dawood@citrix.com)
*   Copyright 2013-2014 Citrix Systems, Inc.  All Rights Reserved.
*
****************************************************************************/

#include <stdarg.h>
#include "video_gl.h"

#define TRACING_ENABLED

struct H264_decoder	H264_decoder = {
    VERSION_MAJOR,
    VERSION_MINOR,
    1,                 /* 1 context supported. */
    1920,
    1080,
    60,
    0, 
    H264_CHROMA_FORMAT_444,
    0,                 /* Preferred alpha value for lossless objects. */
    PIXEL_FORMAT_ARGB, /* Preferred pixel format for lossless objects. */
    &v3_init,
    &v3_open_context,
    &v3_start_frame,
    &v3_decode_frame,
    &v3_compose_with_fb,
    &v3_compose_with_rects,
    &v3_push_frame,
    &v3_close_context,
    &v3_end,
};

OMXH264_decoder *hw_decoder = NULL;

/* All exported by the main process. */
extern Display *GetICADisplay();
extern BOOL TwiModeEnableFlag;  /* Seamless enabled? */

pthread_cond_t fill_buffer_done_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t fill_buffer_done_mutex = PTHREAD_MUTEX_INITIALIZER;
int fill_buffer_done_val = 0;

void DEBUG_TRACE(const char *format, ...)
{
#ifdef TRACING_ENABLED
    char buf[1024];
    va_list args;

    va_start(args, format);
    vsprintf(buf, format, args);
    va_end(args);

    printf("%s", buf);
#endif
}

int port_settings_changed(OMXH264_decoder *decoder, int again)
{
    OMX_PARAM_PORTDEFINITIONTYPE portdef;

    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = decoder->image_decode->out_port;
    OMX_GetParameter(decoder->image_decode->handle, OMX_IndexParamPortDefinition, &portdef);

    DEBUG_TRACE("Got port settings width=%d, height=%d, again=%d\n", decoder->width, decoder->height, again);

    if (decoder->egl_render) {
        DEBUG_TRACE("EGL port settings changed\n");
        /* We're using EGL rendering. */

        ilclient_change_component_state(decoder->egl_render->component, OMX_StateIdle);

        if (ilclient_setup_tunnel(decoder->tunnel, 0, 0) != 0) {
            DEBUG_TRACE("Failed to setup tunnel\n");
            exit(1);
        }

        if (OMX_SendCommand(decoder->egl_render->handle, OMX_CommandPortEnable, decoder->egl_render->out_port, NULL) != OMX_ErrorNone) {
           DEBUG_TRACE("Couldn't enable egl_render port.\n");
        }

        if (OMX_UseEGLImage(decoder->egl_render->handle, &decoder->outbuf, decoder->egl_render->out_port, NULL, decoder->egl_image) != OMX_ErrorNone) {
           DEBUG_TRACE("Couldn't use EGL image.\n");
        }

        ilclient_change_component_state(decoder->egl_render->component, OMX_StateExecuting);

    } else if (decoder->image_resize) {
        DEBUG_TRACE("RESIZE port settings changed\n");
        /* Configure resizer. */
        int ret;

        if (again) {
            DEBUG_TRACE("Port settings changed again...\n");
            /* Disable ports. */
            OMX_SendCommand(decoder->image_decode->handle, OMX_CommandPortDisable, decoder->image_decode->out_port, NULL);
            OMX_SendCommand(decoder->image_resize->handle, OMX_CommandPortDisable, decoder->image_resize->in_port, NULL);
        } else {
            DEBUG_TRACE("Port settings changed...\n");
        }
        portdef.nPortIndex = decoder->image_resize->in_port;
        OMX_SetParameter(decoder->image_resize->handle, OMX_IndexParamPortDefinition, &portdef);

        /* Enable ports. */
        OMX_SendCommand(decoder->image_decode->handle, OMX_CommandPortEnable, decoder->image_decode->out_port, NULL);
        OMX_SendCommand(decoder->image_resize->handle, OMX_CommandPortEnable, decoder->image_resize->in_port, NULL);
    
        if (!again) {
            /* Put resizer in idle state (this allows the out_port of the decoder to become enabled) */
            OMX_SendCommand(decoder->image_resize->handle, OMX_CommandStateSet, OMX_StateIdle, NULL);
        }

        /* Output port must be disabled before setting parameters. */
        ret = OMX_SendCommand(decoder->image_resize->handle, OMX_CommandPortDisable, decoder->image_resize->out_port, NULL);

        if (ret != OMX_ErrorNone) {
           DEBUG_TRACE("Error disabling resize out port, ret=0x%x\n", ret);
        }

        portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
        portdef.nVersion.nVersion = OMX_VERSION;
        portdef.nPortIndex = decoder->image_resize->out_port;
        OMX_GetParameter(decoder->image_resize->handle, OMX_IndexParamPortDefinition, &portdef);

        portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
        portdef.format.image.eColorFormat = OMX_COLOR_Format32bitABGR8888;
        portdef.format.image.nFrameWidth = decoder->width;
        portdef.format.image.nFrameHeight = decoder->height;
        portdef.format.image.nStride = 0;
        portdef.format.image.nSliceHeight = 0;
        portdef.format.image.bFlagErrorConcealment = OMX_FALSE;

        ret = OMX_SetParameter(decoder->image_resize->handle, OMX_IndexParamPortDefinition, &portdef);
        if (ret != OMX_ErrorNone) {
            DEBUG_TRACE("Couldn't set new port settings, error=0x%x\n", ret);
        }

        if (ilclient_setup_tunnel(decoder->tunnel, 0, 0) != 0) {
            DEBUG_TRACE("Failed to setup tunnel\n");
            exit(1);
        }

        OMX_GetParameter(decoder->image_resize->handle, OMX_IndexParamPortDefinition, &portdef);

        /* Save the stride (in bytes). */
        decoder->stride = portdef.format.image.nStride;

        /* Set executing. */
        OMX_SendCommand(decoder->image_resize->handle, OMX_CommandStateSet, OMX_StateExecuting, NULL);

        /* Enable output port. */
        OMX_SendCommand(decoder->image_resize->handle, OMX_CommandPortEnable, decoder->image_resize->out_port, NULL);

        decoder->output_buffer = hw_decoder->fb->data;

        ret = OMX_UseBuffer(decoder->image_resize->handle, &decoder->outbuf, decoder->image_resize->out_port, NULL, portdef.nBufferSize, decoder->output_buffer);
    }

    hw_decoder->renderer_init = 1;

    DEBUG_TRACE("Port settings changed done\n");

    return 0;
}

comp_details *init_component(OMXH264_decoder *decoder, char *name, unsigned int extra_flags, int type)
{
    comp_details *comp = malloc(sizeof(comp_details));

    ilclient_create_component(decoder->client, &(comp->component), name, extra_flags);

    comp->handle = ILC_GET_HANDLE(comp->component);

    if (strcmp(name, "egl_render") == 0) {
        /* TODO: fix. */
        comp->in_port = 220;
        comp->out_port = 221;
    } else {
        OMX_PORT_PARAM_TYPE port;
        port.nSize = sizeof(OMX_PORT_PARAM_TYPE);
        port.nVersion.nVersion = OMX_VERSION;

        OMX_GetParameter(comp->handle, type, &port);

        comp->in_port = port.nStartPortNumber;
        comp->out_port = port.nStartPortNumber + 1;
    }

    DEBUG_TRACE("Component %s, in_port=%d, out_port=%d\n", name, comp->in_port, comp->out_port);

    return comp;
}

void fill_buffer_done(void* data, COMPONENT_T* comp)
{
    /* Signal complete event. */
    pthread_mutex_lock(&fill_buffer_done_mutex);
    fill_buffer_done_val = 1;
    pthread_cond_signal(&fill_buffer_done_cond);
    pthread_mutex_unlock(&fill_buffer_done_mutex);
}

BOOL setup_decoder()
{
    if (hw_decoder != NULL) {
        return FALSE;
    }

    hw_decoder = malloc(sizeof(OMXH264_decoder));

    if (!hw_decoder) {
        DEBUG_TRACE("Couldn't allocate decoder structure.\n");
        return FALSE;
    }

    memset(hw_decoder->tunnel, 0, sizeof(hw_decoder->tunnel));

    OMX_Init();
    
    hw_decoder->client = ilclient_init();

    ilclient_set_fill_buffer_done_callback(hw_decoder->client, fill_buffer_done, hw_decoder);

    hw_decoder->image_decode = init_component(hw_decoder, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS, OMX_IndexParamVideoInit);
    if (!hw_decoder->image_decode) {
        goto error;
    }

    /* Initialize variables. */
    hw_decoder->disp = GetICADisplay();
    hw_decoder->scr = DefaultScreenOfDisplay(hw_decoder->disp);
    hw_decoder->output_buffer = NULL;
    hw_decoder->egl_image = NULL;
    hw_decoder->dest_x = 0;
    hw_decoder->dest_y = 0;
    hw_decoder->ica_window = (Window)0;
    hw_decoder->ica_parent = (Window)0;
    hw_decoder->window_reader = (pthread_t)0;
    hw_decoder->mouse_reader = (pthread_t)0;
    hw_decoder->mouse_fd = -1;
    hw_decoder->terminate_readers = 0;
    hw_decoder->cursor.X_cur = NULL;
    hw_decoder->cursor.image = NULL;
    hw_decoder->cursor.lx = hw_decoder->cursor.ly = 0;
    hw_decoder->fb = 0;
    hw_decoder->size = 0;
    hw_decoder->old_ptr = NULL;
    hw_decoder->egl_render = NULL;
    hw_decoder->image_resize = NULL;
    hw_decoder->renderer_init = 0;

    /* If we're in seamless, do not use EGL rendering. */
    comp_details **comp_out = TwiModeEnableFlag ? &(hw_decoder->image_resize) : &(hw_decoder->egl_render);

    *comp_out = init_component(hw_decoder, 
                               TwiModeEnableFlag ? "resize" : "egl_render", 
                               ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_OUTPUT_BUFFERS, 
                               OMX_IndexParamImageInit);
    if (!(*comp_out)) {
        goto error;
    }

    set_tunnel(hw_decoder->tunnel, hw_decoder->image_decode->component, hw_decoder->image_decode->out_port, (*comp_out)->component, (*comp_out)->in_port);

    memset(&hw_decoder->shm_info, 0, sizeof(hw_decoder->shm_info));

    ilclient_change_component_state(hw_decoder->image_decode->component, OMX_StateIdle);

    /* Set port format. */
    OMX_VIDEO_PARAM_PORTFORMATTYPE format = {0};
    format.nSize = sizeof(format);
    format.nVersion.nVersion = OMX_VERSION;
    format.nPortIndex = hw_decoder->image_decode->in_port;
    format.eCompressionFormat = OMX_VIDEO_CodingAVC;
    OMX_SetParameter(hw_decoder->image_decode->handle, OMX_IndexParamVideoPortFormat, &format);

    ilclient_enable_port_buffers(hw_decoder->image_decode->component, hw_decoder->image_decode->in_port, NULL, NULL, NULL);

    ilclient_change_component_state(hw_decoder->image_decode->component, OMX_StateExecuting);

    return TRUE;

error:
    DEBUG_TRACE("Error setting up decoder.\n");
	ilclient_destroy(hw_decoder->client);
    free(hw_decoder);
    return FALSE;
}

static void close_decoder()
{
    if (hw_decoder) {
        COMPONENT_T *components[3] = {0};
        
        components[0] = hw_decoder->image_decode->component;
        
        if (hw_decoder->egl_render) {
            /* Deinit EGL if we've been using it. */
            deinit_ogl(hw_decoder);
            components[1] = hw_decoder->egl_render->component;
        } else if (hw_decoder->image_resize) {
            if (-1 != hw_decoder->shm_info.shmid) {
                XShmDetach(hw_decoder->disp, &hw_decoder->shm_info);
                /* Free shared memory. */
                shmdt(hw_decoder->shm_info.shmaddr);
                shmctl(hw_decoder->shm_info.shmid, IPC_RMID, 0);
            }

            if (hw_decoder->fb) {
                hw_decoder->fb->data = hw_decoder->old_ptr;
                XDestroyImage(hw_decoder->fb);
            }
            components[1] = hw_decoder->image_resize->component;
        }

        ilclient_disable_tunnel(hw_decoder->tunnel);
        ilclient_teardown_tunnels(hw_decoder->tunnel);
 
        DEBUG_TRACE("Disabling port buffers\n");
        ilclient_disable_port_buffers(components[0], hw_decoder->image_decode->in_port, NULL, NULL, NULL);
        ilclient_disable_port_buffers(components[0], hw_decoder->image_decode->out_port, NULL, NULL, NULL);

        ilclient_state_transition(components, OMX_StateIdle);

        /* Destroy components. */
        ilclient_cleanup_components(components);
        
        if (hw_decoder->client) {
            ilclient_destroy(hw_decoder->client);
        }
    
        OMX_Deinit();

        free(hw_decoder);
        hw_decoder = NULL;
    }
}

int decode_frame(OMXH264_decoder *decoder, unsigned char *data, int size, int last)
{
    static OMX_BUFFERHEADERTYPE *buf = 0;
    static int p_s_c = 0;
    int ret;

grab_buffer:
    if (buf == 0) {
        buf = ilclient_get_input_buffer(decoder->image_decode->component, decoder->image_decode->in_port, 1);
        if (!buf) {
            DEBUG_TRACE("Couldn't get buffer\n");
            return -1;
        }
        buf->nFilledLen = 0;
        buf->nOffset = 0;
        buf->nFlags = 0;
    }

    int buf_left = buf->nAllocLen - buf->nFilledLen;
    int size_to_fill = size > buf_left ? buf_left : size;

    memcpy(buf->pBuffer + buf->nFilledLen, data, size_to_fill);
    buf->nFilledLen += size_to_fill;

    size -= size_to_fill;

    if (size > 0) {
        /* More to come, but the buffer is full. */
        ret = OMX_EmptyThisBuffer(decoder->image_decode->handle, buf);
        if (ret != OMX_ErrorNone) {
            DEBUG_TRACE("[0] Couldn't empty buffer, size=%d, ret=0x%x\n", size, ret);
            return -1;
        }

        /* Advance input pointer... */
        data += size_to_fill;

        /* ..and grab a new buffer. */
        buf = 0;
        goto grab_buffer;
    }

    if (!last) {
        /* All Input consumed. More data to come */
        return -1;
    }

    /* Done. */
    buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    ret = OMX_EmptyThisBuffer(decoder->image_decode->handle, buf);
    if (ret != OMX_ErrorNone) {
        DEBUG_TRACE("[1] Couldn't empty buffer, size=%d, ret=0x%x\n", size, ret);
        return -1;
    }
    /* Make sure we grab a buffer next time we come in. */
    buf = 0;
 
    if (p_s_c == 0) {
        /* Wait for p_s_c event. */
        if (0 == ilclient_wait_for_event(decoder->image_decode->component, OMX_EventPortSettingsChanged, decoder->image_decode->out_port, 0, 0, 1,
                                               ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 100)) {
            DEBUG_TRACE("Got port settings changed event.\n");
            port_settings_changed(decoder, 0);
            p_s_c = 1;
        }
    } else {
        if (0 == ilclient_remove_event(decoder->image_decode->component, OMX_EventPortSettingsChanged, decoder->image_decode->out_port, 0, 0, 1)) {
            /* Port settings changed again. */
            DEBUG_TRACE("Got port settings changed event, again!\n");
            port_settings_changed(decoder, 1);
        }
    }

    if (p_s_c == 1) {
        /* Got new port settings, start filling. */
        ret = OMX_FillThisBuffer((decoder->egl_render ? decoder->egl_render->handle : decoder->image_resize->handle), 
                                 decoder->outbuf);
    
        if (ret != OMX_ErrorNone) {
            DEBUG_TRACE("Error filling buffer, return code %x\n", ret);
        } else {
            /* Wait for fill_buffer_done. */
            pthread_mutex_lock(&fill_buffer_done_mutex);
            while (fill_buffer_done_val == 0) {
                pthread_cond_wait(&fill_buffer_done_cond, &fill_buffer_done_mutex);
            }
            fill_buffer_done_val = 0;
            pthread_mutex_unlock(&fill_buffer_done_mutex);

            return 0;
        }
    }

    return -1;
}

/* This function would be called only once, to initialize the DLL. */
bool v3_init()
{
    char *bcm_init = getenv("CTX_BCM_INIT");
    if (!bcm_init) {
        DEBUG_TRACE("Loading BCM init\n");
        if (!dlopen("bcm_init.so", RTLD_NOW)) {
            return 0;
        }
        setenv("CTX_BCM_INIT", "DONE", 1);
    }

    /* Check for at least 24-bit colour depth. This is done here as
     * returning "0" will result in Receiver falling back to
     * compatible (JPEG) mode.
     */

    /* Set up X11 display variables. */
    Display *disp = GetICADisplay(); /* Exported by the main Receiver process. */

    if (DefaultDepth(disp, DefaultScreen(disp)) < 24) {
        /* This decoder can't deal with anything other than 24 or 32-bit
         * displays.
         */
        return 0;
    }

    /* Defer decoder initialization until it's actually required. Indicate that
     * we support H.264.
     */
    return 1;
}

void v3_end ()
{
    DEBUG_TRACE("V3_END, pthread=0x%x\n", pthread_self());
    close_decoder();
}

H264_context v3_open_context(int width, int height, void* codec_data, int len, unsigned int options)
{
    DEBUG_TRACE("V3_OPEN, pthread=0x%x\n", pthread_self());
    static int id = 1;

    /* Close the existing context if it exists. */
    close_decoder();

    /* Set up decoder and create context. */
    if (!setup_decoder()) {
        /* Couldn't set up decoder. */
        return H264_INVALID_CONTEXT;
    }

    if (hw_decoder) {
        /* Set width and height here. */
        hw_decoder->width = width;
        hw_decoder->height = height;

        if (hw_decoder->egl_render) {
            /* Ensure that EGL is initialized on the same thread.
             * Not doing so could result in resources not being deallocated.
             */
            init_ogl(hw_decoder);
        } else if (hw_decoder->image_resize) {
            /* Seamless. */
            int major, minor;
            Bool pixmaps;
            
            /* Try to use MIT-SHM. */
            BOOL g_using_shm = XShmQueryExtension(hw_decoder->disp) &&
                          XShmQueryVersion(hw_decoder->disp, &major, &minor, &pixmaps);

            DEBUG_TRACE("shm=%d\n", g_using_shm);

            if (g_using_shm) {
                /* Use MIT-SHM. */
                hw_decoder->fb = XShmCreateImage(hw_decoder->disp, DefaultVisualOfScreen(hw_decoder->scr),
                          DefaultDepthOfScreen(hw_decoder->scr), ZPixmap, NULL, &hw_decoder->shm_info,
                          width, height);
                if (hw_decoder->fb) {
                    hw_decoder->size = hw_decoder->fb->bytes_per_line * height;
                }
            } else {
                /* Use XPutImage. */
                hw_decoder->fb = XCreateImage(hw_decoder->disp, DefaultVisualOfScreen(hw_decoder->scr),
                          DefaultDepthOfScreen(hw_decoder->scr), ZPixmap, 0, NULL, width, height,
                          32, 0);
                if (hw_decoder->fb) {
                    hw_decoder->size = hw_decoder->fb->bytes_per_line * height;
                }
            }

            if (hw_decoder->size > 0) {
                hw_decoder->shm_info.shmid = -1;
                if (g_using_shm) {
                    /* Allocated shared memory, aligned on a 16 byte boundary. */
                    hw_decoder->shm_info.shmseg = None;
                    hw_decoder->shm_info.readOnly = 0;
                    hw_decoder->shm_info.shmid = shmget(IPC_PRIVATE, hw_decoder->size + 32, IPC_CREAT | 0600);
                    hw_decoder->shm_info.shmaddr = (char *)shmat(hw_decoder->shm_info.shmid, 0, 0);

                    /* Tell the X server to attach the segment.
                     */
                    if (XShmAttach(hw_decoder->disp, &hw_decoder->shm_info) > 0) {
                        hw_decoder->fb->data = (char *)hw_decoder->shm_info.shmaddr;
                        /* Mark the shared memory for removal now, so that it does
                         * not remain if this program dies unexpectedly.
                         */
                        shmctl(hw_decoder->shm_info.shmid, IPC_RMID, 0);
                    } else {                    
                        /* Error - free shared memory. */
                        shmdt(hw_decoder->shm_info.shmaddr);
                        shmctl(hw_decoder->shm_info.shmid, IPC_RMID, 0);
                        hw_decoder->shm_info.shmid = -1;
                    }
                }

                if (-1 == hw_decoder->shm_info.shmid) {
                    /* Use traditional memory for XPutImage(). This memory is freed
                     * by XDestroyImage();
                     */
                    hw_decoder->fb->data = (char *)malloc(hw_decoder->size + 32);
                    hw_decoder->old_ptr = (void *)hw_decoder->fb->data;
                }
                /* Align. */
                hw_decoder->fb->data = (char *)(((unsigned int)(hw_decoder->fb->data) + 15) & ~0x0F);
            }
        }

        return id++;
    }

	return H264_INVALID_CONTEXT;
}

void v3_close_context(H264_context Ctx)
{
    DEBUG_TRACE("V3_CLOSE, pthread=0x%x\n", pthread_self());
    close_decoder();
}

bool v3_start_frame(H264_context Ctx, unsigned int encoded_size, SIGNED_RECT dirty_rects[], unsigned int num_rects)
{
    unsigned int i;

    /* Save the dirty rects for this frame. */
    if (num_rects > 0) {
        for (i = 0; i < num_rects; i++) {
            hw_decoder->dirty_rects[i] = dirty_rects[i];        
        }
        hw_decoder->num_rects = num_rects;
    } else {
        /* No dirty rect means entire context needs updating. */
        hw_decoder->dirty_rects[0].left = 0;
        hw_decoder->dirty_rects[0].top = 0;
        hw_decoder->dirty_rects[0].right = hw_decoder->width;
        hw_decoder->dirty_rects[0].bottom = hw_decoder->height;
        hw_decoder->num_rects = 1;
    }

	return 1;
}

bool v3_decode_frame(H264_context Ctx, void* H264_data, int len, bool last)
{
    decode_frame(hw_decoder, H264_data, len, last);

	return 1;
}

bool v3_compose_with_fb(H264_context Ctx, struct image_buf *fb, SIGNED_RECT interesting_rects[], unsigned int num_rects)
{
	return 1;
}

bool v3_compose_with_rects(H264_context Ctx, struct image_buf rects[], unsigned int num_rects, bool last)
{
	return 1;
}

static bool intersects(SIGNED_RECT *r1, SIGNED_RECT *r2, SIGNED_RECT *r3)
{
    r3->top = max(r1->top, r2->top);
    r3->bottom = min(r1->bottom, r2->bottom);
    if (r3->top >= r3->bottom) {
        return 0;
    }

    r3->left = max(r1->left, r2->left);
    r3->right = min(r1->right, r2->right);
    if (r3->left >= r3->right) {
        return 0;
    }

    return 1;
}

bool v3_push_frame(H264_context Ctx, struct window_info windows[], unsigned int num_windows, bool wait, bool *pushed)
{
    if (hw_decoder->egl_render) {
        /* Non-seamless rendering. */
        if ((1 == num_windows) && (0 == hw_decoder->ica_window)) {
            Window root, *child, temp; 
            unsigned int n = 0;

            hw_decoder->ica_window = (Window)windows[0].id;

            /* Save window and move EGL texture. */
            XQueryTree(hw_decoder->disp, (Window)windows[0].id, &root, &temp, &child, &n);
            if (n) {
                XFree(child);
                n = 0;
            }

            /* Grab the parent window for tracking purposes. */
            XQueryTree(hw_decoder->disp, temp, &root, &hw_decoder->ica_parent, &child, &n);
            if (n) {
                XFree(child);
                n = 0;
            }

            move_egl_display(hw_decoder, TRUE);
        }

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        eglSwapBuffers(hw_decoder->display, hw_decoder->surface);

    } else if (hw_decoder->image_resize) {

        static GC gc = None;
        unsigned int i;
        int j;

        if (gc == None) {
            /* Create a re-usable graphics context. */
            gc = XCreateGC(hw_decoder->disp, DefaultRootWindow(hw_decoder->disp), 0, 0);
        }

        /* Show composed frame buffer. We must work out, based on the dirty rects.
         * what portions of the window(s) require updating.
         */

        for (i = 0; i < num_windows; i++) {
	        Window X_window = windows[i].id;
            SIGNED_RECT window_rect = windows[i].rect;

            /* Check if any of the dirty rects lie within this window. */
            for (j = 0; j < hw_decoder->num_rects; j++) {
                SIGNED_RECT dirty_rect = hw_decoder->dirty_rects[j];
                SIGNED_RECT overlap;

                if (intersects(&window_rect, &dirty_rect, &overlap)) {
                    int width, height, src_x, src_y, dest_x, dest_y;

                    if (-1 != hw_decoder->shm_info.shmid) {
                        /* No special alignment required. */
                        width = overlap.right - overlap.left;
                        height = overlap.bottom - overlap.top;

                        src_x = overlap.left;
                        src_y = overlap.top;

                        dest_x = windows[i].target_x + overlap.left;
                        dest_y = windows[i].target_y + overlap.top;

                        XShmPutImage(hw_decoder->disp, 
                                X_window, gc, hw_decoder->fb,
                                src_x,
                                src_y,
                                dest_x,
                                dest_y,
                                width, 
                                height, 
                                1);
                    } else {
                        /* XPutImage - no special alignment required. */
                        width = overlap.right - overlap.left;
                        height = overlap.bottom - overlap.top;

                        src_x = overlap.left;
                        src_y = overlap.top;

                        dest_x = windows[i].target_x + overlap.left;
                        dest_y = windows[i].target_y + overlap.top;

                        XPutImage(hw_decoder->disp, 
                                X_window, gc, hw_decoder->fb,
                                src_x,
                                src_y,
                                dest_x,
                                dest_y,
                                width, 
                                height);
                        /* No need to wait for syncrhonization with XPutImage(). */
                    }
                }
            }
     	}
    }

    if (pushed) {
        *pushed = 1;
    }

	return 1;
}
