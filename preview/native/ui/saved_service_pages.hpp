#pragma once

#include <QAbstractTableModel>
#include <QWidget>
#include <QString>

#include <cstddef>
#include <memory>
#include <optional>

class QLabel;
class QSortFilterProxyModel;
class QTableView;

namespace biglybtc::core {
struct SavedHosting;
struct SavedShares;
}

namespace biglybtc::ui {

enum class SavedPageState { NotLoaded, Loading, Missing, Ready, Failed };
enum class SavedPageSource { Primary, Backup, Saving };

// Numeric Qt::UserRole sorting, SourceIndexRole identity and plain display data.
// Models recheck default row/field/text/issue caps and unique source indices;
// rejected input keeps the prior model. No raw ToolTipRole is exposed.
class SavedHostingModel final : public QAbstractTableModel {
public:
  static constexpr int Columns = 10;
  static constexpr int SourceIndexRole = Qt::UserRole + 1;
  static constexpr std::size_t MaximumRows = 10000;
  explicit SavedHostingModel(QObject* parent = nullptr);
  ~SavedHostingModel() override;
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex&, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
  bool setSnapshot(std::shared_ptr<const core::SavedHosting> snapshot);
  void clear();
  std::optional<std::size_t> sourceIndexAt(int row) const;
private:
  std::shared_ptr<const core::SavedHosting> snapshot_;
};

class SavedSharesModel final : public QAbstractTableModel {
public:
  static constexpr int Columns = 6;
  static constexpr int SourceIndexRole = Qt::UserRole + 1;
  static constexpr std::size_t MaximumRows = 10000;
  explicit SavedSharesModel(QObject* parent = nullptr);
  ~SavedSharesModel() override;
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex&, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
  bool setSnapshot(std::shared_ptr<const core::SavedShares> snapshot);
  void clear();
  std::optional<std::size_t> sourceIndexAt(int row) const;
private:
  std::shared_ptr<const core::SavedShares> snapshot_;
};

// GUI-thread, externally serialized API. Supplied shared snapshots must remain
// immutable. Request generations belong to the owner, increase without wrap,
// and are accepted only once while Loading; completion never initiates I/O.
class SavedServicePage : public QWidget {
public:
  ~SavedServicePage() override;
  bool beginRequest(quint64 generation);
  bool setMissing(quint64 generation);
  bool setFailed(quint64 generation, QString message);
  SavedPageState state() const { return state_; }
  quint64 generation() const { return generation_; }
  QTableView& table();
  QString statusText() const;
  QString provenanceText() const;
protected:
  SavedServicePage(QString id, QString title, QString footer, QWidget* parent);
  void installModel(QAbstractItemModel* model);
  bool accepts(quint64 generation) const;
  void ready(SavedPageSource source, bool listPresent, std::size_t rows, std::size_t issues);
  virtual void clearSnapshot() = 0;
private:
  SavedPageState state_ = SavedPageState::NotLoaded;
  quint64 generation_ = 0;
  QLabel* status_;
  QLabel* provenance_;
  QTableView* table_;
  QSortFilterProxyModel* proxy_;
};

class SavedHostingPage final : public SavedServicePage {
public:
  explicit SavedHostingPage(QWidget* parent = nullptr);
  SavedHostingModel& model() { return *model_; }
  // Null/over-cap/inconsistent input fails the current request, returns false.
  // Stale/duplicate completions return false without altering any state.
  bool setReady(quint64 generation, std::shared_ptr<const core::SavedHosting> snapshot,
      SavedPageSource source);
private:
  void clearSnapshot() override;
  SavedHostingModel* model_;
};

class SavedSharesPage final : public SavedServicePage {
public:
  explicit SavedSharesPage(QWidget* parent = nullptr);
  SavedSharesModel& model() { return *model_; }
  bool setReady(quint64 generation, std::shared_ptr<const core::SavedShares> snapshot,
      SavedPageSource source);
private:
  void clearSnapshot() override;
  SavedSharesModel* model_;
};

} // namespace biglybtc::ui
