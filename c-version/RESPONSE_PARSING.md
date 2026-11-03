# Incremental HTTP response framing

Set `HTTPReqMessage.usedAsResponseFromServer` for response parsing and set
`responseToHead` for a HEAD request, because its response can advertise a length
without a body. Existing server/request parsing keeps its default behavior.
Call `FinishClientResponse()` on orderly transport EOF; it rejects incomplete
length/chunk framing and completes a valid connection-close body only once.

The response parser checks Content-Length and Transfer-Encoding across all
header fields, including fields beyond the stored-header limit. Conflicting
lengths and unsupported codings are rejected. Chunked responses include bounded
chunk lines and trailers; premature completion is an error. A caller handling
connection-close framing must distinguish a valid end of stream from transport
failure, particularly when the stream is authenticated.

The response tests exercise split input, malformed framing, excess header
fields and body-free responses. Run all parser tests from `c-version/tests`:

```sh
make all
make CXXFLAGS='-std=c++17 -g -fsanitize=address,undefined -fno-sanitize-recover=all' all
```

The multipart changes initialize unused header pointers and check them before
dereferencing them. No TLS library or firmware
dependency is required to run these tests.
