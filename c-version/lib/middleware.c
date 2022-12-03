#include <stdio.h>
#include <string.h>
#if ENABLE_STATIC_FILE == 1
#include <sys/stat.h>
#endif
#include "middleware.h"
#include "url.h"

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

// typedef enum { HTTP_UNKNOWN, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE } HTTPMethod;
const char *c_method_strings[] = { "BAD", "GET", "POST", "PUT", "DELETE" };

void Api(HTTPReqMessage *req, HTTPRespMessage *res) {
    int n, i = 0;
    char *p;
    char header[] = "HTTP/1.1 200 OK\r\nConnection: close\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n\r\n";

    /* Build header. */
    p = (char *)res->_buf;
    n = strlen(header);
    memcpy(p, header, n);
    p += n;
    i += n;

    /* Build body. */

    struct UrlComponents *c;
    c = parse_url(req->Header.URI);
    char comp[1024];
    sprintf(comp, "<h1>Data</h1><ul>"
                    "<li>method: %s</li>"
                    "<li>route: %s</li>"
                    "<li>path: %s</li>"
                    "<li>command: %s</li>"
                    "<li>querystring: %s</li>"
                    "<li>length: %d</li></ul>"
                    "<h3>Parameters</h3><ul>",
    c_method_strings[c->method], c->route, c->path, c->command, c->querystring, (int)c->parameters_len);
    n = strlen(comp);
    memcpy(p, comp, n);
    i += n;
    p += n;

    for (int j = 0; j < c->parameters_len; j++) {
        sprintf(comp, "<li>%s: <tt>%s</tt></li>", c->parameters[j].name, c->parameters[j].value);
        n = strlen(comp);
        memcpy(p, comp, n);
        i += n;
        p += n;
    }
    p -= n;

    res->_index = i;
}

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

    if (found != 1) {
        struct UrlComponents *c;
        if ((c = parse_url(req->Header.URI))) {
            Api(req, res);
            /*
            printf("method: %s\nroute: %s\npath: %s\ncommand: %s\nquerystring: %s\nlength: %d\n",
            c->method, c->route, c->path, c->command, c->querystring, c->parameters_len);
            for (int i = 0; i < c->parameters_len; i++) {
                printf("'%s' is '%s'\n", c->parameters[i]->name, c->parameters[i]->value);
            }
            */
            found = 1;
        }
    }

#if ENABLE_STATIC_FILE
    /* Check static files. */
    if (found != 1)
        found = _ReadStaticFiles(req, res);
#endif

    /* It is really not found. */
    if (found != 1)
        _NotFound(req, res);
}
