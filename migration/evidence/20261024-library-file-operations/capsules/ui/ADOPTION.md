# CF-06 UI adoption

Seven NEW source paths under staging/preview/native. No existing file or CMake
was edited. Root adds CMake registrations from CMake.snippet and owns all runs.

FileOperationDialog.beginOperation binds session/revision/Target/operation; request
IDs are nonzero/saturating per dialog type across instances. setPlan must echo the complete request. Convert
worker approvalScopeDigest to64-character SHA256 text, entry.actualBytes to row
bytes and preview.presentBytes to totalBytes. Paths and counts are exact data
scope, never inferred by the widget. Keep the prepared storage object on worker.
Submission carries a copied const UI plan and explicit deleteConfirmed; Host must
still check live identity, protection flags and approved scope after retirement.
Review/preparation is read-only and must not stop an active torrent.

FileOperationResultPresentation reports actual completed/cancelled/failure.
Optional setProgress reports actual file counts. Cancel during execute keeps the
dialog open until a matching actual outcome; completed work wins late cancel.
Invalidate on profile close; late old results are refused. setBusy(false) resumes
a deferred first preview. Destination edits clear approval and delete checkbox.

TorrentCreationDialog binds via beginSession(session,libraryRevision). Convert
options to the creator's v1 CreateOptions. sourceKind is picker intent; worker
must inspect actual source. Tracker tier limits/comment/piece sizes match pinned
creator header. Progress echoes identity plus monotone sequence and actual bytes;
scan total0 is unavailable, then total is fixed. Result.outputPublished=true for
PublishedDirectorySyncFailed or PublishedCleanupFailed and give the failure text.
The dialog blocks same-output retry after any actual publication. Import occurs
only if original request.importAfterCreation was explicitly true and root's
independent successful-create/import rules permit it. The widget never imports.

Stable IDs: file-operation-{dialog,destination,browse,paths,summary,status,
confirm-delete,review,execute,cancel}; torrent-creation-{dialog,source-kind,source,
source-browse,output,output-browse,piece-size,trackers,private,comment,import,
progress,status,create,cancel}. All labels use PlainText; tables use plain display
roles. The optional test-only BIGLYBTC_FILEOPS_CAPTURE_DIRECTORY must name an
existing owned absolute nonsymlink directory and writes file-operation-preview.png
and torrent-creation-preview.png. Empty environment performs no capture IO.

There are8 file-operation and9 creation behavioral test methods (plus Qt setup).
No Qt row counts/pass counts are claimed: these sources have NOT been compiled
or executed by the author. Root observes actual rows in its shared build batch.
