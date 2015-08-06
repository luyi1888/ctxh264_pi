/*************************************************************************
*
*   citrix.h
*
*   Citrix-specific type and macro definitions required for Plugin SDK.
*
*   $Id: //icaclient/unix13.2/client/unix/CitrixPluginSDK/inc/citrix.h#1 $
*
*   Copyright 2011-2014 Citrix Systems, Inc.  All Rights Reserved.
*
*************************************************************************/

#ifndef _CTX_TYPES_H_
#define _CTX_TYPES_H_

typedef unsigned char BOOLEAN, UCHAR;
#define TRUE  1
#define FALSE 0

typedef int INT32;
typedef unsigned int UINT32,  IU32; /* ***** Fixme - xkeytran.h ***** */
typedef int VPSTATUS;

/* Timers and events call back to their originator when triggered. */

typedef void (*PFNDELIVER)(void *, void *);

/* Needed for subwindow.h, particularly MM_DOUBLE_BUFFER. */

typedef struct SIGNED_RECT {
    INT32   left;
    INT32   top;
    INT32   right;
    INT32   bottom;
} SIGNED_RECT, *PSIGNED_RECT;

/* Miscellaneous definitions. */

#define ELEMENTS_IN_ARRAY(a) (sizeof (a) / sizeof (a)[0])

#define UNICODESUPPORT
#define PXL_SIZE 32

/* Identify Plugin SDK source. */

#define PLUGIN_SDK
#endif /* _CTX_TYPES_H_ */
