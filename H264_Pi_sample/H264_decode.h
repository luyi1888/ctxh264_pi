/******************************************************************************
 *
 *   H264_decode.h
 *
 *   Citrix Receiver interface to H.264 decoders.
 *
 *   $Id: //icaclient/develop/main/UKH/NetClient/main/unix/inc/H264_decode.h#7 $
 *
 *   Copyright 2013-2014 Citrix Systems, Inc.  All Rights Reserved.
 *
 ********************************************************************************/

#ifndef _H264_DECODE_H_
#define _H264_DECODE_H_

/*  The purpose of this interface is to allow 3rd party H.264 decoders to be
 *  "plugged" into Citrix Receiver, taking advantage of hardware acceleration
 *  or other decoding optimisations. Receiver will receive H.264 encoded data
 *  on a per-frame basis and issue this to the decoder in parts. An ARGB frame
 *  buffer will be issued to the decoder immediately after the H.264 data. This
 *  will be used for drawing parts of the screen, such as text, in a "lossless"
 *  manner. The interface implementation is expected to merge this with the
 *  current H.264 frame (essentially, an overlay) such that no detail be lost
 *  from the ARGB frame buffer. Receiver will then instruct the interface to
 *  push the composed frame to screen, and the process repeats.
 *
 *  In essence, the sequence of API calls will usually be:
 *
 *  init()
 *  ...
 *  cxt = open_context()
 *  ...
 *
 *  repeat
 *
 *    start_frame( cxt )
 *
 *    while ( more_h264_frame_data )            }
 *                                              }
 *      decode_frame( cxt, h264_frame_chunk )   } - (*)see below
 *                                              }
 *    end                                       }
 *
 *    compose_with_fb( cxt, ARGB_buffer ) / compose_with_rects( cxt, rects )
 *
 *    push_frame( cxt )
 *
 *  end
 *
 *  ...
 *  close_context( cxt )
 *
 *  The interface is free to decide how the ARGB merge should be done i.e.,
 *  whether to do this in hardware or software with the latter probably
 *  requiring a YUV->RGB H.264 frame buffer conversion at some point. This
 *  overlay should not be used for the construction of subsequent H.264 frames.
 *  Only the data that was previously H.264 decoded should be used for future
 *  H.264 frames.
 *
 *  Support for more than one H.264 decoding context may be required for cases
 *  where multiple monitors are in use.
 *
 *  (*)Note that a frame may not actually contain any H.264 frame data, with
 *  only lossless areas being updated. If this is the case, the lossless
 *  composition step should be performed with the last composed frame.
 *
 *  SMALL FRAME SUPPORT
 *  -------------------
 *
 *  The server might decide that encoding an entire screen's worth of data for
 *  small changes is ineffficient, and therefore send the frame as a collection
 *  of small lossless bitmaps. These are known as "small frames" and can be
 *  optionally supported by the decoder. The decoder can indicate support by
 *  setting the H264_OPTION_SMALL_FRAME_SUPPORT flag.
 *
 *  Small frame objects (bitmaps or solid fill commands) will either be
 *  pre-composed onto the ARGB frame buffer, or issued via the
 *  compose_with_rects() call, depending on whether the option
 *  "H264_OPTION_PREFER_TEXT_RECTS" is set. If this option is set, the decoder
 *  must retain the small frame objects until the next H.264 frame, where
 *  all small frame objects can be purged.
 */

/*  Interface version. */

#define VERSION_MAJOR  1
#define VERSION_MINOR  0

/*  Pixel formats for the (lossless) frame buffer or lossless objects that are
 *  passed to the decoder for composition.
 */

typedef enum _LLPixelFormat
{
    PIXEL_FORMAT_ARGB = 0x00,
    PIXEL_FORMAT_BGRA = 0x01
} LLPixelFormat;

/* gcc allows bool as a keyword in C. */
#ifndef __cplusplus

#ifndef bool
#define	bool	unsigned char
#endif /* bool */

#endif /* __cplusplus */


