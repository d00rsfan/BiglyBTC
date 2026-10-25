# CF-01 original storage handoffs

Source-only preparation. No execution credit until root runs the qualified native
adapter/owner and the retained original Java classes over fresh generated profiles.
Original source is cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; native baseline is
b44ec22ebb3dbb6cee3ade23c32a7ad26a373926. No real profile, process, payload or network.

Two independent checks are necessary:

* The unchanged `migration/fixtures/java/StorageOracle.java` supplies its seven
  original read-map cases. The new scripted helper invokes actual original FMFile
  operations for writes, allocation, length, reopen and piece completion. Native
  output is re-opened by original Java; numeric maps are never calculated by a
  second oracle algorithm.
* The profile producer creates valid SHA-1 v1 metadata and authentic original
  GlobalManager state, writes through original FMFile and lets the original disk
  manager check it. A native managed owner then performs real owned-loopback
  partial/completion transfers, joins and retires storage before Java reloads the
  same profile to READY and checks actual piece counts, file bytes and policies.

## Shared raw codec tape

CLI `StorageScriptOracle ROOT ROOT/request.tsv`. Native driver uses the same tape
and leaf names. ROOT is a generated case below the ReferenceFence root, contains
only owned regular leaves, and has no symlink components. Tab-separated commands:

```
OPEN kind torrentOffset fileBytes 16384
WRITE fileOffset fixture-relative.bin
READ fileOffset length label
LENGTH logicalLength
COMPLETE globalPieceNumber file-fragment-relative.bin
FLUSH
CLOSE
REOPEN
```

The displayed spaces above are tabs in real tapes. Kinds are original 1 Linear,
2 Compact, 3 Piece Reorder, 4 Piece Reorder Compact. One OPEN; up to 128 operations;
fixture sizes <=4 MiB, one write <=64 KiB. COMPLETE supplies the file fragment of
the actual global piece, matching the original FMFile API, not a made-up layout.
Data leaf: `payload.bin.az!`; control directory `control`; metadata file index is1.
Original emits `STATE step operation length kind` or `STATE step CLOSE closed`,
`READ step label bufferPosition transient`, and `SCRIPT_ORIGINAL_STOPPED count`.
Actual read buffers are `read-<label>.bin`. Native sidecars additionally describe
explicit missing ranges; their absence is never certified by Java zero padding.

Compact holes advance the Java buffer without filling bytes or setting the
transient flag. Reordered unallocated reads zero the remaining buffers and set
`FL_CONTAINS_TRANSIENT_DATA`. These are different mechanisms. Compare actual bytes
only on observed available fragments; missing/EOF stays unverified. In particular
zero bytes cannot become valid pieces without actual hash checking.

## Source storage rules

* Compact retains only a leading cross-piece fragment and trailing fragment.
  Aligned leading fragment has length0. A file wholly inside its first crossing
  fragment needs no state file. Interior writes are consumed/discarded.
  Logical length is state `version=0,length`; flush writes original resilient
  `fmfile<originalIndex>.dat`. Reads may advance logical length in the original.
* Reordered files use original `fmfile<index>.dat.2`, keys `st,len,next,pieces`;
  pieces is original signed big-endian32 map, -1 means unallocated. First piece
  has fixed slot0. Fewer than3 file pieces use linear IO but still mark format.
  New writes allocate map slots; allocation may itself move an earlier allocated
  piece to its canonical slot. Piece completion may swap two stored fragments,
  update both map directions, and truncate a canonical final fragment. FLUSH and
  reopen must preserve bytes and original-compatible controls. This is not a
  transaction/durability proof for the original writer.
* Missing reordered control over an existing data file can cause the original
  recovery path to infer sequential allocation; this is a candidate map for a
  subsequent hash recheck, never evidence of verified data. Malformed controls
  can be rewritten by original recovery before an exception. New strict native
  rejection/recovery policies must be reported separately from accepted-domain
  original equality.
* The original skipped-file policy selects Compact/ReorderCompact together with
  skipped=true. Explicitly unskipping converts them to Linear/Reorder. Profile
  handoffs exercise that selection transition. They never pass by making all
  files DND. Normal selected saved priority is-1 (Java API priority0), skipped is0
  in the persisted priority representation.

## Profile/transfer contract

Three valid v1 files under `layout`: prefix3072 bytes, middle103304 bytes,
suffix24696 bytes; 131072 total bytes, eight16384-byte pieces; deterministic
nonzero content and real SHA-1 hashes. Original raw middle writes are local
pieces0,last,2 out-of-order; prefix/suffix have authentic complete bytes. Kinds2
and4 mark only the middle skipped by the original API; the other files remain
selected. Kind3 is selected throughout. Source producer emits actual checked
piece bits and file facts, never a guessed resume map. All saved intent is stopped.

Native driver must select middle explicitly, preserve unknown metadata/config,
retain every original existing physical leaf, and report actual partial checked
piece indices after a real joined stop, then complete all eight pieces on reopen.
Java readback invokes GlobalManager + manager.initialize until actual READY,
never startDownload/addPeer, then verifies actual counters, policies and paths;
full logical bytes are compared on completion. Do not require unchanged physical
inode across an explicitly documented format conversion if the safe production
adapter needs replacement; require unchanged logical content and no unexpected
aliases instead. Root records exact source/driver identity and any such boundary.

Retained5432 original classes are reused byte-for-byte under the existing
ReferenceFence/peer_interop JVM recipe. Compile authored helper classes only,
never original sources or the project. Preparatory code does not claim v2/hybrid,
power-loss, arbitrary layouts, whole-client or complete CF-01 qualification.
