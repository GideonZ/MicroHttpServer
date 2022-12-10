#include "server.h"
#if LWIP == 1
#include <lwip/inet.h>
#else
#include <arpa/inet.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>

typedef void (*SOCKET_CALLBACK)(void *);

#define NOTWORK_SOCKET 0
#define READING_SOCKET_HDR  1
#define READING_SOCKET_BODY 2
#define READEND_SOCKET 4
#define WRITING_SOCKET 8
#define WRITEEND_SOCKET 16
#define CLOSE_SOCKET 128
//#define IsReqReading(s) (s == READING_SOCKET)
#define IsReqWriting(s) (s == WRITING_SOCKET)
#define IsReqReadEnd(s) (s == READEND_SOCKET)
#define IsReqWriteEnd(s) (s == WRITEEND_SOCKET)
#define IsReqClose(s) (s == CLOSE_SOCKET)

typedef struct _HTTPReq
{
    SOCKET clisock;
    HTTPReqMessage req;
    HTTPRespMessage res;
    size_t windex;
    uint8_t work_state;
} HTTPReq;

HTTPReq http_req[MAX_HTTP_CLIENT];

void HTTPServerInit(HTTPServer *srv, uint16_t port)
{
    // Just in case it was not initialized properly in BSS
    memset(srv, 0, sizeof(HTTPServer));

    struct sockaddr_in srv_addr;
    unsigned int i;

    /* Have a server socket. */
    srv->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->sock <= 0)
        exit(1);
    /* Set server address. */
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    /* Set the server socket can reuse the address. */
    setsockopt(srv->sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    /* Bind the server socket with the server address. */
    if (bind(srv->sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) == -1) {
        exit(1);
    }
    /* Set the server socket non-blocking. */
    fcntl(srv->sock, F_SETFL, O_NONBLOCK);

    /* Start server socket listening. */
    DebugMsg("Listening\n");
    listen(srv->sock, MAX_HTTP_CLIENT / 2);

    /* Append server socket to the master socket queue. */
    FD_ZERO(&(srv->_read_sock_pool));
    FD_ZERO(&(srv->_write_sock_pool));
    FD_SET(srv->sock, &(srv->_read_sock_pool));
    /* The server socket's FD is max in the master socket queue for now. */
    srv->_max_sock = srv->sock;

    /* Prepare the HTTP client requests pool. */
    for (i = 0; i < MAX_HTTP_CLIENT; i++) {
        http_req[i].clisock = -1;
        http_req[i].work_state = NOTWORK_SOCKET;
    }
    srv->available_connections = MAX_HTTP_CLIENT;
}

void _HTTPServerAccept(HTTPServer *srv)
{
    struct sockaddr_in cli_addr;
    socklen_t sockaddr_len = sizeof(cli_addr);
    SOCKET clisock;
    unsigned int i;

    /* Have the client socket and append it to the master socket queue. */
    clisock = accept(srv->sock, (struct sockaddr *)&cli_addr, &sockaddr_len);
    if (clisock != -1) {
        FD_SET(clisock, &(srv->_read_sock_pool));
        /* Set the client socket non-blocking. */
        // fcntl(clisock, F_SETFL, O_NONBLOCK);
        /* Set the max socket file descriptor. */
        if (clisock > srv->_max_sock)
            srv->_max_sock = clisock;
        /* Add into HTTP client requests pool. */
        for (i = 0; i < MAX_HTTP_CLIENT; i++) {
            if (http_req[i].clisock == -1) {
                DebugMsg("Accept client %d.  %s:%d\n", i, inet_ntoa(cli_addr.sin_addr), (int)ntohs(cli_addr.sin_port));
                srv->available_connections -= 1;
                http_req[i].clisock = clisock;
                http_req[i].req.Header.FieldCount = 0;
                http_req[i].res.Header.FieldCount = 0;
                http_req[i].windex = 0;
                http_req[i].work_state = READING_SOCKET_HDR;
                break;
            }
        }
    }
}

int _CheckLine(char *buf)
{
    int i = 0;

    if (buf[i] == '\n') {
        if (buf[i - 1] == '\r')
            i = 2;
        else
            i = 1;
    }

    return i;
}

int _CheckFieldSep(char *buf)
{
    int i = 0;

    if ((buf[i - 1] == ':') && (buf[i] == ' ')) {
        i = 2;
    }

    return i;
}

