#pragma once

#include "library_model.hpp"
#include <QMetaType>
#include <QStringList>
#include <memory>
#include <optional>
#include <vector>

namespace biglybtc::ui {

enum class FileOperation { RemoveKeepData, RemoveDeleteData, Relocate, Relink };
struct FileOperationRequest {
  quint64 session=0, request=0, revision=0;
  Target target;
  FileOperation operation=FileOperation::RemoveKeepData;
  QString destinationParent;
  std::optional<bool> deleteTorrentFile{}; // null: resolve original preference in the worker.
  bool operator==(const FileOperationRequest&) const = default;
};
struct FileOperationPath {
  QString source, destination;
  // Actual existing leaf bytes. A missing leaf has no observed byte count;
  // its value is ignored and displayed as unavailable.
  quint64 bytes=0;
  bool present=false;
};
struct FileOperationPlanPacket {
  FileOperationRequest request;
  // Worker approval scope: identity/path/inode/destination decisions, not
  // incidental transfer counters changed by the required retirement checkpoint.
  QString approvalScopeDigest, sourceRoot, targetRoot;
  std::vector<FileOperationPath> paths;
  quint64 presentFiles=0, missingFiles=0, totalBytes=0; // totalBytes = presentBytes.
  QString failure;
  bool deleteTorrentFile=false;
  QString archivePath;
  quint64 archiveBytes=0;
  bool archivePresent=false,archiveInternalState=false;
  QString archiveBackupPath;
  quint64 archiveBackupBytes=0;
  bool archiveBackupPresent=false;
};
using FileOperationPlanPresentation=std::shared_ptr<const FileOperationPlanPacket>;
struct FileOperationSubmission {
  FileOperationPlanPresentation plan;
  bool deleteConfirmed=false;
  bool archiveConfirmed=false;
};
struct FileOperationResultPacket {
  FileOperationRequest request;
  bool completed=false, cancelled=false;
  QString failure;
};
using FileOperationResultPresentation=std::shared_ptr<const FileOperationResultPacket>;
struct FileOperationProgressPacket {
  FileOperationRequest request;
  quint64 sequence=0, completedFiles=0, totalFiles=0;
  QString currentPath;
};
using FileOperationProgressPresentation=std::shared_ptr<const FileOperationProgressPacket>;

enum class TorrentCreationSource { File, Directory };
enum class TorrentCreationFormat { V1, V2, Hybrid };
struct TorrentCreationOptions {
  TorrentCreationSource sourceKind=TorrentCreationSource::File;
  QString source, output;
  std::vector<QStringList> trackerTiers;
  quint64 pieceSize=0; // 0 selects the creator's qualified automatic policy.
  bool privateTorrent=false;
  QString comment;
  bool importAfterCreation=false; // Explicit request; creation alone never imports.
  TorrentCreationFormat format=TorrentCreationFormat::V1;
};
struct TorrentCreationRequest {
  quint64 session=0, request=0, revision=0;
  TorrentCreationOptions options;
};
struct TorrentCreationProgressPacket {
  quint64 session=0, request=0, revision=0, sequence=0;
  quint64 completedBytes=0, totalBytes=0;
};
using TorrentCreationProgressPresentation=std::shared_ptr<const TorrentCreationProgressPacket>;
struct TorrentCreationResultPacket {
  quint64 session=0, request=0, revision=0;
  bool completed=false, cancelled=false;
  QString output, hash, failure;
  // A failed post-publication directory sync is not permission to retry the
  // same output. The worker reports actual publication separately.
  bool outputPublished=false;
};
using TorrentCreationResultPresentation=std::shared_ptr<const TorrentCreationResultPacket>;

} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::FileOperationRequest)
Q_DECLARE_METATYPE(biglybtc::ui::FileOperationPlanPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::FileOperationSubmission)
Q_DECLARE_METATYPE(biglybtc::ui::FileOperationResultPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::FileOperationProgressPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::TorrentCreationRequest)
Q_DECLARE_METATYPE(biglybtc::ui::TorrentCreationProgressPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::TorrentCreationResultPresentation)
