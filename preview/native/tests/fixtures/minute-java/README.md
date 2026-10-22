# Actual original minute writer replay

Eight literal plans were executed by Java 17 against unmodified original LongTermStatsImpl and LongTermStatsBase. Their stdout contains each action's observed dated file bytes in Base64, or `-` for an absent file. replay.inc only decodes those outputs; it contains no expected writer algorithm.

The native replay calls the real held-profile minute writer on new owned /tmp profiles and compares every observed path after each action. It has not been executed when this fixture is generated. Epoch-zero writer bytes are included; positive-anchor parser admission is a separate question.

Known literal peer/DHT cells, fixed clock and Overall snapshot, inert config/timer/average housekeeping, and fenced real append streams replace providers. OverallStatsImpl, real schedulers, cache behavior, unknown-source gaps, overflow, IO failures, crash durability and full-client parity are outside this equality corpus. No original profile/client is run.
