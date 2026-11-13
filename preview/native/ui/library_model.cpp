#include "library_model.hpp"

#include <algorithm>
#include <QDateTime>
#include <limits>
#include <biglybtc/queue_target_statistics.hpp>

namespace biglybtc::ui {
namespace {
QVariant number(std::optional<qint64> value, int role) {
  if (value) { return QVariant::fromValue(*value); }
  return role == Qt::UserRole ? QVariant{} : QVariant{QString::fromUtf8("—")};
}
QString savedSelectedTooltip() {
  return LibraryModel::tr("Saved snapshot of selected files, excluding skipped files. These values are not live progress. An em dash means the saved data is unavailable or inconsistent.");
}
}

LibraryModel::LibraryModel(QObject* parent) : QAbstractTableModel(parent) {}
int LibraryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
int LibraryModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : Columns; }

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || index.column() < 0 || index.column() >= Columns) { return {}; }
  const auto& row = rows_[static_cast<std::size_t>(index.row())];
  if (role == KeyRole) { return row.item.key; }
  if (role == CompletionRole) { return completionAt(index.row()).value_or(false); }
  if(role==Qt::ToolTipRole&&index.column()==1){
    QString tooltip=row.item.networkStatus;
    if(row.item.trackerScrape){const auto& value=*row.item.trackerScrape;
      if(value.failed){tooltip=tr("Tracker scrape failed");}
      if(value.seeds>=0||value.peers>=0){if(!tooltip.isEmpty()){tooltip+='\n';}
        tooltip+=tr("Retained tracker counts: %1 seeds, %2 peers").arg(value.seeds<0?QString::fromUtf8("—"):QString::number(value.seeds)).arg(value.peers<0?QString::fromUtf8("—"):QString::number(value.peers));}
      tooltip+=tr("\nLast tracker attempt observed %1").arg(QDateTime::fromMSecsSinceEpoch(value.observedWallMillis).toString(Qt::ISODate));
    }
    if(row.item.mainlineEstimate){const auto& value=*row.item.mainlineEstimate;if(!tooltip.isEmpty()){tooltip+="\n\n";}
      tooltip+=tr("Mainline estimate: %1 seeds, %2 peers\nObserved %3\nSource: Mainline DHT (BEP33 estimate)")
          .arg(value.seeds).arg(value.peers).arg(QDateTime::fromMSecsSinceEpoch(value.startedWallMillis).toString(Qt::ISODate));
    }
    if(!row.item.resumeStatus.isEmpty()){if(!tooltip.isEmpty()){tooltip+='\n';}tooltip+=row.item.resumeStatus;}
    if(!tooltip.isEmpty()){return tooltip;}
  }
  if(index.column()>=15){
    const auto& value=row.item.originalValues[static_cast<std::size_t>(index.column()-15)];
    if(index.column()==16&&value.metaType().id()==QMetaType::QVariantMap){const auto info=value.toMap();if(role==Qt::DisplayRole||role==Qt::UserRole){return info.value(QStringLiteral("text"));}if(role==Qt::ToolTipRole){auto detail=info.value(QStringLiteral("detail")).toString();if(detail.isEmpty()){detail=info.value(QStringLiteral("text")).toString();}return QStringLiteral("<qt>")+detail.toHtmlEscaped().replace(QLatin1Char('\n'),QStringLiteral("<br>"))+QStringLiteral("</qt>");}return {};}
    if(role==Qt::ToolTipRole){return value.isValid()?QVariant{}:QVariant{tr("This value is not available from the current service or saved state")};}
    if(role==Qt::UserRole){if(value.isValid()&&((index.column()==25||index.column()==26)&&value.toLongLong()==-1)){return QVariant::fromValue(qint64{2147483647});}return value;}
    if(role!=Qt::DisplayRole){return {};}
    if(!value.isValid()){return QString::fromUtf8("—");}
    const auto number=value.toLongLong();
    switch(index.column()){
      case 15: return value.toDouble()==std::numeric_limits<double>::infinity()?QString::fromUtf8("∞"):QString::number(value.toDouble(),'f',3);
      case 21: case 22: return number>0?QDateTime::fromMSecsSinceEpoch(number).toString(QStringLiteral("yyyy-MM-dd HH:mm")):QString::fromUtf8("—");
      case 23: return number==0?QString::fromUtf8("∞"):number<0?tr("Disabled"):transferBytes(number,units_,true);
      case 24: return number==core::SourceEtaInfinity?QString::fromUtf8("∞"):number<0?QString::fromUtf8("—"):transferUptime(number);
      case 25: case 26: return number==-2?QString{}:number<0?QString::fromUtf8("∞"):transferUptime(number);
      case 27: return transferBytes(number,units_);
      default:return value;
    }
  }
  if (role == Qt::ToolTipRole && index.column()==14) { return row.item.trackerName; }
  if (role == Qt::ToolTipRole && index.column()==13) { return row.item.savePath; }
  if (role == Qt::ToolTipRole) { return index.column() >= 10 && index.column()<=12 ? QVariant{savedSelectedTooltip()} : QVariant{}; }
  if (role != Qt::DisplayRole && role != Qt::UserRole && role != Qt::AccessibleTextRole) { return {}; }
  const auto bytes=[&](std::optional<qint64> value,bool rate=false)->QVariant{
    if(!value||role==Qt::UserRole){return number(value,role);}return transferBytes(*value,units_,rate);
  };
  switch (index.column()) {
    case 0: return row.item.name;
    case 1: return row.item.state;
    case 2: return bytes(row.item.bytes);
    case 3: {
      if(row.item.assumedComplete.value_or(false)){return role==Qt::UserRole?QVariant{100.0}:QVariant{QStringLiteral("100.0%")};}
      if(row.item.savedSelected&&row.item.savedSelected->percentThousandths){const auto percent=static_cast<double>(*row.item.savedSelected->percentThousandths)/10.0;return role==Qt::UserRole?QVariant{percent}:QVariant{QString::number(percent,'f',1)+'%'};}
      if (!row.item.bytes || !row.item.completed) { return number(std::nullopt, role); }
      const double percent = *row.item.bytes == 0 ? 100.0
          : std::clamp(100.0 * static_cast<double>(*row.item.completed) / static_cast<double>(*row.item.bytes), 0.0, 100.0);
      return role == Qt::UserRole ? QVariant{percent} : QVariant{QString::number(percent, 'f', 1) + '%'};
    }
    case 4: case 5: {
      // Original Up/DownSpeedItem uses numeric zero for absent statistics and
      // an empty cell for a zero rate. Keep telemetry optional outside the UI.
      const auto value=(index.column()==4?row.down:row.up).value_or(0);
      return role==Qt::UserRole?QVariant::fromValue(value):value==0?QVariant{QString{}}:QVariant{transferBytes(value,units_,true)};
    }
    case 6: return bytes(row.item.goodReceived?row.item.goodReceived:row.item.received);
    case 7: return bytes(row.item.sent);
    case 8: return row.item.hash;
    case 9: return number(row.item.savedPosition, role);
    case 10: return bytes(row.item.savedSelected ? row.item.savedSelected->bytes : std::optional<qint64>{});
    case 11: return bytes(row.item.savedSelected ? row.item.savedSelected->remaining : std::optional<qint64>{});
    case 13: return row.item.savePath;
    case 14: return row.item.trackerName;
    case 12: {
      const auto percent = row.item.savedSelected ? row.item.savedSelected->percentThousandths : std::optional<qint32>{};
      if (!percent) { return number(std::nullopt, role); }
      if (role == Qt::UserRole) { return QVariant::fromValue(*percent); }
      return QStringLiteral("%1.%2%").arg(*percent / 10).arg(*percent % 10);
    }
    default: return {};
  }
}

