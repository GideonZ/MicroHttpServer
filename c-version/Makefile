PROJ=microhttpserver

CC=cc
CPP=c++
INCLUDES=-Ilib
# DEFS=-D_PARSE_SIGNAL_ -D_PARSE_SIGNAL_INT_ -DDEBUG_MSG -DENABLE_STATIC_FILE=1 -DMHS_PORT=8001
DEFS=-D_PARSE_SIGNAL_ -D_PARSE_SIGNAL_INT_ -DENABLE_STATIC_FILE=1 -DMHS_PORT=8001
CFLAGS=-g -O0 -Wall
SRCS=main.c lib/url.c lib/server.c lib/middleware.c lib/multipart.c lib/dummy_api.c lib/http_protocol.c

all:
	$(CC) $(SRCS) $(INCLUDES) $(DEFS) $(CFLAGS) -o $(PROJ)

clean:
	rm -rf *.out *.bin *.exe *.o *.a *.so *.list *.img test build $(PROJ)

assem: $(SRCS) lib/assembly.c
	$(CPP) -c -I../../io/stream -I../../system -I../../components ../../api/json.cc -o json.o
	$(CPP) -c -I../../components ../../components/mystring.cc -o string.o
	$(CPP) -c -I../../system ../../system/small_printf.cc -o print.o
	$(CPP) -DENABLE_STATIC_FILE -DMHS_PORT=1 lib/multipart.c lib/http_protocol.c lib/assembly.c print.o json.o string.o -o assem
