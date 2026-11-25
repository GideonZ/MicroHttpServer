#include "server.h"
#include <string.h>
#include <limits.h>

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
    hdr->Response = "";
    hdr->RawCopy = NULL;
    hdr->RawCopySize = 0;
    hdr->RawCopyLength = 0;
}

void InitReqMessage(HTTPReqMessage *req)
{
    req->usedAsResponseFromServer = 0;
    req->responseToHead = 0;
    req->responseStatus = 0;
    req->interimResponses = 0;
    req->trailerBytes = 0;
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
            memmove(req->_buf, p, avail);
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

static int response_token(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || (c && strchr("!#$%&'*+-.^_`|~", c));
}

static char *response_field(char *line)
{
    char *colon = strchr(line, ':');
    if (!colon || colon == line) return NULL;
    for (char *p = line; p < colon; ++p) if (!response_token((unsigned char)*p)) return NULL;
    *colon++ = 0;
    while (*colon == ' ' || *colon == '\t') ++colon;
    char *end = colon + strlen(colon);
    while (end > colon && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    for (char *p = colon; *p; ++p)
        if (((unsigned char)*p < 32 && *p != '\t') || (unsigned char)*p == 127) return NULL;
    return colon;
}

static int response_length(const char *p, size_t *length)
{
    if (!*p) return 0;
    size_t value = 0;
    for (; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (INT_MAX - (*p - '0')) / 10u) return 0;
        value = value * 10 + (*p - '0');
    }
    *length = value;
    return 1;
}

static void parse_response_header(HTTPReqMessage *req)
{
    HTTPReqHeader *hdr = &req->Header;
    char *line = hdr->_buffer;
    char *end = strstr(line, "\r\n");
    int length_seen = 0, transfer_seen = 0;
    size_t length = 0;
    req->protocol_state = eReq_Error; // Only a fully validated header clears this.
    if (!end || end - line < 12 || (memcmp(line, "HTTP/1.0 ", 9) && memcmp(line, "HTTP/1.1 ", 9))) return;
    if (line[9] < '1' || line[9] > '5' || line[10] < '0' || line[10] > '9' || line[11] < '0' || line[11] > '9') return;
    if (end - line > 12 && line[12] != ' ') return;
    for (char *p = line + 12; p < end; ++p)
        if (((unsigned char)*p < 32 && *p != '\t') || (unsigned char)*p == 127) return;
    req->responseStatus = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
    if (req->responseStatus == 101) return; // Protocol upgrades are not HTTP exchanges.
    *end = 0;
    line[8] = 0;
    hdr->Version = line;
    hdr->Response = line + 9;
    hdr->Method = HTTP_UNKNOWN;
    hdr->URI = "";
    hdr->FieldCount = 0;
    req->ContentType = NULL;
    for (line = end + 2; *line; line = end + 2) {
        end = strstr(line, "\r\n");
        if (!end) return;
        if (end == line) break;
        *end = 0;
        char *value = response_field(line);
        if (!value) return;
        if (_IsLengthHeader(line)) {
            size_t parsed;
            if (!response_length(value, &parsed) || (length_seen && parsed != length)) return;
            length_seen = 1; length = parsed;
        } else if (_IsTransferEncoding(line)) {
            if (transfer_seen || strcasecmp(value, "chunked")) return;
            transfer_seen = 1;
        } else if (_IsTypeField(line) && !req->ContentType) {
            req->ContentType = value;
        }
        // Storage is bounded, but every field still participates in framing validation.
        if (hdr->FieldCount < MAX_HEADER_FIELDS) {
            hdr->Fields[hdr->FieldCount].key = line;
            hdr->Fields[hdr->FieldCount++].value = value;
        }
    }
    if (length_seen && transfer_seen) return;
    req->bodySize = length;
    req->bodyType = transfer_seen ? eChunked : length_seen ? (length ? eTotalSize : eNoBody) : eUntilDisconnect;
    if (req->responseToHead || req->responseStatus < 200 || req->responseStatus == 204 || req->responseStatus == 304)
        req->bodyType = eNoBody;
    req->chunkState = eChunkHeader;
    req->trailerBytes = 0;
    req->BodyCB = NULL;
    req->BodyContext = NULL;
    req->protocol_state = eReq_HeaderDone;
}

void _ParseHeader(HTTPReqMessage *req)
{
    if (req->usedAsResponseFromServer) { parse_response_header(req); return; }
    char *lines[32] = {0};
    int line = 0;
    char *p = (char *)req->Header._buffer;

    // DebugMsg("\tParse Header\n");

    // First split the header buffer into lines by searching for the \r\n sequences
    // The method used here is to separate on \r and skip the following \n.
    for(line = 0; line < 32; line++) {
        lines[line] = strsep(&p, "\r");
        if (!p) {
            break;
        }
        p++; // skip the \n
        if (!lines[line] || ! *(lines[line])) {
            break;
        }
    }

    // Step 2: Get the verb, path and HTTP identifier from the first line
    // Split by space
    // VERB path HTTP/1.1
    req->Header.URI = "";
    req->Header.Response = "";
    req->Header.Version = NULL;
    req->Header.Method = HTTP_UNKNOWN;
    char *cur = lines[0];
    char *verb = strsep(&cur, " ");
    if (!req->usedAsResponseFromServer) {
        req->Header.Method = HaveMethod(verb);
        if (cur) {
            req->Header.URI = strsep(&cur, " ");
            req->Header.Version = cur; // can also be NULL
            DebugMsg("HTTP %s %s\n", verb, req->Header.URI ? req->Header.URI : "");
        }
    } else { // response: store response code
        // verb now holds the response code and explanation
        req->Header.Response = cur;
    }
    // Step 3: Split the remaining lines into fields.
    // All subsequent lines are in the form of KEY ": " VALUE, although the space is not mandatory by spec.
    // so leading spaces need to be trimmed, and the separator is simply ':'
    req->Header.FieldCount = 0;
    for(line = 1; line < 32; line++) {
        if (!lines[line] || ! *(lines[line])) {
            break;
        }
        if (req->Header.FieldCount >= MAX_HEADER_FIELDS) {
            break; // Fields[] is fixed-size; ignore any further header lines
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

    if ((req->Header.Method == HTTP_POST) || (req->usedAsResponseFromServer)) {
        int length_header_seen = 0;
        for (unsigned int i = 0; i < req->Header.FieldCount; i++) {
            if (_IsLengthHeader(req->Header.Fields[i].key)) {
                long len = strtol(req->Header.Fields[i].value, NULL, 0);
                length_header_seen = 1;
                // Only a positive Content-Length means there is a body. A negative
                // value would become a huge size_t (and a negative (int) length in
                // the body callback -> OOB copy); a zero length is simply no body.
                // In both cases leave bodyType as eNoBody so the request is never
                // driven into body processing: a 0-length body callback is treated
                // as stream termination and can double-free the body absorber.
                if (len > 0) {
                    req->bodyType = eTotalSize;
                    req->bodySize = (size_t)len;
                }
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
        // Fall back to "read the body until the peer disconnects" only when the
        // client gave no explicit framing at all. An explicit non-positive
        // Content-Length means "no body", so it must not become eUntilDisconnect.
        if ((req->bodyType == eNoBody) && !length_header_seen) {
            req->bodyType = eUntilDisconnect;
        }
    }
    req->BodyCB = NULL; // to be filled in by the application
    req->BodyContext = NULL; // to be filled in by the application
}

static int response_chunk_size(const char *p, size_t *size)
{
    size_t value = 0;
    int digits = 0;
    for (;;) {
        int digit = *p >= '0' && *p <= '9' ? *p - '0' :
            *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
        if (digit < 0) break;
        if (value > (INT_MAX - digit) / 16u) return 0;
        value = value * 16 + digit; ++p; ++digits;
    }
    if (!digits) return 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p++ != ';') return 0;
        while (*p == ' ' || *p == '\t') ++p;
        if (!response_token((unsigned char)*p)) return 0;
        while (response_token((unsigned char)*p)) ++p;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '=') {
            ++p;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == '"') {
                ++p;
                while (*p && *p != '"') {
                    if (*p == '\\') { if (!*++p) return 0; }
                    if (((unsigned char)*p < 32 && *p != '\t') || (unsigned char)*p == 127) return 0;
                    ++p;
                }
                if (*p++ != '"') return 0;
            } else {
                if (!response_token((unsigned char)*p)) return 0;
                while (response_token((unsigned char)*p)) ++p;
            }
        }
    }
    *size = value;
    return 1;
}

static int handle_response_chunked(HTTPReqMessage *req)
{
    for (;;) {
        if (req->chunkState == eChunkBody) {
            int available = req->_valid - req->_used;
            if ((size_t)available > req->chunkRemain) available = (int)req->chunkRemain;
            if (!available) return 1;
            if (req->BodyCB) req->BodyCB(req->BodyContext, req->_buf + req->_used, available);
            req->_used += available;
            req->chunkRemain -= available;
            if (req->protocol_state == eReq_Error) return -1;
            if (!req->chunkRemain) req->chunkState = eChunkTrailer;
        } else {
            char *line = GetLineFromBuffer(req);
            if (!line) {
                if (req->_valid - req->_used >= HTTP_BUFFER_SIZE) return -1;
                return 1;
            }
            if (req->chunkState == eChunkHeader) {
                if (!response_chunk_size(line, &req->chunkRemain)) return -1;
                req->chunkState = req->chunkRemain ? eChunkBody : eChunkFinalTrailer;
            } else if (req->chunkState == eChunkTrailer) {
                if (*line) return -1;
                req->chunkState = eChunkHeader;
            } else {
                req->trailerBytes += strlen(line) + 2;
                if (req->trailerBytes > HTTP_MAX_HEADER_SIZE) return -1;
                if (!*line) return 0;
                if (!response_field(line) || _IsLengthHeader(line) || _IsTransferEncoding(line)) return -1;
            }
        }
        if (req->_used == req->_valid) req->_used = req->_valid = 0;
    }
}

int _HandleChunked(HTTPReqMessage *req)
{
    if (req->usedAsResponseFromServer) return handle_response_chunked(req);
    while(1) {
        if (req->chunkState == eChunkHeader) {
            char *chunkline = GetLineFromBuffer(req);
            if (chunkline) {
                long remain = strtol(chunkline, NULL, 16);
                // DebugMsg("Chunkline read: '%s'; size = %d\n", chunkline, (int)remain);
                if (remain <= 0) {
                    return 0; // done, or reject a negative/invalid chunk size
                }
                req->chunkRemain = (size_t)remain;
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
        case eUntilDisconnect:
            if (req->usedAsResponseFromServer) {
                int available = req->_valid - req->_used;
                if (available && req->BodyCB) req->BodyCB(req->BodyContext, req->_buf + req->_used, available);
                req->_used = req->_valid = 0;
                return 1; // Only an explicit orderly EOF completes this body.
            }
            break;
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
    if (req->usedAsResponseFromServer && memchr(hdr->_buffer, 0, until >= 0 ? until : hdr->_buffer_valid)) {
        req->protocol_state = eReq_Error;
        return 0;
    }
    if (until >= 0) {
        
        // Application requires a raw copy of the header
        if (hdr->RawCopy) {
            hdr->RawCopyLength = (until <= hdr->RawCopySize) ? until : hdr->RawCopySize;
            memcpy(hdr->RawCopy, hdr->_buffer, hdr->RawCopyLength);
        }

        _ParseHeader(req); // Do we actually NEED to parse everything??
        new_bytes_used = until - valid; // old valid!
        if (req->protocol_state != eReq_Error) req->protocol_state = eReq_HeaderDone;
    } else {
        DebugMsg("Header not yet found.\n");
        new_bytes_used = cancopy; // bytes from input used, but not yet reached full header
        if (hdr->_buffer_valid == HTTP_MAX_HEADER_SIZE) req->protocol_state = eReq_HeaderTooBig;
    }
    req->_used += new_bytes_used;
    if (req->_used >= req->_valid) {
        req->_valid = 0;
        req->_used = 0;
    }
    //printf("%d bytes used. Leaving %d bytes to process later.\n", new_bytes_used, req->_valid - req->_used);

    return new_bytes_used;
}

uint8_t ProcessClientData(HTTPReqMessage *req, HTTPRespMessage *resp, HTTPREQ_CALLBACK callback)
{
    int n;
next_header:
    if (req->protocol_state == eReq_Header) {
        _GetHeader(req);
    }
    if (req->protocol_state == eReq_HeaderTooBig || req->protocol_state == eReq_Error) {
        return CLOSE_SOCKET;
    }
    if (req->protocol_state == eReq_HeaderDone) {
        if (req->usedAsResponseFromServer && req->responseStatus < 200) {
            if (++req->interimResponses > 8) { req->protocol_state = eReq_Error; return CLOSE_SOCKET; }
            void *raw = req->Header.RawCopy;
            int raw_size = req->Header.RawCopySize;
            InitReqHeader(&req->Header);
            req->Header.RawCopy = raw;
            req->Header.RawCopySize = raw_size;
            req->protocol_state = eReq_Header;
            if (req->_valid > req->_used) goto next_header;
            return READING_SOCKET;
        }
        callback(req, resp); // early call to processor to set up body absorber
        if (req->protocol_state == eReq_Error) return CLOSE_SOCKET;
        if (req->bodyType != eNoBody) {
            req->protocol_state = eReq_Body;
            if (req->_valid == req->_used) {
                req->_valid = req->_used = 0;
                return READING_SOCKET;
            }
        } else {
            if (req->usedAsResponseFromServer) req->protocol_state = eReq_Complete;
            return WRITING_SOCKET;
        }
    }
    if (req->protocol_state == eReq_Body) {
        n = _GetBody(req);
        if (n < 0 || req->protocol_state == eReq_Error) {
            req->protocol_state = eReq_Error;
            return CLOSE_SOCKET;
        }
        if (n > 0) {
            return READING_SOCKET;
        } else if(n == 0) {
            if (req->usedAsResponseFromServer) req->protocol_state = eReq_Complete;
            // Send a Terminate
            if (req->BodyCB) {
                req->BodyCB(req->BodyContext, NULL, 0);
                /* The absorber freed itself on this terminate; clear the pointers
                   so the connection-close path does not deliver a second (abort)
                   callback to a freed context. */
                req->BodyCB = NULL;
                req->BodyContext = NULL;
            }
            // Switch to writing the response
            if(resp) {
                resp->_buf[resp->_index] = '\0';
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

int FinishClientResponse(HTTPReqMessage *req)
{
    if (!req->usedAsResponseFromServer || req->protocol_state != eReq_Body || req->bodyType != eUntilDisconnect) return 0;
    if (req->BodyCB) req->BodyCB(req->BodyContext, NULL, 0);
    req->BodyCB = NULL;
    req->BodyContext = NULL;
    if (req->protocol_state == eReq_Error) return 0;
    req->protocol_state = eReq_Complete;
    return 1;
}
