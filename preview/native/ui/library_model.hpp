#pragma once

#include <biglybtc/download_action.hpp>

#include <QAbstractTableModel>
#include <QHash>
#include <QMetaType>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace biglybtc::ui {

struct Target {
  QString hash;
  quint64 generation = 0;
  bool operator==(const Target&) const = default;
};
using DownloadTargets = std::vector<Target>;
using FileIndices = std::vector<int>;
inline constexpr quint16 AllDownloadActions = (quint16{1} << 10) - 1;
constexpr quint16 downloadActionBit(core::DownloadAction action) noexcept {
  const auto ordinal = static_cast<unsigned>(action);
  return ordinal < 10 ? static_cast<quint16>(quint16{1} << ordinal) : quint16{0};
}

struct FileItem {
  int index;
  QString path;
  qint64 bytes;
  int priority;
  bool skipped;
  bool padding;
};
struct FileDetails {
  Target target;
  std::vector<FileItem> items;
  // Host grants editing only after the target has no retained engine handle.
  bool canEdit = false;
};
using FilePresentation = std::shared_ptr<const FileDetails>;
struct SavedSelectedValues {
  std::optional<qint64> bytes{};
  std::optional<qint64> remaining{};
  std::optional<qint32> percentThousandths{};
  quint32 defaults = 0;
  quint32 diagnostics = 0;
};
struct LibraryItem {
  QString key;
  QString hash;
  QString name;
  QString state;
  std::optional<qint64> bytes{};
  std::optional<qint64> completed{};
  std::optional<qint64> received{};
  std::optional<qint64> sent{};
  bool canContinue = false;
  std::optional<qint64> savedPosition{};
  std::optional<SavedSelectedValues> savedSelected{};
  // Current Host-authorized actions. Saved state names grant no permissions.
  quint16 actionMask = 0;
};
struct LibraryPresentation {
  quint64 revision = 0;
  std::vector<LibraryItem> items;
};
using Presentation = std::shared_ptr<const LibraryPresentation>;
struct Telemetry {
  Target target;
  qint64 completed;
  qint64 down;
  qint64 up;
  QString state;
};
struct Details {
  Target target;
  QString name;
  qint64 bytes;
  FilePresentation files{};
};

class LibraryModel final : public QAbstractTableModel {
  Q_OBJECT
public:
  static constexpr int Columns = 13;
  static constexpr int KeyRole = Qt::UserRole + 1;
  static constexpr int CompletionRole = Qt::UserRole + 2;
  static constexpr std::size_t MaximumRows = 100000;
  explicit LibraryModel(QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex&, int role) const override;
  QVariant headerData(int section, Qt::Orientation, int role) const override;
  bool apply(const Presentation& snapshot);
  bool apply(const Telemetry& update);
  bool apply(const Details& details);
  std::optional<bool> completionAt(int row) const;
  std::optional<Target> targetAt(int row) const;
  int rowFor(const QString& hash) const;
  bool accepts(const Target& target) const;
  bool canContinue(const Target& target) const;
  bool hasDetails(const Target& target) const;
  quint64 revision() const noexcept { return revision_; }
  quint16 permittedActions(const Target&) const;
  quint16 permittedActionsAt(int row) const;
private:
  struct Row {
    LibraryItem item;
    std::optional<qint64> down{};
    std::optional<qint64> up{};
  };
  std::vector<Row> rows_;
  QHash<QString, int> byHash_;
  quint64 revision_ = 0;
};
} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::Target)
Q_DECLARE_METATYPE(biglybtc::ui::DownloadTargets)
Q_DECLARE_METATYPE(biglybtc::ui::FileIndices)
Q_DECLARE_METATYPE(biglybtc::core::DownloadAction)
Q_DECLARE_METATYPE(biglybtc::ui::Presentation)
Q_DECLARE_METATYPE(biglybtc::ui::Telemetry)
Q_DECLARE_METATYPE(biglybtc::ui::Details)
