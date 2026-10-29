# I2CP framing, signed registration and initial channel

State: in_progress. Feature F06.i2cp_initial_session. Source e124abb90ab846c4b057a6b868004c0acb737118 extends the original pure framing/no-auth channel with exact SessionConfig/Create/Reconfigure, DSA0/Ed7 signatures and Java UTF16-sorted UTF8 options. Current CREATED stores a genuine ID; matching UPDATED commits original putAll merge. Identity is borrowed synchronously for signing; no private key persistence. Explicit numeric-loopback only, bounded request/write/read, deadlines and generation/revision guards remain.

Current focused7-suite three-lane/private and actual two-artifact Java object proof: ../evidence/20261029-signed-session-local-publish/verification.json. Failed helper-only attempts and their precise corrections remain in that evidence; no native/whole-suite replay for helper fixes. Older evidence remains historical.

Original i2p-java9134f808337b401e8e53c73734c81fab04280c9d; modified plugin33d97eca67a9f332e3d4da7e7492ec54f4bec078 context remains separate. No whole modified-core source equivalence.

CREATED is registration only, not original OPEN. LeaseSet2 genuine independent encryption key/signature and renewal handling is the next prerequisite. No send-after-CREATED shortcut. Auth/TLS/remote/offline/unsupported keytypes, automatic Clock/profile/router selection, public/backend activation, full payload/lookup/tunnel lifecycle and performance remain open.
