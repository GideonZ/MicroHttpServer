#ifndef __MICRO_HTTP_SERVER_H__
#define __MICRO_HTTP_SERVER_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HTTP_MAX_HEADER_SIZE (2048)
#define HTTP_BUFFER_SIZE (2048)

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

// this function is called whenever there is body data to be processed.
// When the function is NULL in the request class, the data will be ditched for the request.
// When this function returns 0 for the response, the stream will terminate.
// The context field can be used to identify which stream this call belongs to.
typedef int (*HTTPBODY_IN_CALLBACK)(void *context, const uint8_t *data, int size);
typedef int (*HTTPBODY_OUT_CALLBACK)(void *context, uint8_t *data, int size);

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
    char _buffer[HTTP_MAX_HEADER_SIZE+4];
    int _buffer_valid;
    HTTPMethod Method;
    const char *URI;
    const char *Version;
    HTTPHeaderField Fields[MAX_HEADER_FIELDS];
    unsigned int FieldCount;
} HTTPReqHeader;

typedef enum {
    eReq_Header,
    eReq_HeaderTooBig,
    eReq_HeaderDone,
    eReq_Body,
} t_ProtocolRecvState;

typedef enum {
    eNoBody,
    eUntilDisconnect,
    eTotalSize,
    eChunked,
} t_BodyType;

typedef enum {
    eChunkHeader,
    eChunkBody,
    eChunkTrailer,
} t_ChunkState;

typedef struct _HTTPReqMessage
{
    t_ProtocolRecvState protocol_state;
    HTTPReqHeader Header;
    const char *ContentType;
    HTTPBODY_IN_CALLBACK BodyCB;
    void    *BodyContext;
    size_t   bodySize;
    t_BodyType bodyType;
    t_ChunkState chunkState;
    size_t   chunkRemain;
    uint8_t  _buf[HTTP_BUFFER_SIZE+4]; // receive buffer
    int      _valid;
    int      _used;
} HTTPReqMessage;

typedef struct _HTTPRespHeader
{
    char *Version;
    char *StatusCode;
    char *Description;
    HTTPHeaderField Fields[MAX_HEADER_FIELDS];
    unsigned int FieldCount;
} HTTPRespHeader;

typedef struct _HTTPRespMessage
{
    HTTPRespHeader Header;
    HTTPBODY_OUT_CALLBACK BodyCB;
    void *BodyContext;
    size_t _index;
    uint8_t _buf[HTTP_BUFFER_SIZE+4];
} HTTPRespMessage;

void InitReqMessage(HTTPReqMessage *req);
void InitRespMessage(HTTPRespMessage *resp);

typedef void (*HTTPREQ_CALLBACK)(HTTPReqMessage *, HTTPRespMessage *);
uint8_t ProcessClientData(HTTPReqMessage *req, HTTPRespMessage *resp, HTTPREQ_CALLBACK callback);

void HTTPServerInit(HTTPServer *, uint16_t);
void HTTPServerRun(HTTPServer *, HTTPREQ_CALLBACK);
#define HTTPServerRunLoop(srv, callback)                                                                               \
    {                                                                                                                  \
        while (1) {                                                                                                    \
            HTTPServerRun(srv, callback);                                                                              \
        }                                                                                                              \
    }
void HTTPServerClose(HTTPServer *);
//typedef void (*SOCKET_CALLBACK)(void *);

#define NOTWORK_SOCKET 0
#define READING_SOCKET 1
#define READEND_SOCKET 2
#define WRITING_SOCKET 3
#define WRITEEND_SOCKET 4
#define CLOSE_SOCKET 5

#define DEBUG_MSG 1

#ifdef DEBUG_MSG
#include <stdio.h>
#define DebugMsg(...) (printf(__VA_ARGS__))
#else
#define DebugMsg(...)
#endif

#endif
