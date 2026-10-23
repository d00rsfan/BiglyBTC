# Original Azureus anti-spoof authority primitive

C++23/OpenSSL3.5.5 owner-thread service pinned to original Java
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f DHTControlImpl generation,
verification and LRU bodies. Type1 XORs raw IP bytes into an ephemeral16-byte
key, MD5s that block and uses signed big-endian first4 bytes. Type2 XORs up to16
contact-ID bytes and returns first8 digest bytes. Separate256-entry access-order
caches key by raw IP (port/scope ignored) or full contactID. Callers preserve
InetAddress factory normalization; forced16-byte mapped addresses differ from
normalized IPv4. Tokens and cache keys are owned.

`create()` generates a private CSPRNG key per instance. No persistence, singleton,
rotation policy or shared profile exists. Supplied keys are for explicit generated
contexts; callers must erase their own copies. Instance and digest scratch keys
are cleansed. Storage-disabled behavior is original zero tokens and no cache;
this is not a storage admission gate by itself. Caller must gate storage support.
Native provider/random failures return explicit Crypto errors without inserting
zero tokens. This deliberately fails closed instead of original catch-and-zero.
Existing cached results remain usable as source. Owner-thread mismatch is explicit.
Contact IDs are capped8192 bytes; actual UDP IDs are20. No null Java objects or
shared returned-array mutation are emulated. Full integration baseline stays null.

Two CTest rows cover original source fixtures and native ownership/LRU/caps/
actual provider failure/thread/lifecycle checks. Fresh Java checks independently
verify native-issued tokens. Java helpers use exact source bodies, actual MD5/
frozen HashWrapper, inert supplied contacts/monitor/logger and generated public
keys under unchanged ReferenceFence. No Control constructor, normal core,
network, real identity/profile, STORE DB, route, plugin or application activation.
