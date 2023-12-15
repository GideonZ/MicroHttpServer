#include "server.h"
#include <string.h>

HTTPMethod HaveMethod(char *method)
{
    HTTPMethod m;

    if (memcmp(method, "GET", 3) == 0)
        m = HTTP_GET;
    else if (memcmp(method, "POST", 4) == 0)
        m = HTTP_POST;
    else if (memcmp(method, "PUT", 3) == 0)
        m = HTTP_PUT;
    else if (memcmp(method, "DELETE", 6) == 0)
        m = HTTP_DELETE;
    else
        m = HTTP_GET;

    return m;
}

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
    req->bodyType = eNoBody;
    req->bodySize = 0;
    req->userContext = NULL;
    InitReqHeader(&(req->Header));
}

void InitRespMessage(HTTPRespMessage *resp)
{
    resp->BodyCB = NULL;
    resp->_index = 0;
}

char *GetLineFromBuffer(HTTPReqMessage *req)
{
    req->_buf[req->_valid] = '\0'; // allowed!
    char *p = (char *)req->_buf + req->_used;
    char *pfound = strstr(p, "\r\n");
    if (pfound) {
        *pfound = '\0';
        int len = (int)(pfound - p);
        req->_used += len + 2;
        return p;
    }
    // not found; just clear all data before _used, to make space for more
    // in case that there is less than 256 bytes free in the buffer
    if (req->_used > 0) {
        if ((HTTP_BUFFER_SIZE - req->_valid) < 256) {
            int avail = req->_valid - req->_used;
            DebugMsg("Moving %d bytes (%p -> %p)\n", avail, p, req->_buf);
            memcpy(req->_buf, p, avail);
            req->_used = 0;
            req->_valid = avail;
        }
    }
    return NULL;
}

int _IsLengthHeader(const char *key)
{
    return !strcasecmp(key, "content-length");
}

int _IsTypeField(const char *key)
{
    return !strcasecmp(key, "content-type");
}

int _IsTransferEncoding(const char *key)
{
    return !strcasecmp(key, "transfer-encoding");
}

void _ParseHeader(HTTPReqMessage *req)
{
    char *lines[32];
    int line = 0;
    char *p = (char *)req->Header._buffer;

    DebugMsg("\tParse Header\n");

    // First split the header buffer into lines by searching for the \r\n sequences
    // The method used here is to separate on \r and skip the following \n.
    for(line = 0; line < 32; line++) {
        lines[line] = strsep(&p, "\r");
        if (!p) {
            break;
        }
        p++; // skip the \n
        if (! *(lines[line])) {
            break;
        }
    }

    // Step 2: Get the verb, path and HTTP identifier from the first line
    // Split by space
    // VERB path HTTP/1.1
    char *cur = lines[0];
    char *verb = strsep(&cur, " ");
    req->Header.Method = HaveMethod(verb);
    if (cur) {
        req->Header.URI = strsep(&cur, " ");
        req->Header.Version = cur; // can also be NULL
    } else {
        req->Header.URI = "";
        req->Header.Version = NULL;
    }

    // Step 3: Split the remaining lines into fields.
    // All subsequent lines are in the form of KEY ": " VALUE, although the space is not mandatory by spec.
    // so leading spaces need to be trimmed, and the separator is simply ':'
    req->Header.FieldCount = 0;
    for(line = 1; line < 32; line++) {
        if (! *(lines[line])) {
            break;
        }
        p = lines[line];
        req->Header.Fields[req->Header.FieldCount].key = strsep(&p, ":");
        if (p) {
            while(*p == ' ') {
                p++;
            }
            req->Header.Fields[req->Header.FieldCount].value = p;
            // printf("'%s' -> '%s'\n", req->Header.Fields[req->Header.FieldCount].key, req->Header.Fields[req->Header.FieldCount].value);
            req->Header.FieldCount++;
        } else {
            break;
        }
    }

    // Step 4: Determine how much body data is required for this request
    // If the Transfer-Encoding is set to 'chunked', the body size is set to a high
    // value, such that we know that there is body data coming, and for the time
    // being it is not yet complete.

    req->bodyType = eNoBody;
    req->bodySize = 0;
    req->ContentType = NULL;

    // if (req->Header.Method == HTTP_POST) {
        for (unsigned int i = 0; i < req->Header.FieldCount; i++) {
            if (_IsLengthHeader(req->Header.Fields[i].key)) {
                req->bodyType = eTotalSize;
                req->bodySize = strtol(req->Header.Fields[i].value, NULL, 0);
                break;
            }
        }
        for (unsigned int i = 0; i < req->Header.FieldCount; i++) {
            if (_IsTypeField(req->Header.Fields[i].key)) {
                req->ContentType = req->Header.Fields[i].value;
                break;
            }
        }
        for (unsigned int i = 0; i < req->Header.FieldCount; i++) {
            if (_IsTransferEncoding(req->Header.Fields[i].key)) {
                if (strstr(req->Header.Fields[i].value, "chunked") != NULL) {
                    req->bodyType = eChunked;
                    req->chunkState = eChunkHeader;
                } else {
                    printf("Unknown Encoding: %s\n", req->Header.Fields[i].value);
                }
                break;
            }
        }
        if (req->bodyType == eNoBody) {
            req->bodyType = eUntilDisconnect;
        }
    // }
    req->BodyCB = NULL; // to be filled in by the application
    req->BodyContext = NULL; // to be filled in by the application
}

