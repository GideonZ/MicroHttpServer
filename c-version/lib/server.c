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

void InitReqHeader(HTTPReqHeader *hdr)
{
    hdr->_buffer_valid = 0;
    hdr->Method = HTTP_UNKNOWN;
    hdr->FieldCount = 0;
    hdr->URI = "";
}

void InitReqMessage(HTTPReqMessage *req)
{
    req->protocol_state = eReq_Header;
    req->ContentType = "";
    req->BodyCB = NULL;
    req->_valid = 0;
    req->_used = 0;
    req->bodyType = 0;
    req->bodySize = 0;
    InitReqHeader(&(req->Header));
}

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
                if (srv->available_connections == 0) {
                    FD_CLR(srv->sock, &(srv->_read_sock_pool));
                }
                InitReqMessage(&(http_req[i].req));
                http_req[i].clisock = clisock;
                //http_req[i].req._valid = 0;
                //http_req[i].req.Header.FieldCount = 0;
                http_req[i].res.Header.FieldCount = 0;
                http_req[i].windex = 0;
                http_req[i].work_state = READING_SOCKET_HDR;
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
        /* Send with non-blocking socket. */
        hr->windex += hr->res._index - hr->windex;
        hr->work_state = WRITING_SOCKET;
    } else {
        /* Send with error. */
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
            if (FD_ISSET(http_req[i].clisock, &readable)) {
                /* Deal the request from the client socket. */
                // ReadSock simply reads (the maximum amount of) data into the read buffer and returns
                // a negative value if the socket errors out. In all other cases, the data is passed to
                // the ProcessClientData function, which implements the HTTP protocol.
                if (ReadSock(http_req + i) >= 0) {
                    // processing client data may cause the socket to switch to write mode, or close.
                    http_req[i].work_state = ProcessClientData(&(http_req[i].req), &(http_req[i].res), callback);
                }
                if (IsReqWriting(http_req[i].work_state)) {
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
                // at least one free socket, so accept is now allowed
                FD_SET(srv->sock, &(srv->_read_sock_pool));
            }
        }
    }
}

void HTTPServerClose(HTTPServer *srv)
{
    shutdown(srv->sock, SHUT_RDWR);
    close((srv)->sock);
}