/* A structure to describe some image data, be it a frame buffer, individual
 * lossless text rectangles, small frame bitmaps or solid fills.
 *
 * In the latter 3 cases, Receiver will choose 1 of 4 possible operations:
 *
 *  IMAGE_OP_DRAW_LOSSLESS: draw supplied lossless text at specified
 *  coordinates, using specified width and height. A pointer to the image data
 *  will be given via the "bits" parameter. The destination coordinates, width
 *  and height can be used to index the text rectangle, for example, in a
 *  database-type system.
 *
 *  IMAGE_OP_DELETE_LOSSLESS: delete lossless text at specified coordinates,
 *  using specified width and height. Note that "delete" does not necessarily
 *  require data to be fully removed, for example, this operation may be
 *  implemented as an "alpha-zerorer" i.e., set all alpha values in the
 *  destination rectangle to zero. Alternatively, the destination coordinates,
 *  width and height can be used to locate the text rectangle, for example,
 *  in a database-type system.
 *
 *  IMAGE_OP_SMALL_FRAME_BITMAP: draw supplied bitmap at specified coordinates,
 *  using specified with and height. A pointer to the image data will be given
 *  via the "bits" parameter. The destination coordinates, width and height can
 *  be used to index the text rectangle, for example, in a database-type system.
 *  This operation is almost identical to IMAGE_OP_DRAW_LOSSLESS, the only
 *  difference being that it will never be "undone" by a corresponding delete.
 *
 *  IMAGE_OP_SMALL_FRAME_SOLID_FILL: draw a solid rectangle of colour specified
 *  by "col", using the specified destination coordinates, width and height.
 */

typedef enum _ImageOp
{
	IMAGE_OP_DRAW_LOSSLESS 		        = 0,
	IMAGE_OP_DELETE_LOSSLESS	        = 1,
    IMAGE_OP_SMALL_FRAME_BITMAP         = 2,
    IMAGE_OP_SMALL_FRAME_SOLID_FILL     = 3
} ImageOp;

struct image_buf
{
    unsigned int cb_size;       /* Size of this structure. */

    void *mem;                  /* Pointer to allocated memory. */

    void *bits;                 /* Pointer to start of image bits. */

    unsigned char pixel_format; /* Pixel format (see above). */

    unsigned char lossless_op;  /* Lossless operation (see above). */

    int stride;                 /* Stride (in bytes). Not necessarily width *
                                 * sizeof(pixel) if a specified alignment is
                                 * required.
                                 */

    unsigned int width;         /* Image width (pixels). */

    unsigned int height;        /* Image height (pixels). */

    int dst_x, dst_y;           /* The x and y coordinates of the upper-left
                                 * corner of the destination rectangle if
                                 * this image is being transferred onto a
                                 * surface.
                                 */

    int src_x, src_y;           /* The x and y coordinates of the upper-left
                                 * corner of the source rectangle if this
                                 * operation is a copy from source.
                                 */

    unsigned int col;           /* RGB colour for solid fills
                                 * (IMAGE_OP_SMALL_FRAME_SOLID_FILL)
                                 */
};

/*  A structure to provide information about windows to the interface. This
 *  is required for seamless and full-screen/windowed sessions and enables the
 *  decoder interface to update those windows with the correct areas of the
 *  composed H.264 frame.
 */

typedef enum _WindowInfoFlags
{
	WINDOW_INFO_FLAG_SEAMLESS   = 0x00000001,
} WindowInfoFlags;

struct window_info
{
    unsigned int cb_size;   /* Size of this structure. */

    unsigned int id;        /* Window manager specific ID (or handle). */

    SIGNED_RECT rect;       /* Window rect. This is used to determine where to
                             * draw the image from the composed H.264 frame
                             * as the source rectangle may differ from the
                             * window's position and size.
                             */
    int         target_x;   /* Target x-offset within context. */

    int         target_y;   /* Target y-offset within context. */

    unsigned int flags;     /* See above. */
};


typedef unsigned int H264_context;
static const H264_context H264_INVALID_CONTEXT          = 0;

/* If H264_OPTION_PREFER_TEXT_RECTS is specified by the interface, Receiver
 * will issue a list of lossless text rectangle data instead of an ARGB
 * frame buffer, for composition with the H.264 frame. Also, if small frame
 * support is enabled, and H264_OPTION_PREFER_TEXT_RECTS is specified, 
 * Receiver will issue the individual small frame bitmaps, similar to how
 * text rectangles are issued. Otherwise, small frame bitmaps will be
 * composed onto the same ARGB frame buffer as the lossless text rectangles.
 */

typedef enum _H264Option
{
	H264_OPTION_LOSSLESS			= 0x00000001,
	H264_OPTION_WINDOW_SUPPORT		= 0x00000002,
	H264_OPTION_PREFER_TEXT_RECTS	= 0x00000004,
    H264_OPTION_SMALL_FRAME_SUPPORT = 0x00000008
} H264Option;

typedef enum _ChromaFormat
{
	H264_CHROMA_FORMAT_400	= 0x00000001, /* Monochrome. */
	H264_CHROMA_FORMAT_420	= 0x00000002,
	H264_CHROMA_FORMAT_422  = 0x00000004,
	H264_CHROMA_FORMAT_444  = 0x00000008
} ChromaFormat;