int _HandleChunked(HTTPReqMessage *req)
{
    while(1) {
        if (req->chunkState == eChunkHeader) {
            char *chunkline = GetLineFromBuffer(req);
            if (chunkline) {
                req->chunkRemain = strtol(chunkline, NULL, 16);
                // DebugMsg("Chunkline read: '%s'; size = %d\n", chunkline, (int)req->chunkRemain);
                if (req->chunkRemain == 0) {
                    return 0; // done
                }
                req->chunkState = eChunkBody;
            } else {
                DebugMsg("Chunkline could not be read. %d / %d\n", req->_used, req->_valid);
                return 1; // did something but need more data
            }
        }

        if (req->chunkState == eChunkBody) {
            uint8_t *p = req->_buf + req->_used;
            int n = req->_valid - req->_used;
            if (n == 0) {
                return 1;
            }
            int available = (n > (int)req->chunkRemain) ? (int)req->chunkRemain : n;
            if (req->BodyCB) {
                req->BodyCB(req->BodyContext, p, available);
            } else {
                DebugMsg("\tData ditched, no callback.\n");
            }
            req->_used += available;
            req->chunkRemain -= available;
            if (req->chunkRemain <= 0) {
                req->chunkState = eChunkTrailer;
            }
            if (req->_used == req->_valid) {
                req->_valid = 0;
                req->_used = 0;
                return 1; // need more data
            }
        }

        if (req->chunkState == eChunkTrailer) {
            char *chunkline = GetLineFromBuffer(req);
            if (chunkline) {
                req->chunkState = eChunkHeader;
            } else {
                return 1; // need more data
            }
        }
    }
}

int _HandleContentSize(HTTPReqMessage *req)
{
    uint8_t *p = req->_buf + req->_used;
    int n = req->_valid - req->_used;
    int available = (n > (int)req->bodySize) ? (int)req->bodySize : n;
    if (req->BodyCB) {
        req->BodyCB(req->BodyContext, p, available);
    } else {
        DebugMsg("\tData ditched, no callback.\n");
    }
    req->_used += available;
    req->bodySize -= available;
    if (req->_used == req->_valid) {
        req->_valid = 0;
        req->_used = 0;
    }
    if (req->bodySize <= 0) {
        return 0; // done!
    }
    return 1; // need more data
}

int _GetBody(HTTPReqMessage *req)
{
    switch(req->bodyType) {
        case eChunked:
            return _HandleChunked(req);
        case eTotalSize:
            return _HandleContentSize(req);
        default:
            DebugMsg("Don't know how to handle body.\n");
    }
    return 0;
}

int _TestHeaderComplete(HTTPReqHeader *hdr)
{
    char *pfound = strstr(hdr->_buffer, "\r\n\r\n");
    if (pfound != NULL) {
        return 4 + (int)(pfound - hdr->_buffer);
    }
    return -1;
}

int _GetHeader(HTTPReqMessage *req)
{
    char *p = (char *)req->_buf;
    int n = req->_valid - req->_used;
    p += req->_used;
    HTTPReqHeader *hdr = &(req->Header);

    int valid = hdr->_buffer_valid;
    int space = HTTP_MAX_HEADER_SIZE - valid;
    int cancopy = (n > space) ? space : n;

    if (n == 0) {
        req->protocol_state = eReq_HeaderTooBig;
        return 0;
    }

    //printf("Copying %d bytes to offset %d\n", cancopy, valid);
    memcpy(hdr->_buffer + valid, p, cancopy);
    hdr->_buffer_valid += cancopy; // new valid!
    // make it a null terminated string, so we know until where data was read later on, without
    // checking 'n' in every string operation. This is allowed, because we allocated 4 bytes more in the buffer
    hdr->_buffer[hdr->_buffer_valid] = '\0';

    int new_bytes_used;
    int until = _TestHeaderComplete(&req->Header);
    if (until >= 0) {
        _ParseHeader(req); // Do we actually NEED to parse everything??
        new_bytes_used = until - valid; // old valid!
        req->protocol_state = eReq_HeaderDone;
    } else {
        DebugMsg("Header not yet found.\n");
        new_bytes_used = cancopy; // bytes from input used, but not yet reached full header
    }
    if (new_bytes_used >= req->_valid) {
        req->_valid = 0;
        req->_used = 0;
    } else {
        req->_used = new_bytes_used;
    }
    //printf("%d bytes used. Leaving %d bytes to process later.\n", new_bytes_used, req->_valid - req->_used);

    return new_bytes_used;
}

uint8_t ProcessClientData(HTTPReqMessage *req, HTTPRespMessage *resp, HTTPREQ_CALLBACK callback)
{
    int n;

    if (req->protocol_state == eReq_Header) {
        _GetHeader(req);
    }
    if (req->protocol_state == eReq_HeaderTooBig) {
        return CLOSE_SOCKET;
    }
    if (req->protocol_state == eReq_HeaderDone) {
        callback(req, resp); // early call to processor to set up body absorber
        if (req->bodyType != eNoBody) {
            req->protocol_state = eReq_Body;
            if (req->_valid == req->_used) {
                req->_valid = req->_used = 0;
                return READING_SOCKET;
            }
        } else {
            return WRITING_SOCKET;
        }
    }
    if (req->protocol_state == eReq_Body) {
        n = _GetBody(req);
        if (n > 0) {
            return READING_SOCKET;
        } else if(n == 0) {
            // Send a Terminate
            if (req->BodyCB) {
                req->BodyCB(req->BodyContext, NULL, 0);
            }
            // Switch to writing the response
            if(resp) {
                resp->_buf[resp->_index] = '\0';
                printf("Reponse:\n%s\n", resp->_buf);
                return WRITING_SOCKET;
            } else {
                return CLOSE_SOCKET; // no response
            }
        } else {
            return CLOSE_SOCKET; // error
        }
    }
    return READING_SOCKET;
}
