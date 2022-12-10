#include <stdio.h>
#include <string.h>
#if ENABLE_STATIC_FILE == 1
#include <sys/stat.h>
#endif
#include "middleware.h"
#include "url.h"

/* Debug */
#define DUMP_BYTES 16
void dump_hex_actual(const void *pp, int len, int relative)
{
    int w,t;
    uint8_t c;
    uint8_t *p = (uint8_t *)pp;
    
	for(w=0;w<len;w+=DUMP_BYTES) {
        if(relative)
            printf("%4x: ", w);
        else
		    printf("%p: ", p + w);
        for(t=0;t<DUMP_BYTES;t++) {
            if((w+t) < len) {
		        printf("%02x ", *((uint8_t *)&(p[w+t])));
		    } else {
		        printf("   ");
		    }
		}
        for(t=0;t<DUMP_BYTES;t++) {
            if((w+t) < len) {
                c = p[w+t] & 0x7F;

                if((c >= 0x20)&&(c <= 0x7F)) {
                    printf("%c", c);
                } else {
                    printf(".");
                }
		    } else {
		        break;
		    }
		}
		printf("\n");
	}
}

void dump_hex(const void *pp, int len)
{
    dump_hex_actual(pp, len, 0);
}

void dump_hex_relative(const void *pp, int len)
{
    dump_hex_actual(pp, len, 1);
}

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
const char *c_method_strings[] = { "BAD", "GET", "POST", "PUT", "DELETE" };

// ** DIRTY **
extern int execute_api_v1(HTTPReqMessage *req, HTTPRespMessage *resp);

/* Body Parsing (Attachment Extractor) */
typedef enum {
    eInit = 0,
    eBinary,
    eDitch,
    eHeader,
    eData,
    eTerminated,
} stream_state_t;

typedef struct _FileStream
{
    stream_state_t state;
    const char *type;
    char *boundary;
    int boundary_length;
    int match_state;
    char header[1024];
    int header_size;
    char data[4096];
    int data_size;
    int field_count;
    HTTPHeaderField fields[8];
    BODY_DATABLOCK_CB block_cb;
    void *block_context;
} FileStream_t;

// Finding the separators in a stream is not that easy when the data comes in in packets, as the separators can be
// partly on packet boundaries. Secondly, it is not possible to stream out all data to the destination directly, because
// part of the last data might belong to the boundary. One solution is found in the use of a FIFO where the data packets
// are pushed into. This allows some of the data to be held back before forwarding it to the destination. The separator
// detection is based on a state machine. The state of the string comparison is kept, at all times, in particular at the
// end of a data block, such that the string comparison can continue when new data arrives. In order for this mechanism
// to work well, there is never more data taken out from the FIFO than the number of available data bytes minus the
// separator length. Secondly, the FIFO should always be able to take new data in. The maximum size of a data block is
// known. The chosen FIFO size equals two data blocks. This also allows of the parsing of the multipart mime header,
// while it is in the fifo; if the block size is larger than the expected header. On top of all this, a state machine
// that keeps track of the order of the data elements. A multipart form is structured like this:
// (--, SEPARATOR, \r\n, (HEADER FIELD, \r\n)+, \r\n, DATA)+), --, SEPARATOR, \r\n.
// When the separator to search for is extended with as --SEP\r\n, and the \r\n are included in the header fields, this
// simplifies to:
// (SEPARATOR, (HEADER FIELD)+, EMPTY_LINE, DATA)+, SEPARATOR.
//
// Another approach is pattern searching on the fly. In this approach, the separator search state machine is extended
// to also handle the header as well. Separate buffers are created to store the three different types of data that
// the multipart stream header contains:
// - Header Bytes
// - Data Bytes
// Storing separator bytes is not necessary because they are constant. Still, these bytes will turn out to be data bytes
// as soon as it turns out that not the entire separator matches. Then these bytes need to be added to the data bytes.
// The data bytes buffer not strictly needed, but might facilitate writing the data to a device that requires alignment.
// Storing the header bytes facilitates in parsing the header, because this guarantees that all the data is
// sequential, and doesn't wrap around the boundaries of a circular buffer, as is the case with a FIFO.
// The state machine should have the following states:
// [ wait_separator, separator, header, data ]. In the first state, all non-matching data is thrown away.

