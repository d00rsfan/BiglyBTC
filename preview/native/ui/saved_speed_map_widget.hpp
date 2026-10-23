#pragma once

#include "saved_speed_manager_presentation.hpp"
#include "biglybtc/saved_speed_map.hpp"

#include <QPointF>
#include <QRectF>
#include <QWidget>
#include <cstddef>
#include <memory>
#include <optional>

class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QShowEvent;

namespace biglybtc::ui {
// A saved-only projection. No clock, network provider, file access or sampler.
class SavedSpeedMapWidget final : public QWidget {
  Q_OBJECT
public:
  explicit SavedSpeedMapWidget(QWidget* parent = nullptr);
  bool setPresentation(SavedSpeedManagerPresentation, quint64 expectedSession);
  SavedSpeedManagerPresentation presentation() const { return packet_; }
  std::shared_ptr<const core::SavedSpeedMap> map() const { return map_; }
  std::optional<std::size_t> selectedRecord() const noexcept { return selected_; }
  QRectF plotRect() const noexcept;
  std::optional<QPointF> point(std::size_t record) const noexcept;
  QString recordDescription(std::size_t record) const;
protected:
  void paintEvent(QPaintEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void showEvent(QShowEvent*) override;
  void changeEvent(QEvent*) override;
  void focusInEvent(QFocusEvent*) override;
  void focusOutEvent(QFocusEvent*) override;
private:
  QString summary() const;
  QString description() const;
  void select(std::size_t);
  void refresh();
  void queueRefresh();
  quint64 session_ = 0, revision_ = 0;
  bool dirty_ = true, queued_ = false, publishing_ = false;
  SavedSpeedManagerPresentation packet_;
  std::shared_ptr<const core::SavedSpeedMap> map_;
  std::optional<core::SavedSpeedMapError> error_;
  std::optional<std::size_t> selected_;
};
} // namespace biglybtc::ui