QVariant LibraryModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || section < 0 || section >= Columns) { return {}; }
  if (role == Qt::ToolTipRole && section == 9) { return tr("Position in the downloading or seeding queue"); }
  if (role == Qt::ToolTipRole && section == 13) { return tr("Destination folder or file for the torrent data"); }
  if (role == Qt::ToolTipRole && section == 14) { return tr("Tracker host; account credentials and URL path are omitted"); }
  if (role == Qt::ToolTipRole && section >= 10 && section <= 12) { return savedSelectedTooltip(); }
  if (role != Qt::DisplayRole) { return {}; }
  return tr(core::ClassicColumnCatalog[static_cast<std::size_t>(section)].label.data());
}

bool LibraryModel::apply(const Presentation& snapshot) {
  if (!snapshot || snapshot->revision <= revision_ || snapshot->items.size() > MaximumRows) { return false; }
  QHash<QString, int> hashes;
  QHash<QString, bool> keys;
  for (std::size_t i = 0; i < snapshot->items.size(); ++i) {
    const auto& item = snapshot->items[i];
    if (item.key.isEmpty() || keys.contains(item.key) || (item.bytes && *item.bytes < 0)
        || (item.actionMask & ~AllDownloadActions) != 0) { return false; }
    if(item.trackerScrape){const auto& value=*item.trackerScrape;
      if(value.seeds< -1||value.peers< -1||value.observedWallMillis<0||(!value.failed&&value.seeds==-1&&value.peers==-1)){return false;}}

    if(item.mainlineEstimate&&(item.mainlineEstimate->seeds<0||item.mainlineEstimate->peers<0
        ||(item.mainlineEstimate->seeds==0&&item.mainlineEstimate->peers==0)||item.mainlineEstimate->startedWallMillis<0)){return false;}
    if (item.savedSelected) {
      const auto& saved = *item.savedSelected;
      if ((saved.bytes && *saved.bytes < 0) || (saved.remaining && *saved.remaining < 0)
          || (saved.bytes && saved.remaining && *saved.remaining > *saved.bytes)
          || (saved.percentThousandths && (*saved.percentThousandths < 0 || *saved.percentThousandths > 1000))) { return false; }
    }
    if((item.liveDownloadRate&&*item.liveDownloadRate<0)||(item.liveUploadRate&&*item.liveUploadRate<0)){return false;}
    keys.insert(item.key, true);
    if (!item.hash.isEmpty()) {
      if (hashes.contains(item.hash)) { hashes[item.hash] = -1; }
      else { hashes.insert(item.hash, static_cast<int>(i)); }
    }
  }
  bool sameRows = rows_.size() == snapshot->items.size();
  if (sameRows) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      if (rows_[i].item.key != snapshot->items[i].key) { sameRows = false; break; }
    }
  }
  if (!sameRows) { beginResetModel(); }
  std::vector<Row> replacement;
  replacement.reserve(snapshot->items.size());
  for (std::size_t i = 0; i < snapshot->items.size(); ++i) {
    auto item = snapshot->items[i];
    if (sameRows && !item.bytes) { item.bytes = rows_[i].item.bytes; }
    Row row{std::move(item)};row.down=row.item.liveDownloadRate;row.up=row.item.liveUploadRate;
    replacement.push_back(std::move(row));
  }
  rows_ = std::move(replacement);
  byHash_ = std::move(hashes);
  revision_ = snapshot->revision;units_=snapshot->units;
  if (!sameRows) { endResetModel(); }
  else if (!rows_.empty()) { emit dataChanged(index(0, 0), index(rowCount() - 1, Columns - 1)); }
  return true;
}

