#ifndef __MICRO_HTTP_MIDDLEWARE_H__
#define __MICRO_HTTP_MIDDLEWARE_H__

#include "server.h"

#ifndef ENABLE_STATIC_FILE
#define ENABLE_STATIC_FILE 2
#define LWIP 1
#endif

#if (ENABLE_STATIC_FILE == 1) && !(defined STATIC_FILE_FOLDER)
#define STATIC_FILE_FOLDER "static"
#elif (ENABLE_STATIC_FILE == 2) && !(defined STATIC_FILE_FOLDER)
#define STATIC_FILE_FOLDER "/Temp"
#endif

/* Data type of server application function */
void Dispatch(HTTPReqMessage *, HTTPRespMessage *);

typedef enum {
    eStart = 0,
    eSubHeader,
    eDataBlock,
    eDataEnd,
    eTerminate,
} BlockType_t;

typedef struct {
    BlockType_t type;
    const char *data;
    int   length;
    void *context;
} BodyDataBlock_t;

typedef void (*BODY_DATABLOCK_CB)(BodyDataBlock_t *);

#endif
