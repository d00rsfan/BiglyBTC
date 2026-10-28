# Original anti-spoof authority review

Baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Exact original
DHTControlImpl.generateSpoofID/generateSpoofID2/verifyContact and both eviction
bodies execute with actual Java MD5/frozen HashWrapper and supplied inert public
test-key/contact/monitor/logger/trace context. Constructor storage-support/key
blocks and UDP type1 source files are inspected and hash-pinned separately;
ordinary Control constructors, managers, config/profiles and networks never run.
The unchanged ReferenceFence guards every Java execution.

Type1 XORs raw IP into a16-byte epoch key, MD5s the entire key and takes signed
big-endian first4 bytes. Port and IPv6 scope do not key the cache; caller address
factory normalization preserves forced16-byte mapped versus IPv4 distinction.
Type2 XORs min(ID length,16), takes first8 bytes, but keys its cache by the entire
ID. Independent256-entry access-order caches return owned native token copies.
Disabled storage returns source zero tokens without hashing/cache/accessing IP.
Verification issues first, then compares type1 signed int or exact8-byte type2;
null/empty/truncated/extended tokens remain distinct. Any source type other than1
uses type2. Native verifies fixed-size bytes with CRYPTO_memcmp.

Native factory uses a private CSPRNG key per instance; no persistence/rotation/
process singleton is invented. Owned epoch and digest scratch keys are cleansed.
All APIs require their original owner thread. Supplied fixture keys remain the
caller's responsibility. Native ID allocation cap8192 bounds each full-ID key;
actual UDP IDs are20. Java null objects/shared-returned-array mutation are not
emulated by this owned API. Native input/provider/random errors stay explicit.

Intentional provider-error policy: original caught digest failure returns zeros,
so verifyContact can accept a supplied zero token after that failure. Original
observations are preserved in fixtures/provider-failure-policy.json. Native new
provider failures return Crypto, insert no zero token, and verification fails
closed; existing cache hits remain usable as source. This safety difference and
memory/thread constraints leave whole-source integration baselines null. No
storage admission is activated merely because disabled mode emits zeros.

Fresh GCC16/Clang24/ASan+UBSan+LSan each pass both CTest rows and19942 frozen
original observations, including every byte value in all4/16 address positions,
three key extremes, IDs0..64, ports/scopes/mapped factories, independent/full-ID
LRU eviction/access refresh, disabled and correct/wrong/null/length token paths.
Each additionally compares1024 fresh tokens across512 generated contexts and
reopens native-issued tokens through3072 actual original Java verification calls
(1024 accepted,2048 rejected). Native bounds tests cover owned return mutation,
256/257 eviction, exact8192 cap, actual unavailable OpenSSL provider/recovery,
no failure cache insertion, owner-thread rejection and distinct ephemeral keys.

The first development build/tests and all fresh optimized/sanitizer builds pass;
no native/source fixture mismatch occurred. Leak detection remains enabled with
no suppressions. Commands, input/helper/core/libcrypto/binary hashes and tracked
full test logs are recorded. All178 lower owned-UDP qualified inputs remain
byte-identical; that historic full matrix is not needlessly rerun for this new
standalone target. No independent reviewer was used. Original Java HEAD/index,
running client and live profile remain untouched. No STORE/DB/routes/ordinary
plugin/application activation, complete release or whole-client performance
claim. Continue original STORE admission and dependent host/plugin integration.
