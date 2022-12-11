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
#define STATIC_FILE_FOLDER "/Flash/html"
#endif

/* Data type of server application function */
void Dispatch(HTTPReqMessage *, HTTPRespMessage *);

#endif
