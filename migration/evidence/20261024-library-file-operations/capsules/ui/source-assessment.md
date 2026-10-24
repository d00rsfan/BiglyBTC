# Original meanings and bounded implementation

Pinned original cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; exact blobs in
source-pins.json. No original process/profile or payload was opened.

* TorrentUtil.moveDataFiles (3645–3885) chooses a destination **parent directory**
  and calls moveDataFilesLive. Selecting a path is separate from the actual core
  move. Native UI sends only a read-only preview request on first show/Review;
  worker execute owns retirement, mapping, no-replace and recheck decisions.
* TorrentUtil.removeDownloadsPrompterClosedSupport (5378–5406) distinguishes keep
  data from delete data. It additionally honors DO_NOT_DELETE_DATA_ON_REMOVE;
  Download.java153 defines 0x40. ManagerUtils.asyncStopDelete (2218 onwards)
  rechecks this protection before GlobalManager removal. This was sent to the
  storage author; a UI confirmation cannot override original protection.
* Java has a separate delete-torrent-file preference and archive/share-veto
  machinery. This slice exposes no claim to that manager/plugin behavior. UI
  confirms the exact worker-listed data scope; metadata/archive behavior remains
  explicitly worker-owned. Move is not an alias for relink: relink locates
  existing data and requires worker verification. Same-filesystem no-replace
  and unsupported cross-device handling are storage contracts, not UI guesses.
* NewTorrentWizard322–368 distinguishes computed/manual piece sizes and private
  flag. SavePathPanel69 computes an automatic size, while294 onwards provides
  explicit auto-open. Native creation starts with v1/Automatic/no import and
  offers exactly the qualified creator's20 manual choices. It has no v2/hybrid
  selector. Tracker URLs are one per line; blank lines separate tiers, preserving
  commas inside URLs. The worker does all source scanning/hashing and exclusive
  publication. Published-but-unsynced/unclean output is displayed distinctly and
  cannot be automatically retried at the same path.

The existing5432-class retained Java cohort lacks all three SWT subjects
TorrentUtil/ManagerUtils/NewTorrentWizard. A thin actual remove/move invocation
would therefore require new original UIS compilation plus SWT/dispatcher/manager
substitutions. That is outside this bounded chapter's existing oracle scope;
source characterization earns no Java runtime/equality credit. Creation parser
reload/oracle remains assigned to the creator author.

UI state is session/request/revision/Target bound; each approved packet is copied
into privately owned const data before confirmation. Approval scope digest is
separate from execution CAS: root re-prepares after actual stop/checkpoint and
may admit ordinary counter/time changes while rejecting changed data scope.
Only nonblocking QFileDialog opens a picker. Normal dialogs do no path stat,
source scan, filesystem mutation or engine/network startup.
