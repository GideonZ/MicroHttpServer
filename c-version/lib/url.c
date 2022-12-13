/* This file defines the server application functions (SAFs). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "url.h"

// Helper macro for url parsing
#define SPLIT(right, delm)       \
    {                            \
        strsep(&token, delm);    \
        if (token == NULL)       \
            right = empty;       \
        else                     \
            right = token;       \
        token = url_start;       \
    }

const char *supported_apiversions[] = {"v1",
                                       //"v2",
                                       NULL};

void url_decode(char *src, char *dest, int max)
{
    char *p = src;
    char code[3] = {0};
    while (*p && --max) {
        if (*p == '%') {
            memcpy(code, p + 1, 2);
            *dest++ = (char)strtoul(code, NULL, 16);
            p += 2;
        } else if (*p == '+') {
            *dest++ = ' ';
        } else {
            *dest++ = *p;
        }
        p++;
    }
    *dest = '\0';
}

struct Parameter *parse_querystring(char *querystring_copy, size_t *parameters_len)
{
    struct Parameter *parameters;

    char *token, *name, *value;
    static const char *empty = "";

    *parameters_len = 0;
    if (strlen(querystring_copy) == 0) {
        return NULL;
    }

    *parameters_len = 1;
    for (int i = 0; i < strlen(querystring_copy); i++) {
        if (querystring_copy[i] == '&')
            (*parameters_len)++;
    }

    parameters = (struct Parameter *)malloc(*parameters_len * sizeof(struct Parameter));

    int index = 0;
    while ((value = token = strsep(&querystring_copy, "&")) != NULL) {
        name = strsep(&value, "=");
        parameters[index].name = name; // no need to URL decode this
        if (value == NULL) {
            parameters[index].value = empty;
        } else {
            url_decode(value, value, 0);
            parameters[index].value = value;
        }
        index++;
    }

    return parameters;
}

void delete_url_components(UrlComponents *components)
{
    free(components->parameters);
    components->parameters = NULL;
    free(components->url_copy);
    components->url_copy = NULL;
    free(components->querystring_copy);
    components->querystring_copy = NULL;
    free(components);
    components = NULL; // only sets components to NULL on the stack, and then leaves, abandoning the stack
}

int parse_header_to_url_components(HTTPReqHeader *hdr, UrlComponents *c)
{
    int ret = parse_url_static(hdr->URI, c);
    c->method = hdr->Method;
    return ret;
}

int parse_url_static(const char *url, UrlComponents *components)
{
    int supported_version = 0;

    char *url_copy, *url_start, *token;

    if (url[0] == '/')
        url++;

    url_copy = token = url_start = strdup(url);
    static const char *empty = "";

    // Consume apiversion first (a little dirty, but we need to handle it some way)
    components->apiversion = strsep(&token, "/");
    url_start = token = url_start + strlen(components->apiversion) + 1;

    for (int i = 0; supported_apiversions[i] != NULL; i++) {
        if (!strcmp(components->apiversion, supported_apiversions[i])) {
            supported_version = 1;
        }
    }

    if (!supported_version) {
        free(url_copy);
        return -1; // Not supported
    }

    // Separate querystring
    SPLIT(components->querystring, "?");

    // Separate command
    SPLIT(components->command, ":");

    // Separate path
    SPLIT(components->path, "/");

    // Route is all that is left
    components->route = url_start;
    components->url_copy = url_copy;

    // Fix path to allow spaces with %20
    url_decode(components->url_copy, components->url_copy, 0);

    // If the command is empty, we set it explicitly to "none".
    // This is necessary for how the route table works.
    if (strlen(components->command) == 0) {
        components->command = "none";
    }

    // Defaults
    components->method = HTTP_GET; // default
    components->parameters_len = 0;
    components->parameters = NULL;
    components->querystring_copy = NULL;
    return 0;
}

UrlComponents *parse_url(const char *url)
{
    // C-style 'new'.
    UrlComponents *components = (UrlComponents *)malloc(sizeof(UrlComponents));
    if (parse_url_static(url, components)) {
        free(components);
        return NULL;
    }

    components->querystring_copy = strdup(components->querystring);
    components->parameters = parse_querystring(components->querystring_copy, &(components->parameters_len));

    return components;
}

UrlComponents *parse_url_header(HTTPReqHeader *hdr)
{
    UrlComponents *c = parse_url(hdr->URI);
    if (c) {
        c->method = hdr->Method;
    }
    return c;
}