static void _ParseMultiPartHeader(FileStream_t *stream)
{
    char *p = (char *)stream->header + 2;
    char *lines[8];
    int line = 0;
    p[stream->header_size] = 0;

    // First split the header buffer into lines by searching for the \r\n sequences
    // The method used here is to separate on \r and skip the following \n.
    for(line = 0; line < 8; line++) {
        lines[line] = strsep(&p, "\r");
        if (!p) {
            break;
        }
        p++; // skip the \n
        if (! *(lines[line])) {
            break;
        }
    }
    // p is now pointing to the byte after the header, if any.

    // Step 2: Split the remaining lines into fields.
    // All subsequent lines are in the form of KEY ": " VALUE, although the space is not mandatory by spec.
    // so leading spaces need to be trimmed, and the separator is simply ':'
    int count = 0;
    for(line = 0; line < 8; line++) {
        if (! *(lines[line])) {
            break;
        }
        p = lines[line];
        stream->fields[count].key = strsep(&p, ":");
        if (p) {
            while(*p == ' ') {
                p++;
            }
            stream->fields[count].value = p;
            // printf("'%s' -> '%s'\n", stream->fields[count].key, stream->fields[count].value);
            count++;
        } else {
            break;
        }
    }
    stream->field_count = count;

    if (stream->block_cb) {
        BodyDataBlock_t block = { eSubHeader, (const char *)stream->fields, count, stream->block_context };
        stream->block_cb(&block);
    }
}

void attachment_block_debug(BodyDataBlock_t *block)
{
    switch(block->type) {
        case eStart:
            printf("--- Start of Body --- (Type: %s)\n", block->data);
            break;
        case eSubHeader:
            printf("--- SubHeader ---\n");
            HTTPHeaderField *f = (HTTPHeaderField *)block->data;
            for(int i=0; i < block->length; i++) {
                printf("%s => '%s'\n", f[i].key, f[i].value);
            }
            break;
        case eDataBlock:
            printf("--- Data (%d bytes)\n", block->length);
/*
            if (block->length == 4096) {
                dump_hex_relative(block->data, 32);
                printf("... <truncated>\n");
            } else {
                dump_hex_relative(block->data, 32);
                int offset = (block->length - 64);
                if (offset < 32)
                    offset = 32;
                offset &= ~0xF;
                printf("... <not shown %d>\n", offset-32);
                dump_hex_relative(block->data + offset, block->length - offset);
                printf("`---\n");
            }
*/
            break;
        case eDataEnd:
            printf("--- End of Data ---\n");
            break;
        case eTerminate:
            printf("--- End of Body ---\n");
            break;
    }
}

static void expunge(FileStream_t *stream)
{
    if (stream->block_cb) {
        BodyDataBlock_t block = (BodyDataBlock_t){ eDataBlock, stream->data, stream->data_size, stream->block_context };
        stream->block_cb(&block);
    }
    stream->data_size = 0;
}

