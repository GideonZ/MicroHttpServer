CXXFLAGS ?= -std=c++14 -g

all: route prot multi response

route:
	$(CXX) $(CXXFLAGS) route.cpp ../lib/url.c -lgtest -lgtest_main -lpthread -o routeTest && ./routeTest

prot:
	$(CXX) $(CXXFLAGS) protocol.cpp ../lib/http_protocol.c -lgtest -lgtest_main -lpthread -o protocolTest && ./protocolTest

multi:
	$(CXX) $(CXXFLAGS) multipart_test.cpp ../lib/multipart.c ../lib/http_protocol.c -lgtest -lgtest_main -lpthread -o multipartTest && ./multipartTest

response:
	$(CXX) $(CXXFLAGS) response_test.cpp ../lib/http_protocol.c -lgtest -lgtest_main -lpthread -o responseTest && ./responseTest