HTTPMethod HaveMethod(char *method)
{
    HTTPMethod m;

    if (memcmp(method, "GET", 3) == 0)
        m = HTTP_GET;
    else if (memcmp(method, "POST", 4) == 0)
        m = HTTP_POST;
    else if (memcmp(method, "PUT", 3) == 0)
        m = HTTP_PUT;
    else if (memcmp(method, "DELETE", 6) == 0)
        m = HTTP_DELETE;
    else
        m = HTTP_GET;

    return m;
}

void WriteSock(HTTPReq *hr)
{
    ssize_t n;

    if ((hr->windex >= hr->res._index) && (hr->res.BodyCB)) {
        hr->windex = 0;
        hr->res._index = hr->res.BodyCB(hr->res.BodyContext, hr->res._buf, HTTP_BUFFER_SIZE);
        if (hr->res._index == 0) {
            hr->res.BodyCB = NULL;
        }
    }

    n = send(hr->clisock, hr->res._buf + hr->windex, hr->res._index - hr->windex, MSG_DONTWAIT);
    if (n > 0) {
        /* Send some bytes and send left next loop. */
        hr->windex += n;
        if ((hr->res._index > hr->windex) || (hr->res.BodyCB))
            hr->work_state = WRITING_SOCKET;
        else
            hr->work_state = WRITEEND_SOCKET;
    } else if (n == 0) {
        /* Writing is finished. */
        hr->work_state = WRITEEND_SOCKET;
    } else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        /* Send with non-blocking socket. */
        hr->windex += hr->res._index - hr->windex;
        hr->work_state = WRITING_SOCKET;
    } else {
        /* Send with error. */
        hr->work_state = CLOSE_SOCKET;
    }
}

int _IsLengthHeader(const char *key)
{
    return !strcasecmp(key, "content-length");
}

int _IsTypeField(const char *key)
{
    return !strcasecmp(key, "content-type");
}

void _ParseHeader(HTTPReqMessage *req, int bytes_received)
{
    char *p = (char *)req->_buf;
    char *lines[32];
    int line = 0;

    // First split the header buffer into lines by searching for the \r\n sequences
    // The method used here is to separate on \r and skip the following \n.
    for(line = 0; line < 32; line++) {
        lines[line] = strsep(&p, "\r");
        if (!p) {
            break;
        }
        p++; // skip the \n
        if (! *(lines[line])) {
            break;
        }
    }
    // p is now pointing to the byte after the header, if any. This might be part of the body.
    req->Body = (uint8_t *)p;
    req->BodyDataAvail = (size_t)bytes_received - ((size_t)p - (size_t)(req->_buf));

    // Step 2: Get the verb, path and HTTP identifier from the first line
    // Split by space
    // VERB path HTTP/1.1
    char *cur = lines[0];
    char *verb = strsep(&cur, " ");
    req->Header.Method = HaveMethod(verb);
    if (cur) {
        req->Header.URI = strsep(&cur, " ");
        req->Header.Version = cur; // can also be NULL
    } else {
        req->Header.URI = NULL;
        req->Header.Version = NULL;
    }

    // Step 3: Split the remaining lines into fields.
    // All subsequent lines are in the form of KEY ": " VALUE, although the space is not mandatory by spec.
    // so leading spaces need to be trimmed, and the separator is simply ':'
    req->Header.FieldCount = 0;
    for(line = 1; line < 32; line++) {
        if (! *(lines[line])) {
            break;
        }
        p = lines[line];
        req->Header.Fields[req->Header.FieldCount].key = strsep(&p, ":");
        if (p) {
            while(*p == ' ') {
                p++;
            }
            req->Header.Fields[req->Header.FieldCount].value = p;
            // printf("'%s' -> '%s'\n", req->Header.Fields[req->Header.FieldCount].key, req->Header.Fields[req->Header.FieldCount].value);
            req->Header.FieldCount++;
        } else {
            break;
        }
    }

    // Step 4: Determine how much body data is required for this request
    req->BodySize = 0;
    req->ContentType = NULL;
    if (req->Header.Method == HTTP_POST) {
        for (int i = 0; i < req->Header.FieldCount; i++) {
            if (_IsLengthHeader(req->Header.Fields[i].key)) {
                req->BodySize = strtol(req->Header.Fields[i].value, NULL, 0);
                break;
            }
        }
        for (int i = 0; i < req->Header.FieldCount; i++) {
            if (_IsTypeField(req->Header.Fields[i].key)) {
                req->ContentType = req->Header.Fields[i].value;
                break;
            }
        }
    }
    req->BodyCB = NULL; // to be filled in by the application
    req->BodyContext = NULL; // to be filled in by the application
}

