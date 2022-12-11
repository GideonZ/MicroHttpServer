#include <stdio.h>
#include <string.h>
#if ENABLE_STATIC_FILE == 1
#include <sys/stat.h>
#endif
#include "middleware.h"
#include "url.h"
#include "multipart.h"
#include "dummy_api.h"

/* Known Mime Types */
typedef struct {
    const char *extension;
    const char *mime_type;
} mime_map;

mime_map meme_types [] = {
    {".css", "text/css"},
    {".gif", "image/gif"},
    {".htm", "text/html"},
    {".html", "text/html"},
    {".jpeg", "image/jpeg"},
    {".jpg", "image/jpeg"},
    {".ico", "image/x-icon"},
    {".js", "application/javascript"},
    {".pdf", "application/pdf"},
    {".mp4", "video/mp4"},
    {".png", "image/png"},
    {".svg", "image/svg+xml"},
    {".xml", "text/xml"},
    {NULL, NULL},
};

const char *default_mime_type = "text/plain";

// ** DIRTY **
extern int execute_api_v1(HTTPReqMessage *req, HTTPRespMessage *resp);


static const char *get_mime_type(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (dot) { // strrchar Locate last occurrence of character in string
        mime_map *map = meme_types;
        while (map->extension) {
            if (strcmp(map->extension, dot) == 0) {
                return map->mime_type;
            }
            map++;
        }
    }
    return default_mime_type;
}

#if ENABLE_STATIC_FILE 
int filestream_out(void *context, uint8_t *buf, int len)
{
    int result = fread(buf, 1, len, (FILE *)context);
    if (!result) {
        fclose((FILE *)context);
    }
    return result;
}


/* Try to read static files under static folder. */
uint8_t _ReadStaticFiles(HTTPReqMessage *req, HTTPRespMessage *res)
{
//    if (!(req->Header.Method == HTTP_GET)) {
//        return 0;
//    }

    uint8_t found = 0;
    int8_t depth = 0;
    const char *uri = req->Header.URI;
    size_t n = strlen(uri);
    size_t i;

    FILE *fp;
    char path[128] = {STATIC_FILE_FOLDER};

    const char header[] = "HTTP/1.1 200 OK\r\nConnection: close\r\n"
                    "Content-Type: %s\r\n\r\n";

    /* Prevent Path Traversal. */
    for (i = 0; i < n; i++) {
        if (uri[i] == '/') {
            if (((n - i) > 2) && (uri[i + 1] == '.') && ((uri[i + 2] == '.'))) {
                depth -= 1;
                if (depth < 0)
                    break;
            } else if (((n - i) > 1) && (uri[i + 1] == '.'))
                continue;
            else
                depth += 1;
        }
    }

    if (depth >= 0) {
        /* Try to open and load the static file. */
        strcat(path, uri);
        int corr = 0;
        if (path[strlen(path)-1] == '/') {
            path[strlen(path)-1] = 0; // cut last slash
            corr = 1;
        }
        if ((strlen(uri) - corr) == 0) {
            strcat(path, "/index.html");
        }

        fp = fopen(path, "r");
        if (fp != NULL) {
            /* Build HTTP OK header. */
            n = sprintf((char *)res->_buf, header, get_mime_type(path));
            i = n;
            found = 1;
            res->_index = i;

            // always switch to streaming mode
            res->BodyCB = &filestream_out;
            res->BodyContext = fp;

        } else {
            printf("Not found: '%s'\n", path);
        }
    }

    return found;
}
#endif

void _NotFound(HTTPReqMessage *req, HTTPRespMessage *res)
{
    uint8_t n;
    char header[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";

    /* Build HTTP OK header. */
    n = strlen(header);
    memcpy(res->_buf, header, n);
    res->_index = n;
}

/* Dispatch an URI according to the route table. */
void Dispatch(HTTPReqMessage *req, HTTPRespMessage *res)
{
    uint8_t found = 0;

    // By default, there is no callback installed for the body data
    // such that it gets ditched properly.

    if (found != 1) {
#if ENABLE_STATIC_FILE == 2 // Running on Ultimate
        if (execute_api_v1(req, res) == 0) {
            found = 1;
        }
#else
        UrlComponents *c;
        if ((c = parse_url_header(&req->Header)) != NULL) {
            Api(c, req, res);
            delete_url_components(c);
            found = 1;
        }
#endif
    }

#if ENABLE_STATIC_FILE
    /* Check static files. */
    if (found != 1) {

        if (req->BodySize) {
            setup_multipart(req, &attachment_block_debug, NULL);
        }
        found = _ReadStaticFiles(req, res);
    }
#endif

    /* It is really not found. */
    if (found != 1)
        _NotFound(req, res);
}