static int filestream_in(void *context, uint8_t *buf, int len)
{
    FileStream_t *stream = (FileStream_t *)context;

    static const char c_header_end[4] = "\r\n\r\n";

    switch (stream->state) {
        case eInit: // the first time that this function is called is to set it up
        // after the request header has been parsed. It is called without data.
            if (stream->block_cb) {
                BodyDataBlock_t block = { eStart, stream->type, strlen(stream->type), stream->block_context };
                stream->block_cb(&block);
            }
            if (!strncasecmp(stream->type, "multipart", 9)) { // 9 chars
                char *b = strstr(stream->type, "boundary="); // 9 chars
                if (b) {
                    len = strlen(b+9);
                    stream->boundary = malloc(len + 5); // \r\n-- + len + \r\n\0
                    stream->boundary[0] = '\r';
                    stream->boundary[1] = '\n';
                    stream->boundary[2] = '-';
                    stream->boundary[3] = '-';
                    strcpy(stream->boundary + 4, b + 9);
                    stream->boundary[len+4] = 0;
                    stream->boundary_length = strlen(stream->boundary);
                    stream->match_state = 2; // the data we receive starts with a boundary, but without the \r\n
                    stream->state = eDitch;
                    return 0;
                } else {
                    printf("boundary not found in multipart\n");
                }
            }
            stream->state = eBinary;
            break;

        case eBinary:
            if (len == 0) {
                if (stream->block_cb) {
                    BodyDataBlock_t block = { eDataEnd, NULL, 0, stream->block_context };
                    stream->block_cb(&block);
                }
                if (stream->block_cb) {
                    BodyDataBlock_t block = { eTerminate, NULL, 0, stream->block_context };
                    stream->block_cb(&block);
                }
                // Since we are the owner of this struct, we can free it
                free(stream);
            } else {
                if (stream->block_cb) {
                    BodyDataBlock_t block = { eDataBlock, (const char *)buf, len, stream->block_context };
                    stream->block_cb(&block);
                }
            }
            break;

        case eDitch: // idle, looking for separator to start with
        case eData:
        case eHeader:
            // all states together, because this is where this function
            // enters. Could have done this with a sub state
            for(int i=0;i<len;i++) {
                if (stream->state != eHeader) {
                    if (stream->match_state == stream->boundary_length) {
                        stream->match_state = 0;
                        if (stream->data_size) {
                            expunge(stream);
                        }
                        if ((stream->block_cb) && (stream->state == eData)) {
                            BodyDataBlock_t block = { eDataEnd, NULL, 0, stream->block_context };
                            stream->block_cb(&block);
                        }
                        // After a separator there is always a header.
                        // If the header is 0 bytes, we are done.
                        stream->state = eHeader;
                        stream->header_size = 0;
                    } else if (buf[i] == stream->boundary[stream->match_state]) {
                        stream->match_state++;
                    } else if (stream->match_state != 0) {
                        switch(stream->state) {
                            case eHeader:
                                memcpy(stream->header + stream->header_size, stream->boundary, stream->match_state);
                                stream->header_size += stream->match_state;
                                break;
                            case eData:
                                // printf("copying bondary bytes to data %d\n", stream->match_state);
                                for(int j=0;j<stream->match_state;j++) {
                                    stream->data[stream->data_size++] = stream->boundary[j];
                                    if (stream->data_size == 4096) {
                                        expunge(stream);
                                    }
                                }
                                break;
                            default:
                                printf("unknown state %c\n", buf[i]);
                         }
                         stream->match_state = 0;
                    }
                } else {
                    // in header state
                    if (stream->match_state == 4) {
                        stream->match_state = 0;
                        // After the empty line of the header there is always data.
                        _ParseMultiPartHeader(stream);
                        // If the header is 0 bytes, we are done.
                        stream->state = eData;
                        stream->header_size = 0;
                    } else if (buf[i] == c_header_end[stream->match_state]) {
                        stream->match_state++;
                    } else {
                        stream->match_state = 0;
                    }
                }
                // where to store the byte?
                switch(stream->state) {
                    case eData:
                        if (stream->match_state == 0) {
                            stream->data[stream->data_size++] = buf[i];
                            if (stream->data_size == 4096) {
                                expunge(stream);
                            }
                        }
                        break;
                    case eHeader:
                        stream->header[stream->header_size++] = buf[i];
                        break;
                    case eDitch:
                        break;
                    default:
                        printf("store %d %c\n", stream->state, buf[i]);
                }
            }
            if (len == 0) {
                stream->state = eTerminated;
                if (stream->data_size) {
                    expunge(stream);
                    if (stream->block_cb) {
                        BodyDataBlock_t block = { eDataEnd, NULL, 0, stream->block_context };
                        stream->block_cb(&block);
                    }
                }
                if (stream->block_cb) {
                    BodyDataBlock_t block = { eTerminate, NULL, 0, stream->block_context };
                    stream->block_cb(&block);
                }
                if(stream->boundary) {
                    free(stream->boundary);
                }
                free(stream);
            }
            break;

        default:
            printf("Unexpected state filestream_in\n");

    }
    return len;
}

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

    if (req->BodySize) {
        ApiBody_t *body = malloc(sizeof(ApiBody_t));
        body->msg = res; // This function can write directly into the response data buffer (!)

        FileStream_t * stream = malloc(sizeof(FileStream_t));
        memset(stream, 0, sizeof(FileStream_t)); // also sets the state to eInit
        req->BodyCB = &filestream_in;
        req->BodyContext = stream;
        stream->type = req->ContentType;
        stream->block_cb = &ApiBody;
        stream->block_context = body; 
        filestream_in(stream, NULL, 0); // Initialize
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
            req->BodyCB = &filestream_in;
            FileStream_t * stream = malloc(sizeof(FileStream_t));
            memset(stream, 0, sizeof(FileStream_t)); // also sets the state to eInit
            req->BodyContext = stream;
            stream->type = req->ContentType;
            stream->block_cb = &attachment_block_debug;
            filestream_in(stream, NULL, 0); // Initialize
        }

        found = _ReadStaticFiles(req, res);
    }
#endif

    /* It is really not found. */
    if (found != 1)
        _NotFound(req, res);
}
