# Original identity-free chat peek foundation

Native43f45ea08e0bafdad826def80da1296dfe388b15, tree
c99765c720e30801263bb4d225721362e6843410. Original azmsgsync
4209aa5029e3035cbb09dc3a83e9491d3f36fbc6; real original core bencode from
BiglyBTcb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This is a bounded foundation
of P06-DOWNLOAD-CHAT-ADAPTERS, not completion of the download UI/runtime chapter.

Peek derives the original separate transfer key and General AES room key without
creating a UID, signer, Room, history, profile entry or participant. Its payload
matches the original responder/listener: nonce presence rather than a forced
length, exact expected key, required m/l/e with Java integer narrowing, optional
t ignored by aggregation, original maxima and paired signature/public-key
selection. The original response has no echoed nonce. The optional pair alone
is not a verified participant identity because there is no signed content.
Native bounds parsing and rejects duplicate fields, caps the aggregate at three
accepted replies and ignores later replies. These lifetime/size limits are
explicit native policy, not claims of matching every original callback race.

384 responder payload cases and384 aggregate observations compare exact selected
original source bodies using real original BEncoder/BDecoder; identity ciphers,
supplied counts and a memory timer are explicit collaborators. Full encryption
is separately exercised by the existing fenced unmodified MsgSyncHandler:
Native request enters its actual registered peek handler, Native decrypts the
original reply, and another room's ciphertext is refused. The fixture extension
only changes the in-memory test driver, not original handler source. Real TCP/
UDP and the user's profile are unavailable to the fenced original runtime.

Two configured CTests pass with zero skips in1.83seconds:
original-chat-peek-payloads and chat-full-original-java-handler. No preview,
live profile, runtime SDK or running user process was changed. The implementation
still needs actual discovery/owner integration, cancellation qualification,
download controls and genuine incoming notifications; these are active work.
