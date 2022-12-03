#ifndef __URL_H__
#define __URL_H__

#define MAX_STR_LEN 512

struct Parameter {
    const char *name;
    const char *value;
};

struct UrlComponents {
    const char *method;
    const char *apiversion;
    const char *route;
    const char *path;
    const char *command;
    const char *querystring;
    struct Parameter *parameters;
    size_t parameters_len;
    // Writable copies of url and querystring, to be modified by
    // strsep() and parts referenced by pointers
    char *url_copy;
    char *querystring_copy;
};

/*
Extract name-value pairs from the querystring. Allocates memory for an array of struct Parameter,
which also includes memory allocation for name and value strings.
*/
struct Parameter *parse_querystring(char *querystring, size_t *parameters_len);

/*
Extract the different parts of the url. Allocates memory for a struct UrlComponents. Makes a call
to parse_querystring() which also allocates memory.
*/
struct UrlComponents *new_url_components(char *url_copy, const char *method, const char *apiversion, const char *route, const char *path, const char *command, const char *querystring);

/*
Free all memory allocated by new_url_components(). Makes a call to delete_parameters().
*/
void delete_url_components(struct UrlComponents *components);

/*
Parse the given url and extract all parts. Makes a call to new_url_components().
Returns a pointer to an allocated struct UrlComponents, which should be freed using
delete_url_components().
*/
struct UrlComponents *parse_url(const char *url);

#endif
