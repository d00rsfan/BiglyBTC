# Original xmwebui RPC/session contract review

Self review, 2026-10-17. xmwebui baseline
`c9ab8f7cd62b2874f7385772992c0288a224e85d`. Scope: exact three-argument outer
processRequest, isSessionValid/getCookie, StaticUtils split/cause formatting and
TransmissionVars method normalization. The real inner method dispatcher, HTTP
listener and WebPlugin authentication are outside this bounded component.

Qt 6.12 Core wrapper requires an explicit real host dispatcher. Transmission
arguments/result/tag versus JSON-RPC 2 params/result/id, null/defaults, nonstring
preconditions, permission propagation, textual/general errors and cause text are
preserved. A null host result becomes an empty object; unknown method behavior
belongs to the dispatcher (the original implementation normally returns empty
success). The fixture echo/error dispatcher is test code, not a live backend.
Signed 64-bit identifiers and well-formed Unicode/nested JSON values compare
semantically against original JSON output; property order/escape style differs
legitimately. Booleans are compared distinctly from numbers.

Session checks preserve the original /transmission/ path prefix, header-before-
cookie priority, uppercase/lowercase key preference, exact cookie name and Java
ASCII trim/split behavior. A session/CSRF token is not HTTP authentication. Raw
incoming tunnel claims are ignored; only an authenticated internal host adapter
may supply trusted metadata. The original method receives an equivalently
controlled header map from the reference host. No trusted tunnel or method
permission can be inferred from an untrusted external header here.

The fenced reference compiles exact method bodies, unchanged TextualException,
frozen original core JSON and supplied session/dispatcher/host trust. All 804
original observations plus native limit/no-dispatch checks pass GCC 16, Clang 24
and Clang 24 ASan/UBSan/LSan. Matrix/fixture capsules pin exact source/method/helper
classes, commands, Qt runtime and actual native binaries. Leak detection remains
enabled. No HTTP server, real profile or network was used.

Native bounds 10,000 input value nodes, 32 levels, 1 Mi UTF-16 string/key units,
128 headers, 64 Ki header values and 16 Ki paths. Arbitrary oversized/malformed
JSON syntax accepted by original JSON utilities is not qualified. Full HTTP
framing/authentication, GET/gzip/assets, torrent/session/tag/config methods,
read-only permissions, pairing/search/tunnels and live host integration remain
required. Qt HTTP Server is absent from the installed SDK; later listener work
must use a mature qualified parser, not a new handwritten HTTP parser. No binary
or source publication and no complete remote-control parity are claimed.
