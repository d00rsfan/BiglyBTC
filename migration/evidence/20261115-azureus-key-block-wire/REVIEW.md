# Azureus signed key-block wire prerequisite

Native source `fd1f4b16f16fd7bf6a7978a9e3ea963ee7084ae6`, repository tree `d5a40f58f3fc085e253adaa1605f5dfb63fb38de`; original source
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Actual original JDK17 serializers provide16 generated request1036/reply1037
packets at protocols51/55, with two actual reply parse/reencode checks, two
original declared-size refusals and one official-key unsigned rejection.
Native roundtrips12 in-bound packets byte-for-byte, rejects4 oversized original
packets under the existing finite8192-byte envelope, refuses every truncation,
retains trailing bytes, and checks malformed declared sizes, wrong networks,
actions and signed request nonce. The empty ACK carries no new capability.
Codec acceptance is not signature validity or permission to send/serve data.

Focused GCC16 Release/C++23/Qt6.12.0 module build and tests pass. Initial
confined original JVM stalled before output and was terminated; only the
bounded isolated-profile retry is qualified. No full application rebuild is
needed for this unconnected codec prerequisite. No real profile, public-network
run or daily image was used. Signature validation, original saved block policy,
actual source-gated network/DB serving and forwarding remain open; F09 remains
in progress. Detailed group counts are in qualification.json.
