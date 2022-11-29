/* This file defines the server application functions (SAFs). */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "url.h"

const char *supported_apiversions[] = {
    "v1", 
	//"v2",
    NULL
};

struct Parameter **parse_querystring(const char *querystring, size_t *parameters_len) {
	struct Parameter **parameters;

	char *token, *name, *value, *querystring_copy, *tofree;

	*parameters_len = 0;
	if (strlen(querystring) == 0) {
		return NULL;
	}

	*parameters_len = 1;
	for (uint16_t i = 0; i < strlen(querystring); i++) {
		if (querystring[i] == '&') (*parameters_len)++;
	}

	parameters = (struct Parameter **)malloc(*parameters_len * sizeof(struct Parameter *));

	tofree = querystring_copy = strndup(querystring, strlen(querystring));
	uint16_t index = 0;
	while ((value = token = strsep(&querystring_copy, "&")) != NULL) {
		name = strsep(&value, "=");
		if (value == NULL) {
			name = token;
			value = token+strlen(token);
		}
		struct Parameter *parameter = (struct Parameter *)malloc(sizeof(struct Parameter));
		parameter->name = strndup(name, strlen(name));
		parameter->value = strndup(value, strlen(value));
		parameters[index++] = parameter;
	}

	free(tofree);

	return parameters;
}

struct UrlComponents *new_url_components(
		const char *method,
		const char *apiversion,
		const char *route,
		const char *path,
		const char *command,
		const char *querystring) {
	struct UrlComponents *c;

	if (!(c = (struct UrlComponents *)malloc(sizeof(*c)))) {
		return NULL;
	};

    const size_t method_len = strlen(method)+1;
	if (!(c->method = (char *)malloc(method_len))) {
		free(c);
		return NULL;
	}

    const size_t apiversion_len = strlen(apiversion)+1;
	if (!(c->apiversion = (char *)malloc(apiversion_len))) {
		free(c->method);
		free(c);
		return NULL;
	}

    const size_t route_len = strlen(route)+1;
	if (!(c->route = (char *)malloc(route_len))) {
		free(c->apiversion);
		free(c->method);
		free(c);
		return NULL;
	}

    const size_t path_len = strlen(path)+1;
	if (!(c->path = (char *)malloc(path_len))) {
		free(c->route);
		free(c->apiversion);
		free(c->method);
		free(c);
		return NULL;
	}

    const size_t command_len = strlen(command)+1;
	if (!(c->command = (char *)malloc(command_len))) {
		free(c->path);
		free(c->route);
		free(c->apiversion);
		free(c->method);
		free(c);
		return NULL;
	}

    const size_t querystring_len = strlen(querystring)+1;
	if (!(c->querystring = (char *)malloc(querystring_len))) {
		free(c->command);
		free(c->path);
		free(c->route);
		free(c->apiversion);
		free(c->method);
		free(c);
		return NULL;
	}

	strncpy(c->method, method, method_len);
	strncpy(c->apiversion, apiversion, apiversion_len);
	strncpy(c->route, route, route_len);
	strncpy(c->path, path, path_len);
	strncpy(c->command, command, command_len);
	strncpy(c->querystring, querystring, querystring_len);

	c->parameters = parse_querystring(querystring, &(c->parameters_len));

	return c;
}

void delete_parameters(struct Parameter **parameters, uint16_t len) {

	for (uint16_t i = 0; i < len; i++) {
		free(parameters[i]->name);
		parameters[i]->name = NULL;
		free(parameters[i]->value);
		parameters[i]->value = NULL;
		free(parameters[i]);
		parameters[i] = NULL;
	}
	free(parameters);
	parameters = NULL;
}

void delete_url_components(struct UrlComponents *components) {
	free(components->command);
	components->command = NULL;
	free(components->apiversion);
	components->apiversion = NULL;
	free(components->route);
	components->route = NULL;
	free(components->method);
	components->method = NULL;
	free(components->querystring);
	components->querystring = NULL;

	delete_parameters(components->parameters, components->parameters_len);

	free(components);
	components = NULL;
}

struct UrlComponents *parse_url(const char *url) {
	struct UrlComponents *components;
	char *apiversion, *route, *path, *command, *querystring;

	uint8_t supported_version = 0;

	char empty[] = "";
	char *url_copy, *url_start;

	if (url[0] == '/') url++;

	url_copy = url_start = strndup(url, strlen(url));

	// Consume apiversion first (a little dirty, but we need to handle it some way)
	apiversion = strsep(&url_copy, "/");
	url_start = url_copy = url_start+strlen(apiversion)+1;

	for (uint8_t i = 0; supported_apiversions[i] != NULL; i++) {
		if (!strcmp(apiversion, supported_apiversions[i])) {
			supported_version = 1;
		}
	}

	if (!supported_version) {
		return NULL;
	}

	// Separate querystring
	SPLIT(querystring, "?");

	// Separate command
	SPLIT(command, ":");

	// Separate path
	SPLIT(path, "/");

	// Route is all that is left
	route = url_start;

	components = new_url_components("GET", apiversion, route, path, command, querystring);
	free(url_start-strlen(apiversion)-1);

	return components;
}
