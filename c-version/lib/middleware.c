#include <stdio.h>
#include <string.h>
#if ENABLE_STATIC_FILE == 1
#include <sys/stat.h>
#endif
#include "middleware.h"

/* Route */
typedef struct _Route
{
    HTTPMethod method;
    const char *uri;
    SAF saf;
} Route;

Route routes[MAX_HTTP_ROUTES];
int routes_used = 0;

/* Add an URI and the corresponding server application function into the route
   table. */
int AddRoute(HTTPMethod method, const char *uri, SAF saf)
{
    if (routes_used < MAX_HTTP_ROUTES) {
        routes[routes_used].method = method;
        routes[routes_used].uri = uri;
        routes[routes_used].saf = saf;
        routes_used++;

        return routes_used;
    } else {
        return 0;
    }
}

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

#if ENABLE_STATIC_FILE != 2
/* Try to read static files under static folder. */
uint8_t _ReadStaticFiles(HTTPReqMessage *req, HTTPRespMessage *res)
{
    uint8_t found = 0;
    int8_t depth = 0;
    const char *uri = req->Header.URI;
    size_t n = strlen(uri);
    size_t i;

    FILE *fp;
    int size;
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

	res->fp = NULL;

    if ((depth >= 0) && (uri[i - 1] != '/')) {
        /* Try to open and load the static file. */
        memcpy(path + strlen(STATIC_FILE_FOLDER), uri, strlen(uri));
        fp = fopen(path, "r");
        if (fp != NULL) {
            fseek(fp, 0, SEEK_END);
            size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            /* Build HTTP OK header. */
            // n = strlen(header);
            // memcpy(res->_buf, header, n);
            n = sprintf((char *)res->_buf, header, get_mime_type(path));
            i = n;
            found = 1;

            if (size < MAX_BODY_SIZE) {
                /* Build HTTP body. */
                n = fread(res->_buf + i, 1, size, fp);
                i += n;
                res->_index = i;
                fclose(fp);
				res->fp = NULL;
            } else { // use streaming mode
                n = fread(res->_buf + i, 1, MAX_BODY_SIZE, fp);
				i += n;
				res->_index = i;
				res->fp = fp; // read the remainder from file!
            }
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
    uint16_t i;
    size_t n;
    const char *req_uri = req->Header.URI;
    uint8_t found = 0;

    /* Check the routes. */
    for (i = 0; i < routes_used; i++) {
        /* Compare method. */
        if (req->Header.Method == routes[i].method) {
            /* Compare URI. */
            n = strlen(routes[i].uri);
            if (memcmp(req_uri, routes[i].uri, n) == 0)
                found = 1;
            else
                continue;

            if ((found == 1) && ((req_uri[n] == '\0') || (req_uri[n] == '\?'))) {
                /* Found and dispatch the server application function. */
                routes[i].saf(req, res);
                break;
            } else {
                found = 0;
            }
        }
    }

#if ENABLE_STATIC_FILE == 1
    /* Check static files. */
    if (found != 1)
        found = _ReadStaticFiles(req, res);
#endif

    /* It is really not found. */
    if (found != 1)
        _NotFound(req, res);
}
