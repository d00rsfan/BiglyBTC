# Saved tracker credentials and HTTPS trust

Product source `c7a6ab6700666b86cbf2a0a265cb95addb57a660`, over published
`55a72ff84882281928f220d1ab5293bfbaec3a55`, implements read-only use of the
original saved Basic credential cache and JKS trust store for queued scrapes.
No profile conversion or new Java dependency is introduced.

Credentials are selected by the original realm and effective origin, sent only
after a Basic challenge, and withdrawn when their source or internal-tracker
policy changes. Redirects do not forward another origin's credentials. Saved
JKS v1/v2 anchors replace only the request's trust roots; directory/leaf identity
and content changes revoke queued work. Global Qt trust is unchanged.

Fresh GCC and leak-enabled ASan/UBSan each passed HTTP, queue-policy and
network-policy. Clang compiled the same products and passed the latter two
suites; HTTP is compile-only there. Each executed HTTP suite passed 28 Qt rows
covering 25 test methods, without failures or skips. Full CTest output is kept
because its JUnit system-out truncates successful output. Historical suites
from the earlier identity/Tor milestone are not counted as new executions.

The copied current HTTP binary passed two focused HTTPS checks with private
Qt Core/Network, the packaged TLS backend, the approved provider and actual OS
OpenSSL 3 images. The checked 119-leaf distribution replaced dist atomically;
the previous preview was deleted. No extra rollback distribution is retained.

Earlier failed attempts remain recorded: the first found an incorrect Qt byte
conversion before runtime; the second found a 15-versus-16-byte JKS integrity
string. Both were corrected without weakening fixture values or assertions.

Supported scope is saved ASCII Basic credentials, supported HTTP(S) requests
and bounded JKS X.509 stores. Unicode aliases/credentials, other store formats,
other authentication schemes, prompts, proxy behavior and original permissive
hostname/all-trusting retries remain open. No difference is accepted on the
user's behalf; the TLS compatibility decision is in migration/QUESTIONS.md.
The running Java client, original profile and torrent payload remain untouched.
This does not close P05, P07, whole-client or release qualification.
