Adopt the five new files from `staging/` using `manifest.json`.

- Add `adapters/application_settings.cpp` to `biglybtc_profile`. The std-only core catalog is header-only.
- `application_settings_tests.cpp`: plain executable linked to `biglybtc_profile`, private `LINKER:--wrap=fsync`; 11 source-defined test families. No Qt dependency.
- `settings_roundtrip_driver.cpp`: plain executable linked to `biglybtc_profile`; invoke `PROFILE apply` after the original Java writer, then `PROFILE check` after the original Java resave. It requires `native.options.opaque = [7, "opaque–π"]`, compares its exact encoded subtree, and prints no credentials. Destination is canonical profile parent + `/payload`.

Host owns session/revision fencing and validates `prepared.document()` with the complete NetworkPolicy before save. The UI submission is sparse. Do not build a full document from displayed defaults: missing original LAN default=true differs from native policy's missing fallback=false, and the original platform-derived save directory has no portable default.

Malformed individual fields remain visible; invalid Boolean integers retain int64 values. Invalid types have no typed value. OriginalOnly fields are editable to repair source config but have no native effect. Incomplete suffix naming is OriginalOnly. The fixed filename is `biglybt.config`; `azureus.config` is neither read nor modified.

Read may expose Backup/Saving recovery for diagnosis. Save requires unchanged pristine Primary or all three candidates absent. `PreparedApplicationSettings` privately binds the original baseline; its edited Document deliberately retains that baseline backing for ConfigFile CAS. Do not reparse desired bytes and pass that reparsed document into ConfigFile.save. Unknown raw subtrees remain intact. A post-publication sync error remains the original storage::Error; do not infer rollback or retry by recomputing changes.

No compiler, C++ test, Java oracle, profile or network execution was performed by this author. Source pins are in `source-provenance.json`. Initial header and two narrow semantic amendments remain recorded under `header-amendment/`.
