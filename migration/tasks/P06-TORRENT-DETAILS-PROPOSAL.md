# Next proposed chapter: complete native torrent properties

Status: proposed only; do not start while finishing the authorised resume/status
bar build. User requests all torrent-property tabs opened by double-click and
allows implementing missing dependencies. IP-filter is low priority; embedded
browser and media player have unconditional lowest priority and are skipped.
Original baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, MyTorrentsView.registerPluginViews.

Original core registrations (not merely the current five Native tabs):
GeneralView, FilesView, TaggingView, TorrentOptionsView, TrackerView, PeersView,
PeersGraphicView, PiecesView, PieceMapView, PieceBlocksView (hidden by default),
DownloadActivityView, TorrentInfoView, PrivacyView, LoggerView,
DownloadXferStatsView. Inventory existing plugin contributions separately; do
not add fake enabled placeholders for unsupported plugin services.

Dependency order:
1. A bounded worker-owned per-torrent inspection stream, scoped by profile,
   engine epoch, torrent identity/admission and observation sequence. Reuse the
   sole alert consumer and existing global peer/tracker/accounting ownership.
   Observe only visible selected detail tabs; no blocking GUI queries or
   redundant full-library refreshes. Preserve open tabs across sorting, row
   movement, stop/restart and profile close; reject stale results/commands.
2. Complete General/Files and activate Trackers/Peers. Reuse existing file
   policies/links and All Peers snapshots, filtered by actual torrent identity.
   Actual announce/scrape/error/endpoint status and tooltip detail, original
   saved column layouts, numeric sorting and stable viewport/selection.
3. Pieces/Piece Map/Piece Blocks with actual verified availability/block state.
   Historical resume claims and unchecked pending maps must not appear as live
   verification. Add missing bounded engine observations where required.
4. Tags/Options/Torrent Info via existing source-owned configuration commands,
   metadata parsers and compatible CAS writes. Preserve Unicode paths, opaque
   keys, v1/v2 identities and original policies; no second UI policy engine.
5. Activity/transfer statistics, swarm peer diagram, privacy/network information
   and per-torrent console. Add missing services before claiming live values.
   The diagram uses current peers; swarm merging keeps its separate low priority.
   Logs and histories must be bounded; no unbounded per-torrent temp directories.

Provide original visibility/order/settings where supported and native shortcut/
context behaviour. Public/private and overlay constraints, private announce-key
protection, geometry/storage/checking rules and joined retirement remain in
force. Use synthetic fixtures and owned transfers; no real profile writes by
tools, no private names/keys/addresses in public evidence. Batch coherent builds,
read only failures/summaries, and stop after a complete chapter for user review.
