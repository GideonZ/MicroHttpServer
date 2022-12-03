#include <iostream>
#include <string>
#include <gtest/gtest.h>
#include <stdio.h>

#include "../lib/url.h"

// The fixture for testing class Foo.
class RouteTest : public ::testing::Test {
protected:
    // SetUp and TearDown executes for each test case.

    void SetUp() override {
    }

    void TearDown() override {
    }

    void MakeComponentParts(struct UrlComponents *c) {
        method = c->method;
        apiversion = c->apiversion;
        route = c->route;
        path = c->path;
        command = c->command;
        querystring = c->querystring;
    }

    // Class members are accessible from test cases. Reinitiated before each test.

    std::string method;
    std::string apiversion;
    std::string route;
    std::string path;
    std::string command;
    std::string querystring;

    const size_t psize = sizeof(struct Parameter);

    struct Parameter *parameters;
    size_t len;
};


///////////////////////////////////////////////////////////////
//                  URL COMPONENTS TESTS                     //
///////////////////////////////////////////////////////////////

TEST_F(RouteTest, ParseUrl_RoutePathCommandQuerystring) {
    struct UrlComponents *c;

    c = parse_url("/v1/files/some/path/to/disk.d64:createDiskImage?type=d64&format=json");

    MakeComponentParts(c);

    EXPECT_EQ("GET", method);
    EXPECT_EQ("v1", apiversion);
    EXPECT_EQ("files", route);
    EXPECT_EQ("some/path/to/disk.d64", path);
    EXPECT_EQ("createDiskImage", command);
    EXPECT_EQ("type=d64&format=json", querystring);

    delete_url_components(c);
}

TEST_F(RouteTest, ParseUrl_Route) {
    struct UrlComponents *c;

    c = parse_url("/v1/files");

    MakeComponentParts(c);

    EXPECT_EQ("GET", method);
    EXPECT_EQ("files", route);
    EXPECT_EQ("", path);
    EXPECT_EQ("", command);
    EXPECT_EQ("", querystring);

    delete_url_components(c);
}

TEST_F(RouteTest, ParseUrl_Empty) {
    struct UrlComponents *c;

    c = parse_url("");
    EXPECT_EQ(nullptr, c);
}

TEST_F(RouteTest, ParseUrl_RoutePath) {
    struct UrlComponents *c;

    c = parse_url("v1/route/some/path/");

    MakeComponentParts(c);

    EXPECT_EQ("GET", method);
    EXPECT_EQ("route", route);
    EXPECT_EQ("some/path/", path);
    EXPECT_EQ("", command);
    EXPECT_EQ("", querystring);

    delete_url_components(c);
}

TEST_F(RouteTest, ParseUrl_RouteCommand) {
    struct UrlComponents *c;

    c = parse_url("v1/route:command");

    MakeComponentParts(c);

    EXPECT_EQ("GET", method);
    EXPECT_EQ("route", route);
    EXPECT_EQ("", path);
    EXPECT_EQ("command", command);
    EXPECT_EQ("", querystring);

    delete_url_components(c);
}

TEST_F(RouteTest, ParseUrl_RouteQuerystring) {
    struct UrlComponents *c;

    c = parse_url("v1/route?querystring");

    MakeComponentParts(c);

    EXPECT_EQ("GET", method);
    EXPECT_EQ("route", route);
    EXPECT_EQ("", path);
    EXPECT_EQ("", command);
    EXPECT_EQ("querystring", querystring);

    delete_url_components(c);
}

TEST_F(RouteTest, ParseUrl_UnsupportedApiVersion) {
    struct UrlComponents *c;

    c = parse_url("/v64/route?querystring");

    EXPECT_EQ(nullptr, c);
}

///////////////////////////////////////////////////////////////
//                  QUERYSTRING TESTS                        //
///////////////////////////////////////////////////////////////

TEST_F(RouteTest, ParseQS_Empty) {
    char q[] = "";
    parse_querystring(q, &len);

    EXPECT_EQ(0, len);
}

TEST_F(RouteTest, ParseQS_OneParameter) {
    char q[] = "hej=foo";
    parameters = parse_querystring(q, &len);

    EXPECT_EQ(1, len);
    EXPECT_EQ("hej", std::string(parameters->name));
    EXPECT_EQ("foo", std::string(parameters->value));
}

TEST_F(RouteTest, ParseQS_NoPairEndAmp) {
    char q[] = "abc&";
    parameters = parse_querystring(q, &len);

    EXPECT_EQ(2, len);
    EXPECT_EQ("abc", std::string(parameters->name));
    EXPECT_EQ("", std::string(parameters->value));
    EXPECT_EQ("", std::string((parameters+psize)->name));
    EXPECT_EQ("", std::string((parameters+psize)->value));
}

TEST_F(RouteTest, ParseQS_TwoNoPair) {
    char q[] = "one&two";
    parameters = parse_querystring(q, &len);

    EXPECT_EQ(2, len);
    EXPECT_EQ("one", std::string(parameters->name));
    EXPECT_EQ("", std::string(parameters->value));
    EXPECT_EQ("two", std::string((parameters+psize)->name));
    EXPECT_EQ("", std::string((parameters+psize)->value));
}

TEST_F(RouteTest, ParseQS_TwoLastIsPair) {
    char q[] = "one&two=pair";
    parameters = parse_querystring(q, &len);

    EXPECT_EQ(2, len);
    EXPECT_EQ("one", std::string(parameters->name));
    EXPECT_EQ("", std::string(parameters->value));
    EXPECT_EQ("two", std::string((parameters+psize)->name));
    EXPECT_EQ("pair", std::string((parameters+psize)->value));
}

TEST_F(RouteTest, ParseQS_OnlyAmp) {
    char q[] = "&&";
    parameters = parse_querystring(q, &len);

    EXPECT_EQ(3, len);
    EXPECT_EQ("", std::string(parameters->name));
    EXPECT_EQ("", std::string(parameters->value));
    EXPECT_EQ("", std::string((parameters+psize)->name));
    EXPECT_EQ("", std::string((parameters+psize)->value));
    EXPECT_EQ("", std::string((parameters+2*psize)->name));
    EXPECT_EQ("", std::string((parameters+2*psize)->value));
}