int LibraryModel::rowFor(const QString& hash) const { return byHash_.value(hash, -1); }
std::optional<bool> LibraryModel::completionAt(int row) const {
  if (row < 0 || row >= rowCount()) { return std::nullopt; }
  const auto& item = rows_[static_cast<std::size_t>(row)].item;
  if (item.assumedComplete) { return item.assumedComplete; }
  if (!item.bytes || *item.bytes < 0 || !item.completed || *item.completed < 0) { return std::nullopt; }
  return *item.completed >= *item.bytes;
}
bool LibraryModel::accepts(const Target& target) const { return target.generation == revision_ && rowFor(target.hash) >= 0; }
std::optional<Target> LibraryModel::targetAt(int row) const {
  if (row < 0 || row >= rowCount()) { return std::nullopt; }
  const auto& hash = rows_[static_cast<std::size_t>(row)].item.hash;
  if (hash.isEmpty() || rowFor(hash) < 0) { return std::nullopt; }
  return Target{hash, revision_};
}
bool LibraryModel::canContinue(const Target& target) const {
  return accepts(target) && rows_[static_cast<std::size_t>(rowFor(target.hash))].item.canContinue;
}
bool LibraryModel::canRecheck(const Target& target) const {
  return accepts(target)&&rows_[static_cast<std::size_t>(rowFor(target.hash))].item.canRecheck;
}
quint16 LibraryModel::permittedActionsAt(int row) const {
  return row >= 0 && row < rowCount() ? rows_[static_cast<std::size_t>(row)].item.actionMask : quint16{0};
}
quint16 LibraryModel::permittedActions(const Target& target) const {
  return accepts(target) ? permittedActionsAt(rowFor(target.hash)) : quint16{0};
}
bool LibraryModel::hasDetails(const Target& target) const {
  return accepts(target) && rows_[static_cast<std::size_t>(rowFor(target.hash))].item.bytes.has_value();
}
bool LibraryModel::apply(const Telemetry& update) {
  if (!accepts(update.target) || update.completed < 0 || update.down < 0 || update.up < 0) { return false; }
  const auto row = rowFor(update.target.hash);
  auto& value = rows_[static_cast<std::size_t>(row)];
  value.item.completed = update.completed;
  value.item.state = update.state;
  value.down = update.down;
  value.up = update.up;
  emit dataChanged(index(row, 1), index(row, 5));
  return true;
}
bool LibraryModel::apply(const Details& details) {
  if (!accepts(details.target) || details.bytes < 0 || (details.files && details.files->target != details.target)) { return false; }
  const auto row = rowFor(details.target.hash);
  auto& value = rows_[static_cast<std::size_t>(row)].item;
  value.bytes = details.bytes;
  if (!details.name.isEmpty()) { value.name = details.name; }
  emit dataChanged(index(row, 0), index(row, 3));
  return true;
}
} // namespace biglybtc::ui
