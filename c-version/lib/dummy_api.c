#include "dummy_api.h"
#include "multipart.h"
#include <string.h>

const char *c_method_strings[] = { "BAD", "GET", "POST", "PUT", "DELETE" };

/* Example implementation of API */
typedef struct {
    HTTPRespMessage *msg;
    char filename[128];
    int filesize;
} ApiBody_t;

void ApiBody(BodyDataBlock_t *block)
{
    ApiBody_t *body = (ApiBody_t *)block->context;
    HTTPRespMessage *resp = body->msg;
    char temp[256];
    temp[0] = 0;
    switch(block->type) {
        case eStart:
            sprintf(temp, "<h3>Attachments</h3><ul>\n");
            strcpy(body->filename, "WRONG!");
            break;
        case eDataStart:
            body->filesize = 0;
            strcpy(body->filename, "raw data");
            break;
        case eSubHeader:
            body->filesize = 0;
            strcpy(body->filename, "Unnamed");
            HTTPHeaderField *f = (HTTPHeaderField *)block->data;
            for(int i=0; i < block->length; i++) {
                if (strcasecmp(f[i].key, "Content-Disposition") == 0) {
                    // extract filename from value string, e.g. 'form-data; name="bestand"; filename="sample.html"'
                    char *sub = strstr(f[i].value, "filename=\"");
                    if (sub) {
                        strncpy(body->filename, sub + 10, 127);
                        body->filename[127] = 0;
                        char *quote = strstr(body->filename, "\"");
                        if (quote) {
                            *quote = 0;
                        }
                    }
                }
            }
            break;
        case eDataBlock:
            body->filesize += block->length;
            break;
        case eDataEnd:
            sprintf(temp, "<li>'%s' <tt> (Size: %d)</tt></li>\n", body->filename, body->filesize);
            break;
        case eTerminate:
            sprintf(temp, "</ul>\n</body></html>\n");
            free(body);
            break;
    }
    int n = strlen(temp);
    char *p = (char *)resp->_buf + resp->_index;
    memcpy(p, temp, n);
    resp->_index += n;
}


void Api(UrlComponents *c, HTTPReqMessage *req, HTTPRespMessage *res)
{
    int n, i = 0;
    char *p;
    char header[] = "HTTP/1.1 200 OK\r\nConnection: close\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n\r\n"
                    "<!DOCTYPE html><html><body>\n";

    /* Build header. */
    p = (char *)res->_buf;
    n = strlen(header);
    memcpy(p, header, n);
    p += n;
    i += n;

    /* Build body. */
    char comp[1024];
    sprintf(comp, "<h1>Data</h1><ul>"
                    "<li>method: %s</li>"
                    "<li>route: %s</li>"
                    "<li>path: %s</li>"
                    "<li>command: %s</li>"
                    "<li>querystring: \"%s\"</li>"
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
    const char *closing = "</ul>\n";
    n = strlen(closing);
    memcpy(p, closing, n);
    i += n;
    p += n;
    res->_index = i;

    if (req->bodyType != eNoBody) {
        ApiBody_t *body = malloc(sizeof(ApiBody_t));
        body->msg = res; // This function can write directly into the response data buffer (!)
        setup_multipart(req, &ApiBody, body);
    } else {
        // close early
        const char *tail = "</body></html>\n";
        n = strlen(tail);
        memcpy(p, tail, n);
        i += n;
        p += n;
        res->_index = i;
    }
}
