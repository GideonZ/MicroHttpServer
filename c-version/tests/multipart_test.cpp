#include <iostream>
#include <string>
#include <gtest/gtest.h>
#include <stdio.h>

#include "../lib/multipart.h"
#include "attachment.c" // including the 'C' file avoids the need for header and extern
#undef __cplusplus
#include "../lib/dump_hex.h"

class HttpMultipartTest : public ::testing::Test {
protected:
    // SetUp and TearDown executes for each test case.
    void SetUp() override {
    }

    void TearDown() override {
    }

    // Class members are accessible from test cases. Reinitiated before each test.

};

typedef struct {
    HTTPRespMessage *msg;
    char filename[128];
    int filesize;
} TestBody_t;

void TestBodyCB(BodyDataBlock_t *block)
{
    TestBody_t *body = (TestBody_t *)block->context;
    HTTPRespMessage *resp = body->msg;
    const char *sub;
    HTTPHeaderField *f;
    char temp[256];
    temp[0] = 0;
    switch(block->type) {
        case eStart:
            sprintf(temp, "Start\n");
            strcpy(body->filename, "WRONG!");
            break;
        case eDataStart:
            body->filesize = 0;
            strcpy(body->filename, "raw data");
            break;
        case eSubHeader:
            body->filesize = 0;
            strcpy(body->filename, "Unnamed");
            f = (HTTPHeaderField *)block->data;
            for(int i=0; i < block->length; i++) {
                if (strcasecmp(f[i].key, "Content-Disposition") == 0) {
                    // extract filename from value string, e.g. 'form-data; name="bestand"; filename="sample.html"'
                    sub = strstr(f[i].value, "filename=\"");
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
            sprintf(temp, "%s (Size: %d)\n", body->filename, body->filesize);
            break;
        case eTerminate:
            sprintf(temp, "End.\n");
            break;
        case eAbort:
            sprintf(temp, "Abort.\n");
            break;
    }
    int n = strlen(temp);
    char *p = (char *)resp->_buf + resp->_index;
    memcpy(p, temp, n);
    resp->_index += n;
}

///////////////////////////////////////////////////////////////
//                  MULTIPART TESTS                          //
///////////////////////////////////////////////////////////////
TEST_F(HttpMultipartTest, MultiPart1)
{
    int len = sizeof(post_rekwest)-1; // remove trailing 0;
    // Search end of header
    const char *data = strstr((const char *)post_rekwest, "\r\n\r\n");
    data += 4;
    int header_len = (int)(data - (const char *)post_rekwest);
    len -= header_len;
    
    HTTPReqMessage req;
    HTTPRespMessage resp;
    InitReqMessage(&req);
    InitRespMessage(&resp);

    req.ContentType = "multipart/form-data; boundary=5a8868354b79d47b-6e1ba8ed24fd99d1-25b65b0b92c4868d-d05927ea32311de5";

    TestBody_t bodyContext;
    bodyContext.msg = &resp;
    setup_multipart(&req, &TestBodyCB, &bodyContext);

    while(len) {
        int send = (len < 1000) ? len : 1000;
        //dump_hex_relative(data, send);
        req.BodyCB(req.BodyContext, (const uint8_t *)data, send);
        len -= send;
        data += send;
    }
    // Terminate
    req.BodyCB(req.BodyContext, NULL, 0);

    const char *expected = "Start\n"
        "Commando.sid (Size: 4126)\n"
        "Commando.ssl (Size: 22)\n"
        "End.\n";

    EXPECT_STREQ((const char *)resp._buf, expected);
}

TEST_F(HttpMultipartTest, MultiPart2)
{
    int len = sizeof(post_postman)-1; // remove trailing 0
    // Search end of header
    const char *data = strstr((const char *)post_postman, "\r\n\r\n");
    data += 4;
    int header_len = (int)(data - (const char *)post_postman);
    len -= header_len;

    HTTPReqMessage req;
    HTTPRespMessage resp;
    InitReqMessage(&req);
    InitRespMessage(&resp);

    req.ContentType = NULL;
    req.bodySize = 4126;
    req.bodyType = eTotalSize;

    TestBody_t bodyContext;
    bodyContext.msg = &resp;
    setup_multipart(&req, &TestBodyCB, &bodyContext);

    while(len) {
        int send = (len < 1000) ? len : 1000;
        //dump_hex_relative(data, send);
        req.BodyCB(req.BodyContext, (const uint8_t *)data, send);
        len -= send;
        data += send;
    }
    // Terminate
    req.BodyCB(req.BodyContext, NULL, 0);
    resp._buf[resp._index] = 0;

    const char *expected = "Start\n"
        "raw data (Size: 4126)\n"
        "End.\n";

    EXPECT_STREQ((const char *)resp._buf, expected);
}

// A connection dropped mid-body delivers a negative length; the absorber must
// forward eAbort and must NOT run the completion (eTerminate) path.
TEST_F(HttpMultipartTest, MultiPartAbortMidBody)
{
    const char *data = strstr((const char *)post_rekwest, "\r\n\r\n") + 4;

    HTTPReqMessage req;
    HTTPRespMessage resp;
    InitReqMessage(&req);
    InitRespMessage(&resp);
    req.ContentType = "multipart/form-data; boundary=5a8868354b79d47b-6e1ba8ed24fd99d1-25b65b0b92c4868d-d05927ea32311de5";

    TestBody_t bodyContext;
    bodyContext.msg = &resp;
    setup_multipart(&req, &TestBodyCB, &bodyContext);

    // Feed only the first chunk (a part is now in progress), then abort.
    req.BodyCB(req.BodyContext, (const uint8_t *)data, 1000);
    req.BodyCB(req.BodyContext, NULL, -1);
    resp._buf[resp._index] = 0;

    EXPECT_NE(strstr((const char *)resp._buf, "Abort.\n"), (const char *)NULL);
    EXPECT_EQ(strstr((const char *)resp._buf, "End.\n"), (const char *)NULL);
}

// A part header longer than the 1024-byte header buffer must be truncated
// safely (no overflow into data[]/fields[]/callbacks) and still parse and
// terminate cleanly.
TEST_F(HttpMultipartTest, MultiPartOverlongHeader)
{
    std::string longname(1500, 'A');
    std::string body = "--X\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"" + longname + "\"\r\n"
        "\r\n"
        "hello"
        "\r\n--X--\r\n";

    HTTPReqMessage req;
    HTTPRespMessage resp;
    InitReqMessage(&req);
    InitRespMessage(&resp);
    req.ContentType = "multipart/form-data; boundary=X";

    TestBody_t bodyContext;
    bodyContext.msg = &resp;
    setup_multipart(&req, &TestBodyCB, &bodyContext);

    int len = (int)body.size();
    const char *p = body.c_str();
    while (len) {
        int send = (len < 256) ? len : 256;
        req.BodyCB(req.BodyContext, (const uint8_t *)p, send);
        len -= send;
        p += send;
    }
    req.BodyCB(req.BodyContext, NULL, 0);
    resp._buf[resp._index] = 0;

    // The over-long header is truncated, so the part parses and the body still
    // terminates without corruption or a crash.
    EXPECT_NE(strstr((const char *)resp._buf, "End.\n"), (const char *)NULL);
}
