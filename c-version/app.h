/* This file declares the server application functions (SAFs). */

#ifndef __APP_H__
#define __APP_H__

#include "server.h"

void HelloPage(HTTPReqMessage *, HTTPRespMessage *);
void Fib(HTTPReqMessage *, HTTPRespMessage *);

#endif
