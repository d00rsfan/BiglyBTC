# Immutable original Java artifact and reference replay

Native source `d95808b4e2534aa72860efd23f73dee133973e7d`, tree `023c6d6b572fe860c069c0cec3b98be224d4c705`; original baseline remains
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

A new peer-set API check exposed that the pre-existing reference target/classes
lacked TagPeerImpl.matches although it exists in the selected immutable source.
Reference language resources also differed. A working checkout directory was
therefore not sufficient artifact provenance. No prior review is overwritten;
this record supplements the affected current comparisons and explicitly retains
older unreviewed helper classpaths as follow-up work.

The tracked builder reads the immutable Core source and two bundled compile jars
from Git, applies original JDK9+/resource exclusions without running Maven or
mutating the reference checkout, and builds2680 Java files/84 resources using
JDK17.0.20,source/target8. It refuses an unmarked nonempty cache, records input/
dependency/artifact hashes and emits a classpath descriptor. The dedicated owned
cache is persistent test material,not a Java client,daily image or rollback.
Eight ignored editorconfig files left by the initial private diagnostic extractor
were removed from the adopted owned cache; they were never classloader inputs.
No Java oracle ran during the final rebuild or that resource cleanup.

All18 accepted2026-11-16 original groups were recompiled and replayed separately
against the fresh immutable-source classes. Fifteen output artifacts are byte
identical:230 actual address-report cases,local/prefix rebind,category constructors,
swarm metadata,compiled dependencies,recursive control,new-only/ordinary graphs,
registry,state transition policy,tracker setter/handler and Untagged setter/
listener lifecycle. This reaffirms those bounded source behaviors,not every
original service or the entire migration. No unchanged Native matrix was blindly
repeated. Fixture identities and replay summaries are retained below.

Three localization artifacts differ: one category value,four state values and
27 stock values. These are32 changed leaf values across overlapping catalogues,
not32 distinct source keys. Portuguese-Brazil and Estonian translations are
updated from the exact pinned resources. The three fixtures and two generated
immutable C++ tables are refreshed; a tracked state/category generator joins
the existing stock generator. Metadata/locale/source guards and all behavioral
fixtures remain unchanged. Bencode fixtures are marked binary so genuine
embedded whitespace and encoded byte lengths survive Git checks/line-ending
conversion without trimming original message data.

Focused current Core checks compare all66 locales/198 category labels/990 state
labels/3894 stock values,plus their existing state/category control cases. Actual
normal worker stock/category/state-source consumers pass after explicit static
consumer relinking. One application link completes this bounded data/tooling
correction. GCC16 Release/C++23/Qt6.12 offscreen; no fresh sanitizer,real profile,
SWT or full production-network qualification is claimed. Agent self-review only.

The fresh Core also supplies168 actual PeerSet constructor/token/parameter and
TagPeerImpl.matches cases. Limiter/plugin factories and the allocated enclosing
matcher are supplied inert boundaries; actual parser/matcher bodies run. An
IPv4-only203.0.113.0/24 set rejects203.0.114.1 but accepts2001:db8::1 and unresolved
generated.i2p/generated.onion endpoints because their zero l_address skips the
range check. Zero-address/invalid-mask and absent-download/inverse cases are
retained as source evidence. No address is contacted. This is an API-level
finding,not an SWT/full tag lifecycle reproduction; no report was sent.
The bounded Native classifier,range policy and real owned peer cache remain
next work. The separate peer-set source coverage has no Native first-port or
integrated baseline yet.

[Workflow](../../ORIGINAL_JAVA_ORACLE.md) and
[remaining attestation task](../../tasks/P00-ORACLE-ATTESTATION.md) require the
pinned artifact for future comparisons. Older consumed torrent/network helper
classpaths still need their own recompilation/reference checks before being
silently switched or used to advance full parity. Current C++ helper environment
entries are not relabelled as attested merely because the new cache exists.

Whole runtime registry/P08/F13 and global integrated baselines remain in progress.
Continue the plan autonomously. No running client,real profile,payload,protected
SDK,daily/launcher or rollback image changed.
