#include <string>
#include <gtest/gtest.h>
#include "../lib/server.h"

struct Capture { std::string body; int completed = 0; };
static int capture(void *context, const uint8_t *bytes, int size)
{
    Capture *result = static_cast<Capture *>(context);
    if (size > 0) result->body.append(reinterpret_cast<const char *>(bytes), size);
    else if (!size) ++result->completed;
    return size;
}
static void attach(HTTPReqMessage *req, HTTPRespMessage *)
{
    req->BodyCB = capture;
    req->BodyContext = req->userContext;
}
static void feed(HTTPReqMessage& req, const std::string& bytes, int part)
{
    size_t offset = 0;
    while (offset < bytes.size() && req.protocol_state != eReq_Complete && req.protocol_state != eReq_Error) {
        int n = bytes.size() - offset;
        if (n > part) n = part;
        if (n > HTTP_BUFFER_SIZE - req._valid) n = HTTP_BUFFER_SIZE - req._valid;
        ASSERT_GT(n, 0);
        memcpy(req._buf + req._valid, bytes.data() + offset, n);
        req._valid += n; offset += n;
        ProcessClientData(&req, NULL, attach);
    }
}
TEST(ResponseFraming, OrderlyEofIsExplicit)
{
    for (int part : {1, 7, 1024}) {
        HTTPReqMessage req; InitReqMessage(&req); req.usedAsResponseFromServer = 1;
        Capture result; req.userContext = &result;
        feed(req, "HTTP/1.1 200 OK\r\n\r\nhello", part);
        EXPECT_EQ(req.protocol_state, eReq_Body);
        EXPECT_EQ(result.body, "hello"); EXPECT_EQ(result.completed, 0);
        EXPECT_EQ(FinishClientResponse(&req), 1); EXPECT_EQ(result.completed, 1);
        EXPECT_EQ(FinishClientResponse(&req), 0); EXPECT_EQ(result.completed, 1);
    }
}
TEST(ResponseFraming, TruncatedLengthCannotCompleteAtEof)
{
    HTTPReqMessage req; InitReqMessage(&req); req.usedAsResponseFromServer = 1;
    Capture result; req.userContext = &result;
    feed(req, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nhi", 1);
    EXPECT_EQ(FinishClientResponse(&req), 0); EXPECT_EQ(result.completed, 0);
}
TEST(ResponseFraming, HeadAndBodylessCompleteWithoutRead)
{
    for (const char *status : {"200 OK", "204 No Content", "304 Not Modified"}) {
        HTTPReqMessage req; InitReqMessage(&req); req.usedAsResponseFromServer = 1;
        req.responseToHead = status[0] == '2' && status[2] == '0';
        Capture result; req.userContext = &result;
        feed(req, std::string("HTTP/1.1 ") + status + "\r\nContent-Length: 999\r\n\r\n", 1);
        EXPECT_EQ(req.protocol_state, eReq_Complete); EXPECT_TRUE(result.body.empty());
    }
}
TEST(ResponseFraming, FieldsAfterStorageLimitStillFrame)
{
    HTTPReqMessage req; InitReqMessage(&req); req.usedAsResponseFromServer = 1;
    Capture result; req.userContext = &result;
    std::string wire = "HTTP/1.1 200 OK\r\n";
    for (int i = 0; i < 35; ++i) wire += "X-Extra: value\r\n";
    feed(req, wire + "Content-Length: 2\r\n\r\nhi", 7);
    EXPECT_EQ(req.Header.FieldCount, MAX_HEADER_FIELDS);
    EXPECT_EQ(req.protocol_state, eReq_Complete); EXPECT_EQ(result.body, "hi");
}
TEST(ResponseFraming, ConflictingFramingIsAnError)
{
    for (const char *extra : {"Content-Length: 3", "Transfer-Encoding: chunked"}) {
        HTTPReqMessage req; InitReqMessage(&req); req.usedAsResponseFromServer = 1;
        Capture result; req.userContext = &result;
        feed(req, std::string("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n") + extra + "\r\n\r\nhi", 1);
        EXPECT_EQ(req.protocol_state, eReq_Error); EXPECT_TRUE(result.body.empty());
    }
}
TEST(ResponseFraming, ZeroChunkRequiresTrailerTerminator)
{
    HTTPReqMessage req; InitReqMessage(&req); req.usedAsResponseFromServer = 1;
    Capture result; req.userContext = &result;
    feed(req, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n", 1);
    EXPECT_EQ(req.protocol_state, eReq_Body); EXPECT_EQ(FinishClientResponse(&req), 0);
    feed(req, "X-Trailer: yes\r\n\r\n", 1);
    EXPECT_EQ(req.protocol_state, eReq_Complete); EXPECT_EQ(result.completed, 1);
}
TEST(ResponseFraming, InterimResponsesAreBounded)
{
    HTTPReqMessage req; InitReqMessage(&req); req.usedAsResponseFromServer = 1;
    Capture result; req.userContext = &result;
    std::string wire;
    for (int i = 0; i < 9; ++i) wire += "HTTP/1.1 100 Continue\r\n\r\n";
    feed(req, wire, 1024);
    EXPECT_EQ(req.protocol_state, eReq_Error); EXPECT_TRUE(result.body.empty());
}
TEST(ResponseFraming, FinalStatusReplacesInterimRawHeaders)
{
    HTTPReqMessage req; InitReqMessage(&req); req.usedAsResponseFromServer = 1;
    Capture result; req.userContext = &result;
    char raw[255]; req.Header.RawCopy = raw; req.Header.RawCopySize = sizeof(raw);
    const std::string final = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    feed(req, "HTTP/1.1 103 Early Hints\r\n\r\n" + final, 1024);
    EXPECT_EQ(req.protocol_state, eReq_Complete);
    EXPECT_EQ(std::string(raw, req.Header.RawCopyLength), final);
}
