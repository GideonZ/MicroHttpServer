#include <sys/socket.h>
#include "netdb.h"
#include <ctype.h>
#include <string.h>
#include "server.h"
#include "middleware.h"
#include "multipart.h"

// http://hackerswithstyle.se/leet/search/aql?query=%28name%3A%22spacetaxi%22%29+%26+%28type%3Ad64%29+%26+%28category%3Agames%29

#define HOSTNAME      "hackerswithstyle.se"
#define HOSTPORT      80
#define URL_SEARCH    "/leet/search/aql"
#define URL_PATTERNS  "/leet/search/aql/presets"
#define URL_ENTRIES   "/leet/search/entries"
#define URL_DOWNLOAD  "/leet/search/bin"

int connect_to_assembly()
{
    int error;
    struct hostent my_host, *ret_host;
    struct sockaddr_in serv_addr;
    char buffer[1024];

    // setup the connection
    int result = gethostbyname_r(HOSTNAME, &my_host, buffer, 1024, &ret_host, &error);
    printf("Result Get HostName: %d\n", result);

    if (!ret_host) {
        printf("Could not resolve host '%s'.\n", HOSTNAME);
        return -1;
    }

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        printf("NO socket\n");
        return sock_fd;
    }

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, ret_host->h_addr, ret_host->h_length);
    serv_addr.sin_port = htons(HOSTPORT);

    if (connect(sock_fd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
        printf("Connection failed.\n");
        return -1;
    }
    printf("Connection succeeded.\n");
    return sock_fd;   
}

int read_socket(int sock, HTTPReqMessage *req)
{
    char *p = (char *)req->_buf;
    p += req->_valid;
    int space = HTTP_BUFFER_SIZE - req->_valid;
    printf("Recv %d -> %p\n", space, p);
    int n = space ? recv(sock, p, space, 0) : 0;
    if (n >= 0) {
        req->_valid += n;
    }
    return n;
}

void attachment_block_debug(BodyDataBlock_t *block);

void process_it(HTTPReqMessage *req, HTTPRespMessage *resp)
{
    printf("Process it! %d\n", req->protocol_state);
    printf("Used: %d. Valid %d. Content: %s\n", req->_used, req->_valid, req->ContentType);
    if (!req->BodyCB) {
        setup_multipart(req, &attachment_block_debug, NULL);
    }
}

void get_presets(int sock, HTTPReqMessage *resp)
{
    const char request[] = 
        "GET " URL_PATTERNS " HTTP/1.1\r\n"
        "Accept-encoding: identity\r\n"
        "Host: " HOSTNAME "\r\n"
        "User-Agent: Ultimate\r\n"
        "Connection: close\r\n"
        "\r\n";

    int n = send(sock, request, strlen(request), MSG_DONTWAIT);
    printf("Request sent (%d bytes)\n", n);

    uint8_t state = WRITING_SOCKET;
    do {
        n = read_socket(sock, resp);
        if (n) {
            state = ProcessClientData(resp, NULL, process_it);
        }
    } while(state < WRITING_SOCKET);
}

int main(int argc, const char **argv)
{
    HTTPReqMessage *resp = malloc(sizeof(HTTPReqMessage));
    memset(resp, 0, sizeof(HTTPReqMessage));
    int sock = connect_to_assembly();
    if (sock >= 0) {
        get_presets(sock, resp);
        close(sock);
    }
    free(resp);
}
