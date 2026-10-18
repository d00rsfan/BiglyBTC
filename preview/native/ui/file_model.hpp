#pragma once

#include "library_model.hpp"

namespace biglybtc::ui {

class FileModel final : public QAbstractTableModel {
  Q_OBJECT
public:
  static constexpr int Columns = 5;
  static constexpr std::size_t MaximumRows = 100000;
  explicit FileModel(QObject* parent = nullptr);
  void select(std::optional<Target> target);
  bool apply(const FilePresentation&);
  std::optional<Target> target() const { return target_; }
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex&, int role) const override;
  QVariant headerData(int section, Qt::Orientation, int role) const override;
  std::optional<int> originalIndex(int row) const;
  int rowFor(int originalIndex) const;
private:
  std::optional<Target> target_;
  FilePresentation presentation_;
};

} // namespace biglybtc::ui
