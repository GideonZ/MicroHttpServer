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
    }
    int n = strlen(temp);
    char *p = (char *)resp->_buf + resp->_index;
    memcpy(p, temp, n);
    resp->_index += n;
}

///////////////////////////////////////////////////////////////
//                  PROTOCOL TESTS                           //
///////////////////////////////////////////////////////////////
TEST_F(HttpMultipartTest, MultiPart1)
{
    int len = sizeof(post_rekwest);
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
        int send = (len < 100) ? len : 100;
        //dump_hex_relative(data, send);
        req.BodyCB(req.BodyContext, (const uint8_t *)data, send);
        len -= send;
        data += send;
    }
    // Terminate
    req.BodyCB(req.BodyContext, NULL, 0);

    resp._buf[resp._index] = '\0';

    const char *expected = "Start\n"
        "Commando.sid (Size: 4126)\n"
        "Commando.ssl (Size: 22)\n"
        "End.\n";

    EXPECT_STREQ((const char *)resp._buf, expected);
}
