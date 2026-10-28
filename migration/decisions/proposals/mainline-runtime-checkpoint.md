# Proposed clarification: disposable Mainline engine checkpoint

Status: proposed for bounded root review; runtime qualification pending. Related: AD-001, AD-004 and AD-008.

AD-004 continues to forbid replacing original configuration, IDs, caches, resume or payload data with a native-only schema. A separate `BiglyBTC-mainline.config` may hold **optional disposable native LT DHT runtime state**, as an engine optimization only. Its absence, refusal or removal must not require data conversion, erase originals, widen routes, or affect unrelated payload ownership/accounting. It is not an original-compatible node-ID/cache implementation.

Use the held profile lease and existing ConfigFile writer/recovery/CAS. The versioned bounded wrapper binds the exact plugin directory, preserves unknown fields, and cannot inject settings. Loads create nothing. Same-state saves remain CAS-checked no-ops; malformed/mismatched files are preserved; any failed/uncertain publication disables further writes. Only current Normal/direct Mainline service can load/save it. Bad optional state may be ignored only by starting fresh with the identical route policy. Original mlDHT files stay read-only hints and no baseID.config value is installed as an LT identity.

Implementation `d3f148820d723368dd974d3cef06c9915bbe8a62` is a source candidate, not execution evidence. Keep broad AD-004/P05 compatibility and platform gates open. Root may append this clarification to AD-004 after review; this proposal neither changes the existing accepted decision nor authorizes shipping/Java-LT identity equivalence.
