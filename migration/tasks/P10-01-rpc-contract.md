# P10-01 Original xmwebui RPC/session contract

Status: bounded contract implementation qualified across GCC 16, Clang 24 and
ASan/UBSan/LSan. Listener and real host method integration remain pending. Original xmwebui baseline
`c9ab8f7cd62b2874f7385772992c0288a224e85d`.

Characterize exact outer processRequest, isSessionValid/getCookie, split/cause
helpers and JSON-RPC method-name normalization with supplied host dispatcher,
session ID and trusted tunnel metadata. Frozen core JSON utilities; no normal
WebPlugin/plugin/core initialization, HTTP listener, profile or lifecycle calls.
Preserve Transmission arguments/result/tag versus JSON-RPC2 params/result/id,
null/default behavior, permission propagation, textual/general error shapes,
header/cookie precedence and original path exemptions. Incoming tunnel header
trust is a host boundary: only an authenticated tunnel adapter can supply it.

Implement bounded Qt6.12 Core JSON model with a real host callback contract.
HTTP/authenticated listener and original download/session method implementations,
view permissions, tags/config/search/pairing/tunnel/asset support remain required.
Do not invent success data or treat this wrapper as an operational web plugin.
The Qt HTTP Server module is absent; select a mature compatible HTTP parser for
later listener work rather than writing a new untrusted HTTP parser.

All 804 original observations and bounded-input/no-dispatch checks pass per
compiler. JSON semantic comparison keeps booleans distinct from numbers and
checks signed 64-bit identifier values, nested/Unicode data and full original
error/permission shapes. No listener or real profile/network was used.
