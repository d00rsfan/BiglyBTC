#pragma once
#include "biglybtc/file_policy.hpp"
#include "biglybtc/torrent_intake_options.hpp"
#include <QMetaType>
#include <QString>
#include <memory>
#include <limits>
#include <vector>

namespace biglybtc::ui {
inline constexpr std::size_t MaximumIntakePreviewFiles=10000;
struct TorrentIntakeRequest {
  quint64 session=0,request=0;
  QString metadata;
  bool operator==(const TorrentIntakeRequest&)const=default;
};
struct TorrentIntakeFile {
  int index=0;QString path;qint64 bytes=0;bool padding=false;
};
struct TorrentIntakePacket {
  TorrentIntakeRequest source;
  quint64 revision=0;
  QString hash,name;
  std::vector<TorrentIntakeFile> files;
  QString failure;
};
using TorrentIntakePresentation=std::shared_ptr<const TorrentIntakePacket>;
enum class TorrentIntakeStartMode { Queued, Stopped, Paused, Forced };
struct TorrentIntakeSubmission {
  quint64 session=0,request=0,revision=0;
  QString destination;
  bool start=true;
  std::vector<compat::FilePolicy> files; // Complete original-index order, padding included.
  core::TorrentIntakeLimits limits{};
  bool sequentialDownload=false;
  bool forceStart=false;
  bool paused=false;
};
struct TorrentIntakeResultPacket {
  quint64 session=0,request=0,revision=0;
  bool imported=false,started=false;
  QString hash,failure;
};
using TorrentIntakeResultPresentation=std::shared_ptr<const TorrentIntakeResultPacket>;
inline bool validTorrentIntake(const TorrentIntakePresentation& packet) {
  if(!packet||!packet->source.session||!packet->source.request||!packet->revision
      ||!packet->failure.isEmpty()||packet->files.empty()||packet->files.size()>MaximumIntakePreviewFiles
      ||(packet->hash.size()!=40&&packet->hash.size()!=64)||packet->name.isEmpty()||packet->name.size()>4096){return false;}
  for(const auto c:packet->hash){if(!((c>=u'0'&&c<=u'9')||(c>=u'a'&&c<=u'f')||(c>=u'A'&&c<=u'F'))){return false;}}
  qsizetype total=0;qint64 totalBytes=0;
  for(std::size_t n=0;n<packet->files.size();++n){
    const auto& file=packet->files[n];
    if(file.index!=static_cast<int>(n)||file.bytes<0||file.path.isEmpty()||file.path.size()>4096
        ||file.path.size()>4*1024*1024-total||file.bytes>std::numeric_limits<qint64>::max()-totalBytes){return false;}
    total+=file.path.size();totalBytes+=file.bytes;
  }
  return true;
}
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::TorrentIntakeRequest)
Q_DECLARE_METATYPE(biglybtc::ui::TorrentIntakePresentation)
Q_DECLARE_METATYPE(biglybtc::ui::TorrentIntakeSubmission)
Q_DECLARE_METATYPE(biglybtc::ui::TorrentIntakeResultPresentation)
