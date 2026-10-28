# P06-04 Original distributed-rating data compatibility

Status: bounded implementation qualified; full plugin integration pending. Port original azrating RatingData, RatingResults and
KeyGenUtils at `1629340340d0bc391d9e582f9ee989a93d550581`.

Characterize unchanged committed classes with frozen original BEncoder/BDecoder
through a minimal supplied StaticUtilities formatter and guarded generated-data
helper. No normal plugin, DDB, download, UI or network initialization. Preserve
upper-only score cap, int32 truncation, null/partial decode observations, UTF-8
replacement, omitted nick without comment, float accumulation/comment formatting,
retention behavior, and first20 hash bytes followed by .rating. Preserve unknown
input bytes read-only. Host owner-thread model; bound untrusted input/aggregation.

Distributed database reads/writes, eligibility, scheduling, chat/reseed behavior,
original torrent attributes and Qt presentation remain separate required work.
This data implementation must not enable network or publication.

GCC16/Clang24/ASan+UBSan+LSan pass1819 original observations, bounded-parser/
aggregation tests and original settings/cache/document regressions.366 distinct
native-produced payloads per compiler reopen identically in the frozen Java
classes. Initial logger-wrapped observations are explicitly rejected and retained
in evidence; final harness captures its initial PrintStream before Debug can
redirect stdout. No original data class was modified.