int _GetBody(HTTPReq *hr)
{
    SOCKET clisock = hr->clisock;
    HTTPReqMessage *req = &(hr->req);

    const size_t c_max = HTTP_BUFFER_SIZE;
    req->BodySize -= req->BodyDataAvail;
    size_t remain = (req->BodySize > c_max) ? c_max : req->BodySize;

    char *p = (char *)req->_buf;
    int n = recv(clisock, p, remain, 0);
    if (n > 0) {
        req->BodyDataAvail = n;
    } else {
        req->BodyDataAvail = 0;
    }
    DebugMsg("\tGet more Body Data: %lu %lu\n", req->BodySize, req->BodyDataAvail);
    if (req->BodyCB) {
        req->BodyCB(req->BodyContext, req->_buf, req->BodyDataAvail);
        if (req->BodyDataAvail >= req->BodySize) {
            req->BodyCB(req->BodyContext, NULL, 0);
            req->BodyCB = NULL; // done!
            n = 0;
        }
    } else {
        DebugMsg("\tData ditched, no callback.\n");
        if (req->BodyDataAvail >= req->BodySize) {
            n = 0;
        }
    }
    return n;
}

int _GetHeader(HTTPReq *hr)
{
    SOCKET clisock = hr->clisock;
    HTTPReqMessage *req = &(hr->req);
    int n;
    char *p;

    DebugMsg("\tParse Header\n");
    p = (char *)req->_buf;
    n = recv(clisock, p, HTTP_BUFFER_SIZE, 0);

    if (n > 0) {
        p[n] = '\0'; // make it a null terminated string, so we know until where data was read later on, without
                     // checking n in every string operation. This is allowed, because we allocated HTTP_BUFFER_SIZE+4

        _ParseHeader(req, n); // Do we actually NEED to parse everything??
    }
    return n;
}

void _HTTPServerRequest(HTTPReq *hr, HTTPREQ_CALLBACK callback)
{
    int n;
    HTTPReqMessage *req = &(hr->req);

    if (hr->work_state == READING_SOCKET_HDR) {
        n = _GetHeader(hr);
        if (n > 0) {
            if (req->BodyDataAvail < req->BodySize) {
                hr->work_state = READING_SOCKET_BODY;
            } else {
                /* Write all response. */
                hr->work_state = WRITING_SOCKET;
            }
            callback(&(hr->req), &(hr->res));
            // TODO: The data tail could be body, but could also be a new request.
            // This should be handled differently. In practice, BodyDataAvail is always 0 at this point
            if (req->BodyDataAvail) {
                if (req->BodyCB) {
                    req->BodyCB(req->BodyContext, req->Body, req->BodyDataAvail);
                    if (req->BodyDataAvail == req->BodySize) {
                        req->BodyCB(req->BodyContext, NULL, 0); // Terminate
                    }
                }
            }
        } else {
            hr->work_state = CLOSE_SOCKET;
        }
    } else if (hr->work_state == READING_SOCKET_BODY) {
        n = _GetBody(hr);
        if (n > 0) {
            hr->work_state = READING_SOCKET_BODY;
        } else if(n == 0) {
            /* Write all response. */
            hr->work_state = WRITING_SOCKET;
        } else {
            hr->work_state = CLOSE_SOCKET;
        }
    } else {
        printf("Unexpected work state %d\n", hr->work_state);
        hr->work_state = CLOSE_SOCKET;
    }
}

