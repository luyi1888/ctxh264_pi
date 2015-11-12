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

    if (decoder->video_render) {
        DEBUG_TRACE("video_render port settings changed\n");
        /* We're using video_render rendering. */

        ilclient_change_component_state(decoder->video_render->component, OMX_StateIdle);

        if (ilclient_setup_tunnel(decoder->tunnel, 0, 0) != 0) {
            DEBUG_TRACE("Failed to setup tunnel\n");
            exit(1);
        }

        ilclient_change_component_state(decoder->video_render->component, OMX_StateExecuting);

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

    if (strcmp(name, "video_render") == 0) {
        /* TODO: fix. */
        comp->in_port = 90;
        comp->out_port = NULL;
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
    hw_decoder->video_render = NULL;
    hw_decoder->renderer_init = 0;

    comp_details **comp_out = &(hw_decoder->video_render);

    *comp_out = init_component(hw_decoder, 
                               "video_render", 
                               ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_OUTPUT_BUFFERS, 
                               OMX_IndexParamImageInit);
    if (!(*comp_out)) {
        goto error;
    }

    set_tunnel(hw_decoder->tunnel, hw_decoder->image_decode->component, hw_decoder->image_decode->out_port, (*comp_out)->component, (*comp_out)->in_port);

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
        
        if (hw_decoder->video_render) {
            components[1] = hw_decoder->video_render->component;
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

    // if (p_s_c == 1) {
    //     ret = OMX_FillThisBuffer(decoder->video_render->handle, 
    //                              decoder->outbuf);
    
    //     if (ret != OMX_ErrorNone) {
    //         DEBUG_TRACE("Error filling buffer, return code %x\n", ret);
    //     } else {
    //         pthread_mutex_lock(&fill_buffer_done_mutex);
    //         while (fill_buffer_done_val == 0) {
    //             pthread_cond_wait(&fill_buffer_done_cond, &fill_buffer_done_mutex);
    //         }
    //         fill_buffer_done_val = 0;
    //         pthread_mutex_unlock(&fill_buffer_done_mutex);

    //         return 0;
    //     }
    // }
    if (p_s_c == 1)
    {
        return 0;
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

    if(TwiModeEnableFlag)
    {
        DEBUG_TRACE("Do not supported TwiMode yet\n");
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

bool v3_push_frame(H264_context Ctx, struct window_info windows[], unsigned int num_windows, bool wait, bool *pushed)
{
    if (pushed) {
        *pushed = 1;
    }

	return 1;
}
