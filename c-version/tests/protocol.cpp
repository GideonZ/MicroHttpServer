#include <iostream>
#include <string>
#include <gtest/gtest.h>
#include <stdio.h>

//extern "C" {
    #include "../lib/server.h" // TODO: Should become 'http_protocol.h'
//}
#include "attachment.c" // including the 'C' file avoids the need for header and extern

class HttpProtocolTest : public ::testing::Test {
protected:
    // SetUp and TearDown executes for each test case.
    void SetUp() override {
    }

    void TearDown() override {
    }

    // Class members are accessible from test cases. Reinitiated before each test.

};

///////////////////////////////////////////////////////////////
//                  PROTOCOL TESTS                           //
///////////////////////////////////////////////////////////////
int FillBuffer(HTTPReqMessage &req, int max, uint8_t *data, int remain)
{
    int space = HTTP_BUFFER_SIZE - req._valid;
    int couldcopy = (remain > space) ? space : remain;
    int maycopy = (couldcopy > max) ? max : couldcopy;
    memcpy(req._buf + req._valid, data, maycopy);
    req._valid += maycopy;
    return maycopy;
}

int total;
int last;
int readsize = 354;

int bodycb(void *context, const uint8_t *buf, int len)
{
    EXPECT_LE(len, readsize);
    total += len;
    last = len;
    return len;
}

void callback(HTTPReqMessage *req, HTTPRespMessage *resp)
{
    printf("Callback!\n");
    total = 0;
    last = -1;
    req->BodyCB = &bodycb;
}

TEST_F(HttpProtocolTest, Post_With_HyperLibrary)
{
    uint8_t *data = (uint8_t *)post_hyper;
    int len = sizeof(post_hyper)-1; // remove trailing 0;

    HTTPReqMessage req;
    HTTPRespMessage resp;
    InitReqMessage(&req);
    InitRespMessage(&resp);

    while(len) {
        int sent = FillBuffer(req, readsize, data, len);
        EXPECT_NE(0, sent);
        uint8_t ret = ProcessClientData(&req, &resp, &callback);
        if (sent == len) {
            EXPECT_EQ(ret, WRITING_SOCKET);
        } else {
            EXPECT_EQ(ret, READING_SOCKET);
        }
        len -= sent;
        data += sent;
    }

    EXPECT_EQ(req.Header.Method, HTTP_POST);
    EXPECT_STREQ(req.Header.URI, "/v1/runner");
    EXPECT_EQ(req.bodyType, eChunked);
    EXPECT_EQ(total, 4442);
    EXPECT_EQ(last, 0); // make sure the stream terminates
}

TEST_F(HttpProtocolTest, Post_With_RekwestLibrary)
{
    uint8_t *data = (uint8_t *)post_rekwest;
    int len = sizeof(post_rekwest)-1; // remove trailing 0;

    HTTPReqMessage req;
    HTTPRespMessage resp;
    InitReqMessage(&req);
    InitRespMessage(&resp);

    while(len) {
        int sent = FillBuffer(req, readsize, data, len);
        EXPECT_NE(0, sent);
        uint8_t ret = ProcessClientData(&req, &resp, &callback);
        if (sent == len) {
            EXPECT_EQ(ret, WRITING_SOCKET);
        } else {
            EXPECT_EQ(ret, READING_SOCKET);
        }
        len -= sent;
        data += sent;
    }

    EXPECT_EQ(req.Header.Method, HTTP_POST);
    EXPECT_STREQ(req.Header.URI, "/v1/runner");
    EXPECT_EQ(req.bodyType, eTotalSize);
    EXPECT_EQ(total, 4589);
    EXPECT_EQ(last, 0); // make sure the stream terminates
}

TEST_F(HttpProtocolTest, Post_With_AttoLibrary)
{
    uint8_t *data = (uint8_t *)post_atto;
    int len = sizeof(post_atto)-1; // remove trailing 0;

    HTTPReqMessage req;
    HTTPRespMessage resp;
    InitReqMessage(&req);
    InitRespMessage(&resp);
    while(len) {
        int sent = FillBuffer(req, readsize, data, len);
        EXPECT_NE(0, sent);
        uint8_t ret = ProcessClientData(&req, &resp, &callback);
        if (sent == len) {
            EXPECT_EQ(ret, WRITING_SOCKET);
        } else {
            EXPECT_EQ(ret, READING_SOCKET);
        }
        len -= sent;
        data += sent;
    }

    EXPECT_EQ(req.Header.Method, HTTP_POST);
    EXPECT_STREQ(req.Header.URI, "/v1/runner");
    EXPECT_EQ(req.bodyType, eChunked);
    EXPECT_EQ(total, 4436);
    EXPECT_EQ(last, 0); // make sure the stream terminates
}

TEST_F(HttpProtocolTest, Post_With_Postman)
{
    uint8_t *data = (uint8_t *)post_postman;
    int len = sizeof(post_postman)-1; // remove trailing 0;

    HTTPReqMessage req;
    HTTPRespMessage resp;
    InitReqMessage(&req);
    InitRespMessage(&resp);
    while(len) {
        int sent = FillBuffer(req, readsize, data, len);
        EXPECT_NE(0, sent);
        uint8_t ret = ProcessClientData(&req, &resp, &callback);
        if (sent == len) {
            EXPECT_EQ(ret, WRITING_SOCKET);
        } else {
            EXPECT_EQ(ret, READING_SOCKET);
        }
        len -= sent;
        data += sent;
    }

    EXPECT_EQ(req.Header.Method, HTTP_POST);
    EXPECT_STREQ(req.Header.URI, "/v1/runners");
    EXPECT_EQ(req.bodyType, eTotalSize);
    EXPECT_EQ(total, 4126); // RAW size of Commando.sid
    EXPECT_EQ(last, 0); // make sure the stream terminates
}

TEST_F(HttpProtocolTest, Post_Of_BMX)
{
    uint8_t *data = (uint8_t *)bmx;
    int len = sizeof(bmx)-1; // remove trailing 0;

    HTTPReqMessage req;
    HTTPRespMessage resp;
    InitReqMessage(&req);
    InitRespMessage(&resp);
    while(len) {
        int sent = FillBuffer(req, readsize, data, len);
        EXPECT_NE(0, sent);
        uint8_t ret = ProcessClientData(&req, &resp, &callback);
        if (sent == len) {
            EXPECT_EQ(ret, WRITING_SOCKET);
        } else {
            EXPECT_EQ(ret, READING_SOCKET);
        }
        len -= sent;
        data += sent;
    }

    EXPECT_EQ(req.Header.Method, HTTP_POST);
    EXPECT_STREQ(req.Header.URI, "/v1/runners:sidplay");
    EXPECT_EQ(req.bodyType, eChunked);
    EXPECT_EQ(total, 11211); // RAW size of BMX
    EXPECT_EQ(last, 0); // make sure the stream terminates
}
