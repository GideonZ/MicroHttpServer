#include <sys/socket.h>
#include "netdb.h"
#include <ctype.h>
#include <string.h>
#include "server.h"
#include "middleware.h"
#include "multipart.h"
#include "json.h"

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

typedef struct {
    int offset;
    int size;
    uint8_t buffer[16384];
} t_BufferedBody;

void attachment_to_buffer(BodyDataBlock_t *block)
{
    HTTPHeaderField *f;
    t_BufferedBody *body = (t_BufferedBody *)block->context;

    switch(block->type) {
        case eStart:
            printf("--- Start of Body --- (Type: %s)\n", block->data);
            break;
        case eDataStart:
            printf("--- Raw Data Start ---\n");
            body->offset = 0;
            body->size = 0;
            break;
        case eSubHeader:
            printf("--- SubHeader ---\n");
            f = (HTTPHeaderField *)block->data;
            for(int i=0; i < block->length; i++) {
                printf("%s => '%s'\n", f[i].key, f[i].value);
            }
            break;
        case eDataBlock:
            printf("--- Data (%d bytes)\n", block->length);
            if (block->length < (16384 - body->offset)) {
                memcpy(body->buffer + body->offset, block->data, block->length);
                body->offset += block->length;
                body->size += block->length;
            } else {
                printf("-> Ditched, buffer full.\n");
            }
            break;
        case eDataEnd:
            printf("--- End of Data ---\n");
            break;
        case eTerminate:
            printf("--- End of Body ---\n");
            printf("Total size: %d\n", body->size);
            break;
    }
}


void collect_in_buffer(HTTPReqMessage *req, HTTPRespMessage *resp)
{
    t_BufferedBody *body = (t_BufferedBody *)malloc(sizeof(t_BufferedBody));
    req->userContext = body;
    body->offset = 0;
    body->size = 0;
    setup_multipart(req, &attachment_to_buffer, body);
}

void get_response(int sock, HTTPReqMessage *resp, HTTPREQ_CALLBACK callback)
{
    uint8_t state = WRITING_SOCKET;
    do {
        int n = read_socket(sock, resp);
        if (n) {
            state = ProcessClientData(resp, NULL, callback);
        }
    } while(state < WRITING_SOCKET);
}

JSON *convert_buffer_to_json(t_BufferedBody *body)
{
    body->buffer[body->size] = 0;
    JSON *json = NULL;
    int j = convert_text_to_json_objects((char *)body->buffer, body->size, 1000, &json);
    return json;
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
    get_response(sock, resp, collect_in_buffer);
    
    JSON *json = convert_buffer_to_json((t_BufferedBody *)resp->userContext);

    if(json) {
        printf(json->render());
        free(json);
    }
    free(resp->userContext);
}

void get_binary(int sock, HTTPReqMessage *resp, const char *id, int cat, int binId)
{
    const char request_fmt[] = 
        "GET " URL_DOWNLOAD "/%s/%d/%d HTTP/1.1\r\n"
        "Accept-encoding: identity\r\n"
        "Host: " HOSTNAME "\r\n"
        "User-Agent: Ultimate\r\n"
        "Connection: close\r\n"
        "\r\n";

    char req_buffer[256];
    sprintf(req_buffer, request_fmt, id, cat, binId);

    int n = send(sock, req_buffer, strlen(req_buffer), MSG_DONTWAIT);
    get_response(sock, resp, collect_in_buffer);
    free(resp->userContext);
}

extern "C" {
    void outbyte(int c) { putc(c, stdout); }
}

int main(int argc, const char **argv)
{
    HTTPReqMessage *resp = (HTTPReqMessage *)malloc(sizeof(HTTPReqMessage));
    memset(resp, 0, sizeof(HTTPReqMessage));
    int sock = connect_to_assembly();
    if (sock >= 0) {
        //get_presets(sock, resp);
        get_binary(sock, resp, "237033", 0, 0);
        close(sock);
    }
    free(resp);
}
