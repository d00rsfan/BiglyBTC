# Original Java room oracle (opt-in)

The tests compile five unmodified azmsgsync classes from the source hashes in
`source-pins.json`, against an already built original BiglyBT core. They do not
compile, launch or attach to the Java desktop client. JDK 17 is required. The
Native source tree contains only the small host shim, test driver and guard;
original plugin source remains in its separate checkout.

From this directory, prepare a generated directory under the original checkout's
`build` tree. Substitute absolute paths for the four inputs:

```sh
python3 prepare-reference.py \
  --root /absolute/original/build/generated-chat-oracle \
  --core-classes /absolute/original/core/target/classes \
  --plugin-source /absolute/azmsgsync \
  --java-home /absolute/jdk-17
```

Configure the existing Native build with `BIGLYBTC_ORIGINAL_CHAT_ROOT` set to that
root, `BIGLYBTC_ORIGINAL_CORE_CLASSES` to the core classes, and
`BIGLYBTC_ORIGINAL_CHAT_JAVA` to the JDK 17 `bin/java` executable. These options
are off by default. Run `chat-full-original-java-handler` and
`chat-original-display-order` through CTest.

The pipe oracle uses the actual handler for Java-authored message creation and
signatures, synchronization requests and validation of Native-authored replies.
Its diversification-key check invokes the original DHTPluginStorageManager.
`OriginalRoomOrder` contains the exact source-pinned BuddyPluginBeta sorting
methods with inert message/UI/logging records; it is an algorithm comparison,
not a running Buddy UI. Both directions use generated identities and messages.

`FullChatFence` requires the generated marker and confines writes to the oracle
root. It refuses personal data reads outside the generated root/core classes,
sockets, subprocesses, guard replacement and third-party JNI. The harness uses
`COConfigurationManager.initialiseFromMap` to initialise test configuration
without starting core services. Test children clear injected Java/loader options,
use generated home/tmp/config paths and bounded heaps/timeouts. The original
application's automatic security-manager installation is disabled while this
explicit test guard remains installed. The networking package is opened only
for the original contact serializer on JDK 17.

These checks establish handler API/wire and ordering compatibility. They do not
claim a full Java/Native desktop session, a live Public-room conversation or
whole-plugin parity. No real profile or network room is used.
