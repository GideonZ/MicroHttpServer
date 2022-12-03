#ifndef __MICRO_HTTP_SERVER_H__
#define __MICRO_HTTP_SERVER_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_HEADER_SIZE 2048
#define MAX_BODY_SIZE 2048

#ifndef MHS_PORT
#define MHS_PORT 80
#define close lwip_close
#define LWIP 1
#else
#define LWIP 0
#endif

#ifndef MAX_HTTP_CLIENT
#define MAX_HTTP_CLIENT 4
#endif
#ifndef HTTP_SERVER
#define HTTP_SERVER "Micro CHTTP Server"
#endif

typedef int SOCKET;

typedef struct _HTTPServer
{
    SOCKET sock;
    SOCKET _max_sock;
    fd_set _read_sock_pool;
    fd_set _write_sock_pool;
    int available_connections;
} HTTPServer;

typedef struct _HTTPHeaderField
{
    const char *key;
    const char *value;
} HTTPHeaderField;

#ifndef MAX_HEADER_FIELDS
#define MAX_HEADER_FIELDS 20
#endif

typedef enum { HTTP_UNKNOWN, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE } HTTPMethod;

typedef struct _HTTPReqHeader
{
    HTTPMethod Method;
    const char *URI;
    const char *Version;
    HTTPHeaderField Fields[MAX_HEADER_FIELDS];
    unsigned int Amount;
} HTTPReqHeader;

typedef struct _HTTPReqMessage
{
    HTTPReqHeader Header;
    size_t _index;
    uint8_t *Body;
    uint8_t *_buf;
} HTTPReqMessage;

typedef struct _HTTPRespHeader
{
    char *Version;
    char *StatusCode;
    char *Description;
    HTTPHeaderField Fields[MAX_HEADER_FIELDS];
    unsigned int Amount;
} HTTPRespHeader;

typedef struct _HTTPRespMessage
{
    HTTPRespHeader Header;
    FILE *fp;
    size_t _index;
    uint8_t *Body;
    uint8_t *_buf;
} HTTPRespMessage;

typedef void (*HTTPREQ_CALLBACK)(HTTPReqMessage *, HTTPRespMessage *);

void HTTPServerInit(HTTPServer *, uint16_t);
void HTTPServerRun(HTTPServer *, HTTPREQ_CALLBACK);
#define HTTPServerRunLoop(srv, callback)                                                                               \
    {                                                                                                                  \
        while (1) {                                                                                                    \
            HTTPServerRun(srv, callback);                                                                              \
        }                                                                                                              \
    }
void HTTPServerClose(HTTPServer *);

#define DEBUG_MSG 1

#ifdef DEBUG_MSG
#include <stdio.h>
#define DebugMsg(...) (printf(__VA_ARGS__))
#else
#define DebugMsg(...)
#endif

#endif
