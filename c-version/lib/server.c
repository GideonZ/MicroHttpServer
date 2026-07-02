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
#if LWIP == 1
#include "lwip/sys.h"
#else
#include <time.h>
#endif

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
    uint32_t last_active_ms;
} HTTPReq;

HTTPReq http_req[MAX_HTTP_CLIENT];

/* Millisecond clock used for per-connection idle timing. */
static uint32_t _now_ms(void)
{
#if LWIP == 1
    return (uint32_t)sys_now();
#else
    return (uint32_t)time(NULL) * 1000u;
#endif
}

void HTTPServerInit(HTTPServer *srv, uint16_t port)
{
    // Just in case it was not initialized properly in BSS
    memset(srv, 0, sizeof(HTTPServer));

    struct sockaddr_in srv_addr;
    unsigned int i;

    /* Have a server socket. */
    srv->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->sock < 0) {
        DebugMsg("HTTPServerInit failed: no socket.\n");
        return;
    }
    /* Set server address. */
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    /* Set the server socket can reuse the address. */
    setsockopt(srv->sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    /* Bind the server socket with the server address. */
    if (bind(srv->sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) == -1) {
        HTTPServerClose(srv);
        DebugMsg("HTTPServerInit failed: bad bind.\n");
        return;
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
                DebugMsg("Accept client %d on socket %d.  %s:%d\n", i, clisock, 
                    inet_ntoa(cli_addr.sin_addr), (int)ntohs(cli_addr.sin_port));
                srv->available_connections -= 1;
                if (srv->available_connections == 0) {
                    FD_CLR(srv->sock, &(srv->_read_sock_pool));
                }
                InitReqMessage(&(http_req[i].req));
                http_req[i].clisock = clisock;
                http_req[i].res.Header.FieldCount = 0;
                http_req[i].windex = 0;
                http_req[i].work_state = READING_SOCKET;
                http_req[i].last_active_ms = _now_ms();
                break;
            }
        }
    }
}

int ReadSock(HTTPReq *hr)
{
    SOCKET clisock = hr->clisock;
    HTTPReqMessage *req = &(hr->req);
    char *p = (char *)req->_buf;
    p += req->_valid;
    int space = HTTP_BUFFER_SIZE - req->_valid;
    //printf("Recv %d -> %p\n", space, p);
    int n = space ? recv(clisock, p, space, 0) : 0;
    if (n >= 0) {
        req->_valid += n;
    }
    return n;
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
        /* Send buffer full on a non-blocking socket: nothing was sent, so leave
           windex unchanged and retry the SAME bytes when the socket is writable
           again. (Previously windex was advanced to res._index as if the buffer
           had been sent, silently dropping the unsent tail and truncating large
           responses to slow readers.) */
        hr->work_state = WRITING_SOCKET;
    } else {
        /* Send with error. */
        hr->work_state = CLOSE_SOCKET;
    }
}