/*  Decoders are loaded as a shared library containing an exported symbol,
 *  H264_decoder, that refers to a statically-allocated instance
 *  of this structure.
 */

struct H264_decoder
{
    /*  Must be set to VERSION_MAJOR (see above). */
    unsigned int ver_major;

    /*  Must be set to VERSION_MINOR (see above). */
    unsigned int ver_minor;

	/*  The maximum number of contexts the decoder supports. Specify "0"
     *  to indicate no limit.
     */
	int max_contexts;

    /*  The maximum width and height (in pixels) supported by a decoding
     *  context.
     */
	unsigned int width, height;

    /*  The maximum frame rate supported by a decoding context. */
	int max_fps;

    /*  Decoder specific options (see above). Used during negotiation
     *  of capabilities with the server.
     */
	unsigned int options;

    /*  Supported chroma formats (see above). Again, might be used during
     *  capability negotiation with the server.
     */
    unsigned int chroma_formats;

    /*  Preferred alpha value for lossless objects. Usually set to 0 or 255.
        For example, for a pixel format of ARGB, and a preferred alpha value
        of 255 (0xFF), the value is set per pixel as follows: FF RR GG BB.
        And for a pixel format of BGRA: BB GG RR FF.
    */
    unsigned char pref_lossless_alpha_val;

    /*  Preferred pixel format for lossless objects (not guaranteed). Should
     *  be set to a PixelFormat value (enum above).
     */
    unsigned char pref_lossless_pixel_fmt;

    /*  Function: init()
     *
     *  Perform any decoder specific initialisation. Called immediately after
     *  the deocding interface is loaded. Note that it is permitted for init()
     *  to switch implementations of the below functions at runtime if desired.
     *  For example, decode_frame might be implemented as:
     *
     *  bool decode_frame(...)
     *  {
     *      return (*pfn_decode_frame)(...);
     *  }
     *
     *  where "pfn_decode_frame()" is a pointer to a function which might vary
     *  based on some checks performed when init() is called.
     *
     *  Returns:
     *       true:              success, decoder will be used for H.264.
     *
     *       false:             failure, decoder cannot be used.
     */

    bool (*init)();


    /*  Function: open_context()
     *
     *  Create a new H.264 decoding context. The context returned should be
     *  used in subsequent calls. The "options" parameter is used to request
     *  particular features of the decoder that might be required throughout
     *  the lifetime of this context.
     *
     *  Input:
     *       width, height:     requested width and height (in pixels) of the
     *                          new context.
     *
     *       codec_data:        pointer to some H.264 specific codec data.
     *
     *       len:               length (in bytes) of codec data.
     *
     *       options:           flags indicating support for decoder features
     *                          (e.g., windowed drawing - see above).
     *
     *  Returns:
     *       > 0 (success):     a valid H264_context.
     *
     *       H264_INVALID_CONTEXT (error).
     */

    H264_context (*open_context)(int width, int height, void *codec_data,
                                 int len, unsigned int options);

    /*  Function: start_frame()
     *
     *  Initializes the decoding interface to begin decoding a new frame. The
     *  total encoded data size of the frame must be supplied to ensure that
     *  an entire H.264 encoded frame can be assembled from it's constituent
     *  parts. This is especially useful if the decoder does not support
     *  partial decoding.
     *
     *  The function may also be called with a number of "dirty" rectangles.
     *  These rectangles define which areas of the composed frame have actually
     *  changed, so that the implementation may take advantage of updating only
     *  those areas on screen, avoiding the need for a full screen update on
     *  each frame. If no rectangles are specified, then the implementation
     *  must assume that the whole context (monitor) has changed.
     *
     *  An encoded size of 0 means that the frame is a just a text frame or
     *  small frame.
     *
     *  Input:
     *       cxt:               handle to a valid H.264 context.
     *
     *       encoded_size:      encoded data size of the whole H.264 frame.
     *
     *       dirty_rects:       array of dirty rectangles (see above).
     *
     *       num_rects:         number of dirty rectangles, or 0.
     *
     *  Returns:
     *       true:              success, ready to receive frame data.
     *
     *       false:             error, unable to start decoding frame.
     */

    bool (*start_frame)(H264_context cxt, unsigned int encoded_size,
                        SIGNED_RECT dirty_rects[], unsigned int num_rects);


