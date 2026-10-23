#pragma once
#include "biglybtc/passive_speed_mapper.hpp"
#include <QPointF>
#include <QRectF>
#include <QWidget>
#include <memory>
#include <optional>
#include <cstddef>
#include <cstdint>
class QMouseEvent;
class QPaintEvent;
class QShowEvent;
namespace biglybtc::ui {
class LiveSpeedMapWidget final : public QWidget {
  Q_OBJECT
public:
  explicit LiveSpeedMapWidget(QWidget* parent = nullptr);
  bool setSnapshot(std::shared_ptr<const core::PassiveSpeedSnapshot>, bool current);
  std::shared_ptr<const core::PassiveSpeedSnapshot> snapshot() const { return source_; }
  QRectF plotRect() const noexcept;
  std::optional<QPointF> point(std::size_t) const noexcept;
  QString pointDescription(std::size_t) const;
protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void showEvent(QShowEvent*) override;
private:
  void refresh();
  std::shared_ptr<const core::PassiveSpeedSnapshot> source_;
  quint64 revision_ = 0;
  std::uint32_t maxUpload_ = 1, maxDownload_ = 1;
  std::uint16_t maxMetric_ = 0;
  bool current_ = false, dirty_ = true;
};
} // namespace biglybtc::ui
