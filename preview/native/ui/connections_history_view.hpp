#pragma once

#include "biglybtc/runtime_metrics.hpp"

#include <QPointer>
#include <QWidget>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

class QComboBox;
class QLabel;
class QShowEvent;

namespace biglybtc::ui {

enum class ConnectionGauge : std::size_t {
  Connected, Connecting, Unchoked, InterestedInUs, InterestedInPeers,
  UploadRequestPeers, DownloadRequestPeers, SendWaitingSockets, ReceiveWaitingSockets, Count
};
inline constexpr std::size_t ConnectionGaugeCount = static_cast<std::size_t>(ConnectionGauge::Count);
using ConnectionValues = std::array<std::optional<std::int64_t>, ConnectionGaugeCount>;
ConnectionValues projectConnectionValues(const core::RuntimeMetrics*) noexcept;

struct ConnectionHistoryPoint {
  std::uint64_t sequence = 0;
  std::int64_t nanoseconds = 0;
  ConnectionValues values{};
  bool connectedToPrevious = false;
};
struct ConnectionHistoryFrame {
  static constexpr std::size_t MaximumPoints = 600;
  std::array<ConnectionHistoryPoint, MaximumPoints> points{};
  std::size_t count = 0;
};

// Value-only ring of actual response observations. Gauges may decrease. A
// missing field interrupts only that series; a missing response or skipped
// response sequence interrupts all series. No invented samples, rate calculation
// or snapshot alias is retained.
class ConnectionHistory final {
public:
  bool observe(const core::RuntimeMetrics&) noexcept;
  void gap() noexcept { connected_ = false; }
  void clear() noexcept;
  ConnectionHistoryFrame frame() const noexcept;
private:
  std::array<ConnectionHistoryPoint, ConnectionHistoryFrame::MaximumPoints> points_{};
  std::size_t begin_ = 0, count_ = 0;
  std::uint64_t sequence_ = 0;
  std::int64_t nanoseconds_ = 0;
  bool connected_ = false;
};

// Fed only after RuntimeMetricsView admits the packet. Cache updates are plain
// bounded value copies, including while hidden; rendering never samples or reads.
class ConnectionsHistoryView final : public QWidget {
  Q_OBJECT
public:
  explicit ConnectionsHistoryView(QWidget* parent = nullptr);
  void cache(const core::RuntimeMetrics*, bool current, bool reset = false) noexcept;
  void clear() noexcept;
  void refresh();
  ConnectionHistoryFrame activity() const noexcept { return history_.frame(); }
protected:
  void showEvent(QShowEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  void queueRefresh();
  ConnectionHistory history_;
  ConnectionValues latest_{};
  std::uint64_t sequence_ = 0, revision_ = 0;
  std::int64_t nanoseconds_ = 0;
  bool current_ = false, dirty_ = true, rendering_ = false, queued_ = false;
  QPointer<QLabel> interested_;
  QPointer<QComboBox> mode_;
  QPointer<QWidget> plot_, observedWindow_;
};

} // namespace biglybtc::ui
