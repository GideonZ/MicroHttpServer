
#include "multipart.h"
#include <stdio.h>
#include <string.h>

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

// A multipart form is structured like this:
// (--, SEPARATOR, \r\n, (HEADER FIELD, \r\n)+, \r\n, DATA)+), --, SEPARATOR, \r\n.
// When the separator to search for is extended with as --SEP\r\n, and the \r\n are included in the header fields, this
// simplifies to:
// (SEPARATOR, (HEADER FIELD)+, EMPTY_LINE, DATA)+, SEPARATOR.
//
// The chosen approach is pattern searching on the fly. In this approach, the separator search state machine is extended
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
    HTTPHeaderField *f;
    switch(block->type) {
        case eStart:
            printf("--- Start of Body --- (Type: %s)\n", block->data);
            break;
        case eDataStart:
            printf("--- Raw Data Start ---\n");
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

static int filestream_in(void *context, const uint8_t *buf, int len)
{
    FileStream_t *stream = (FileStream_t *)context;

    static const char c_header_end[5] = "\r\n\r\n";
    const char *b;

    switch (stream->state) {
        case eInit: // the first time that this function is called is to set it up
        // after the request header has been parsed. It is called without data.
            if (stream->block_cb) {
                BodyDataBlock_t block = { eStart, stream->type, (int)strlen(stream->type), stream->block_context };
                stream->block_cb(&block);
            }
            if (!strncasecmp(stream->type, "multipart", 9)) { // 9 chars
                b = strstr(stream->type, "boundary="); // 9 chars
                if (b) {
                    len = strlen(b+9);
                    stream->boundary = (char *)malloc(len + 5); // \r\n-- + len + \r\n\0
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
            if (stream->block_cb) {
                BodyDataBlock_t block = { eDataStart, stream->type, (int)strlen(stream->type), stream->block_context };
                stream->block_cb(&block);
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
                                // printf("copying boundary bytes to data %d (pos in data: %d)\n", stream->match_state, i);
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
                        if (buf[i] == stream->boundary[0]) {
                            stream->match_state = 1;
                        } else {
                            stream->match_state = 0;
                        }
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

void setup_multipart(HTTPReqMessage *req, BODY_DATABLOCK_CB data_cb, void *data_context)
{
    req->BodyCB = &filestream_in;
    FileStream_t *stream = (FileStream_t *)malloc(sizeof(FileStream_t));
    memset(stream, 0, sizeof(FileStream_t)); // also sets the state to eInit
    req->BodyContext = stream;
    stream->type = req->ContentType ? req->ContentType : "";
    stream->block_cb = data_cb;
    stream->block_context = data_context;
    filestream_in(stream, NULL, 0); // Initialize
}