void HTTPServerRun(HTTPServer *srv, HTTPREQ_CALLBACK callback)
{
    fd_set readable, writeable;
    struct timeval timeout = { HTTP_CONN_IDLE_TIMEOUT, 0 };
    uint16_t i;

    if (srv->sock < 0)
        return;

    /* Copy master socket queue to readable, writeable socket queue. */
    readable = srv->_read_sock_pool;
    writeable = srv->_write_sock_pool;
    /* Wait for activity on any socket, but time out so idle/stuck client
       connections can be reaped (see HTTP_CONN_IDLE_TIMEOUT). */
    int nready = select(srv->_max_sock + 1, &readable, &writeable, NULL, &timeout);
    if (nready < 0) {
        /* select() failed (e.g. interrupted by a signal): the fd_sets are now
           undefined, so do not inspect them this round. */
        return;
    }
    uint32_t now = _now_ms();
    /* Check server socket is readable. */
    if (FD_ISSET(srv->sock, &readable) && (srv->available_connections > 0)) {
        /* Accept when server socket has been connected. */
        _HTTPServerAccept(srv);
    }
    /* Check sockets in HTTP client requests pool are readable. */
    for (i = 0; i < MAX_HTTP_CLIENT; i++) {
        if (http_req[i].clisock != -1) {
            int active = 0;
            if (FD_ISSET(http_req[i].clisock, &readable)) {
                http_req[i].last_active_ms = now;
                active = 1;
                /* Deal the request from the client socket. */
                // ReadSock simply reads (the maximum amount of) data into the read buffer and returns
                // a negative value if the socket errors out. In all other cases, the data is passed to
                // the ProcessClientData function, which implements the HTTP protocol.
                int rd = ReadSock(http_req + i);
                if (rd > 0) {
                    // processing client data may cause the socket to switch to write mode, or close.
                    http_req[i].work_state = ProcessClientData(&(http_req[i].req), &(http_req[i].res), callback);
                } else {
                    /* recv() returned <= 0: < 0 is a socket error/reset, 0 is peer EOF.
                       Close the connection so its client slot is freed. Without this the
                       slot leaks and the now-dead fd keeps waking select(), eventually
                       driving available_connections to 0 and locking the server up. */
                    http_req[i].work_state = CLOSE_SOCKET;
                }
                if (IsReqWriting(http_req[i].work_state)) {
                    FD_SET(http_req[i].clisock, &(srv->_write_sock_pool));
                    FD_CLR(http_req[i].clisock, &(srv->_read_sock_pool));
                }
            }
            if (IsReqWriting(http_req[i].work_state) && FD_ISSET(http_req[i].clisock, &writeable)) {
                http_req[i].last_active_ms = now;
                active = 1;
                WriteSock(http_req + i);
            }
            /* Per-connection idle reaper: a connection that has seen no read or
               write activity for HTTP_CONN_IDLE_TIMEOUT seconds is stuck (e.g. a
               slowloris client that opened a slot then went silent). Reap it to
               free the slot, even while other connections stay busy. The signed
               difference is wrap-safe and, importantly, stays negative for a
               connection just accepted this round whose timestamp is a hair ahead
               of 'now', so a fresh connection is never reaped on arrival. */
            if (!active &&
                (int32_t)(now - http_req[i].last_active_ms) >= (int32_t)(HTTP_CONN_IDLE_TIMEOUT * 1000)) {
                http_req[i].work_state = CLOSE_SOCKET;
            }
            if (IsReqWriteEnd(http_req[i].work_state)) {
                http_req[i].work_state = CLOSE_SOCKET;
            }
            if (IsReqClose(http_req[i].work_state)) {
                /* If a request body was still being absorbed when the connection
                   is torn down (client disconnect / idle reap mid-upload), tell
                   the absorber to abort (len < 0) so it releases its buffers,
                   open file handle and request context instead of leaking them.
                   A completed body already cleared BodyCB (see ProcessClientData),
                   so this is a no-op for normal, fully-received requests. */
                if (http_req[i].req.BodyCB) {
                    http_req[i].req.BodyCB(http_req[i].req.BodyContext, NULL, -1);
                    http_req[i].req.BodyCB = NULL;
                    http_req[i].req.BodyContext = NULL;
                }
                shutdown(http_req[i].clisock, SHUT_RDWR);
                close(http_req[i].clisock);
                /* Remove the now-closed fd from BOTH master pools. Clearing only
                   the write pool leaks the fd in the read pool for any connection
                   closed straight from the reading state (e.g. a read error/reset,
                   which never passes through the writing state where the read pool
                   is cleared). A closed fd left in the read set makes select()
                   return immediately every iteration, so the server busy-spins and
                   starves the rest of the TCP/IP stack. */
                FD_CLR(http_req[i].clisock, &(srv->_read_sock_pool));
                FD_CLR(http_req[i].clisock, &(srv->_write_sock_pool));
                if (http_req[i].clisock >= srv->_max_sock)
                    srv->_max_sock -= 1;
                http_req[i].clisock = -1;
                http_req[i].work_state = NOTWORK_SOCKET;
                srv->available_connections += 1;
                // at least one free socket, so accept is now allowed
                FD_SET(srv->sock, &(srv->_read_sock_pool));
            }
        }
    }
}

void HTTPServerClose(HTTPServer *srv)
{
    if (srv->sock < 0)
        return;
    shutdown(srv->sock, SHUT_RDWR);
    close((srv)->sock);
    srv->sock = -1;
}