void HTTPServerRun(HTTPServer *srv, HTTPREQ_CALLBACK callback)
{
    fd_set readable, writeable;
    //struct timeval timeout = {5, 5};
    uint16_t i;

    /* Copy master socket queue to readable, writeable socket queue. */
    readable = srv->_read_sock_pool;
    writeable = srv->_write_sock_pool;
    /* Wait the flag of any socket in readable socket queue. */
    select(srv->_max_sock + 1, &readable, &writeable, NULL, NULL); // &timeout);
    printf("$");
    /* Check server socket is readable. */
    if (FD_ISSET(srv->sock, &readable) && (srv->available_connections > 0)) {
        /* Accept when server socket has been connected. */
        _HTTPServerAccept(srv);
    }
    /* Check sockets in HTTP client requests pool are readable. */
    for (i = 0; i < MAX_HTTP_CLIENT; i++) {
        if (http_req[i].clisock != -1) {
            // s = &(http_req[i].clisock);
            if (FD_ISSET(http_req[i].clisock, &readable)) {
                /* Deal the request from the client socket. */
                _HTTPServerRequest(&(http_req[i]), callback);
                if (http_req[i].work_state == WRITING_SOCKET) {
                    FD_SET(http_req[i].clisock, &(srv->_write_sock_pool));
                    FD_CLR(http_req[i].clisock, &(srv->_read_sock_pool));
                }
            }
            if (IsReqWriting(http_req[i].work_state) && FD_ISSET(http_req[i].clisock, &writeable)) {
                WriteSock(http_req + i);
            }
            if (IsReqWriteEnd(http_req[i].work_state)) {
                http_req[i].work_state = CLOSE_SOCKET;
            }
            if (IsReqClose(http_req[i].work_state)) {
                shutdown(http_req[i].clisock, SHUT_RDWR);
                close(http_req[i].clisock);
                FD_CLR(http_req[i].clisock, &(srv->_write_sock_pool));
                if (http_req[i].clisock >= srv->_max_sock)
                    srv->_max_sock -= 1;
                http_req[i].clisock = -1;
                http_req[i].work_state = NOTWORK_SOCKET;
                srv->available_connections += 1;
            }
        }
    }
}

void HTTPServerClose(HTTPServer *srv)
{
    shutdown(srv->sock, SHUT_RDWR);
    close((srv)->sock);
}

#ifdef MICRO_HTTP_SERVER_EXAMPLE
/* This is exmaple. */
void _HelloPage(HTTPReqMessage *req, HTTPRespMessage *res)
{
    int n, i = 0, j;
    char *p;
    char header1[] = "HTTP/1.1 200 OK\r\nConnection: close\r\n";
    char header2[] = "Content-Type: text/html; charset=UTF-8\r\n\r\n";
    char body1[] = "<html><body>許功蓋 Hello <br>";
    char body2[] = "</body></html>";

    /* Build header. */
    p = res->_buf;
    n = strlen(header1);
    memcpy(p, header1, n);
    p += n;
    i += n;

    n = strlen(header2);
    memcpy(p, header2, n);
    p += n;
    i += n;

    /* Build body. */
    n = strlen(body1);
    memcpy(p, body1, n);
    p += n;
    i += n;

    /* Echo request header into body. */
    n = strlen(req->_buf);
    memcpy(p, req->_buf, n);
    p += n;
    i += n;

    n = strlen("<br>");
    memcpy(p, "<br>", n);
    p += n;
    i += n;

    n = strlen(req->Header.URI);
    memcpy(p, req->Header.URI, n);
    p += n;
    i += n;

    n = strlen("<br>");
    memcpy(p, "<br>", n);
    p += n;
    i += n;

    n = strlen(req->Header.Version);
    memcpy(p, req->Header.Version, n);
    p += n;
    i += n;

    for (j = 0; j < req->Header.FieldCount; j++) {
        n = strlen("<br>");
        memcpy(p, "<br>", n);
        p += n;
        i += n;

        n = strlen(req->Header.Fields[j].key);
        memcpy(p, req->Header.Fields[j].key, n);
        p += n;
        i += n;

        p[0] = ':';
        p[1] = ' ';
        p += 2;
        i += 2;

        n = strlen(req->Header.Fields[j].value);
        memcpy(p, req->Header.Fields[j].value, n);
        p += n;
        i += n;
    }

    n = strlen(body2);
    memcpy(p, body2, n);
    i += n;

    res->_index = i;
}

int main(void)
{
    HTTPServer srv;
    HTTPServerInit(&srv, MHS_PORT);
    while (1) {
        HTTPServerRun(&srv, _HelloPage);
    }
    HTTPServerClose(&srv);
    return 0;
}

#endif
