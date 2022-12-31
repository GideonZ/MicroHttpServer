all: route prot multi

route:
	g++ -std=c++14 -g route.cpp ../lib/url.c -lgtest -lgtest_main -lpthread -o routeTest && ./routeTest

prot:
	g++ -std=c++14 -g protocol.cpp ../lib/http_protocol.c -lgtest -lgtest_main -lpthread -o protocolTest && ./protocolTest

multi:
	g++ -std=c++14 -g multipart_test.cpp ../lib/dump_hex.c ../lib/multipart.c ../lib/http_protocol.c -lgtest -lgtest_main -lpthread -o multipartTest && ./multipartTest
