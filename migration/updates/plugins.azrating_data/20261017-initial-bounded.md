# Original azrating data review

Self review, 2026-10-17. Upstream azrating
`1629340340d0bc391d9e582f9ee989a93d550581`; frozen original BiglyBT bencode
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Native static host library covers RatingData payloads, RatingResults and
KeyGenUtils first20-byte hash plus .rating lookup key. Upper-only score clamp,
Long int32 truncation, UTF-16 including unpaired surrogates, Java UTF-8 replacement,
empty-comment nick omission, partial initialization and explicit encoding errors,
float32 accumulation, rounded/truncated display scores, exact comment text and
optional rating retention match the bounded original observations. Original
BEncoder int32 MIN encoding throws; native exposes that failure, returning no
publishable bytes. Partial null-comment aggregate errors preserve original count,
average and retention mutation while reporting the error. No automatic publisher
exists. Original input document/unknown bytes remain immutable and move safely.

The oracle compiles three unchanged committed original data classes with a
minimal StaticUtilities formatter using frozen original BEncoder/BDecoder. It
runs under ReferenceFence with supplied/generated inputs, no plugin/core startup
or network. The initial observation transport is invalid because original Debug
redirected stdout and wrapped long lines at8192 bytes; those1819 prefix lines
lost215 continuation lines. That run is rejected and retained. Capturing the
initial PrintStream in the harness fixes observation transport without changing
the original classes. Final raw output has exactly1819 complete lines.

GCC16, Clang24 and Clang24 ASan/UBSan/LSan each pass1819 differential observations,
parser/aggregation limits and existing settings/cache/document regressions.366
distinct native payloads per compiler also pass original Java decode/re-encode
with identical results. Matrix capsules pin sources, commands and actual binaries.
Leak detection remains enabled. No full application or operational plugin claim.

Native limits input to1MiB,10000 nodes,32 levels and strict canonical bencode;
aggregate to100000 positive ratings,1Mi comment UTF-16 units and4Mi retained text
units. Limits reject before mutation, preserving inputs; arbitrary malformed or
oversized Java acceptance is not qualified. DDB services/publication/eligibility,
original torrent attribute edits, scheduling, chat/reseed and Qt views remain
required. Derived files retain original GPL-2.0-only and Aelitis copyright; full
GPL2 text ships beside them. No source publication/release is claimed here.