    /*  Function: decode_frame()
     *
     *  Accepts partial H.264 encoded data and decodes an entire frame once
     *  all the data is available. This is indicated to the decoder when
     *  the boolean "last" is set to "true". The input "H264_data" is
     *  NOT guaranteed to be valid once this function returns.
     *
     *  Input:
     *       cxt:               handle to a valid H.264 context.
     *
     *       H264_data:         partial/entire encoded H.264 frame data.
     *
     *       len:               length (in bytes) of H.264 frame data.
     *
     *       last:              boolean to indicate whether this frame data is
     *                          the last in a frame.
     *
     *  Returns:
     *       true:              H.264 data successfully consumed (last == false)
     *                          frame successfully decoded (last == true).
     *
     *       false:             invalid partial H.264 frame data (last == false)
     *                          unable to decode frame (last == true).
     */

    bool (*decode_frame)(H264_context cxt, void *H264_data, int len, bool last);


    /*  Function: compose_with_fb()
     *
     *  Instructs the decoding interface to compose a frame buffer with the
     *  current H.264 frame. The frame buffer is described by the various fields
     *  in a "image_buf" structure (see above).
     *
     *  A number of "interesting" rectangles may be passed to the function as
     *  a hint to the affected areas.
     *
     *  Any pixels in the supplied frame buffer that do not need to be
     *  displayed should be marked transparent, i.e., have an alpha value of
     *  0.
     *
     *  Input:
     *       cxt:               handle to a valid H.264 context.
     *
     *       fb:                frame buffer to compose with H.264 frame.
     *
     *       interesting_rects: an array of interesting rectangles indicating
     *                          affected areas.
     *
     *       num_rects:         number of rectangles, or 0 if the entire frame
     *                          buffer should be inspected.
     *
     *  Returns:
     *       true:              success.
     *
     *       false:             error.
     */

    bool (*compose_with_fb)(H264_context cxt, struct image_buf *fb,
                            SIGNED_RECT interesting_rects[], unsigned int num_rects);


    /*  Function: compose_with_rects()
     *
     *  Instructs the decoding interface to compose a list of text rectangles
     *  or small frame bitmaps with the current H.264 frame. Each object, including 
     *  it's destination location, is described by an "image_buf" structure.
     *
     *  This function may be called multiple times if Receiver is unable to
     *  issue all the rectangles in a single call. The terminating call will be
     *  indicated by setting the "last" parameter to TRUE.
     *
     *  Input:
     *       cxt:               handle to a valid H.264 context.
     *
     *       text_rects:        list of objects to compose.
     *
     *       num_rects:         number of rectangles, or 0 for none.
     *
     *       last:              boolean to indicate whether this is the last
     *                          batch of rectangles to compose for the frame.
     *
     *  Returns:
     *       true:              success.
     *
     *       false:             error.
     */

    bool (*compose_with_rects)(H264_context cxt, struct image_buf objects[],
                               unsigned int num_objects, bool last);


    /*  Function: push_frame()
     *
     *  Instructs the interface to push the composed H.264 frame to the
     *  supplied window(s), as defined by a "window_info" structure. This
     *  function may block or return immediately, depending on the value of
     *  the "wait" parameter.
     *
     *  In the case of a full screen or windowed session, a single window
     *  should be supplied so that the composed frame can be drawn into it.
     *
     *  In the case of a seamless sessions, an array of windows should be
     *  provided so that each window can be updated with the corresponding
     *  area in the composed frame.
     *
     *  If no window is supplied, the decoding interface is free to choose
     *  how the composed frame should be displayed on screen, if at all.
     *
     *  If "wait" is "true", this function must not return until the frame
     *  has been displayed. If "wait" is "false", the supplied (but optional)
     *  boolean will be set to "true" once the frame is pushed to screen.
     *
     *  Input:
     *       cxt:               handle to a valid H.264 context.
     *
     *       windows:           an array of window_info structures (see above).
     *
     *       num_windows:       number of window_info structures, or 0.
     *
     *       wait:              boolean to indicate whether the function should
     *                          return immediately or wait until the frame has
     *                          been displayed.
     *
     *       pushed:            a pointer to a boolean which will be set to
     *                          "true" once frame has been pushed to screen.
     *                          This parameter may be NULL.
     *
     *  Returns:
     *       true:              success, frame displayed.
     *
     *       false:             error, couldn't display frame.
     */

    bool (*push_frame)(H264_context cxt, struct window_info windows[],
                       unsigned int num_windows, bool wait, bool *pushed);


    /*  Function: close_context()
     *
     *  Tears down a previously opened H.264 decoding context. Any resources
     *  associated with the context should be released.
     *
     *  Input:
     *       cxt:               handle to a valid H.264 context.
     */

    void (*close_context)(H264_context cxt);


    /*  Function: end()
     *
     *  Perform any decoder specific de-initialisation. Called after all contexts
     *  are closed and client wishes to stop decoding any further data from interface,
     *	perhaps due to user wishing to terminate the program.
     */

    void (*end)();
};

#endif /* _H264_DECODE_H_ */
