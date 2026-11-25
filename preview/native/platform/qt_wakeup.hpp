// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>

#include <memory>
#include <utility>

#ifdef _WIN32
#include <QWinEventNotifier>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <QSocketNotifier>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/eventfd.h>
#endif
#endif

namespace biglybtc::platform {

// A coalescing notification for an already bounded producer/consumer queue.
// The notifier must be destroyed before the event or descriptors it observes.
class QtWakeup final {
public:
#ifdef _WIN32
  using Notifier = QWinEventNotifier;
  QtWakeup() noexcept : event_(::CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
  ~QtWakeup() { if (event_) { ::CloseHandle(event_); } }
  bool valid() const noexcept { return event_ != nullptr; }
  void signal() const noexcept { ::SetEvent(event_); }
  void consume() const noexcept {} // The Windows wait consumes the auto-reset event.
#else
  using Notifier = QSocketNotifier;
#ifdef __linux__
  QtWakeup() noexcept : read_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)), write_(read_) {}
#else
  QtWakeup() noexcept {
    int ends[2] = {-1, -1};
    if (::pipe(ends) != 0) { return; }
    for (const auto fd : ends) {
      if (::fcntl(fd, F_SETFL, O_NONBLOCK) != 0 || ::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        ::close(ends[0]);
        ::close(ends[1]);
        return;
      }
    }
    read_ = ends[0];
    write_ = ends[1];
  }
#endif
  ~QtWakeup() {
    if (read_ >= 0) { ::close(read_); }
    if (write_ >= 0 && write_ != read_) { ::close(write_); }
  }
  bool valid() const noexcept { return read_ >= 0; }
  void signal() const noexcept {
#ifdef __linux__
    const std::uint64_t value = 1;
#else
    const char value = 1;
#endif
    while (::write(write_, &value, sizeof(value)) < 0 && errno == EINTR) {}
  }
  void consume() const noexcept {
#ifdef __linux__
    std::uint64_t data[1]{};
#else
    char data[256]{};
#endif
    while (::read(read_, data, sizeof(data)) < 0 && errno == EINTR) {}
  }
#endif

  QtWakeup(const QtWakeup&) = delete;
  QtWakeup& operator=(const QtWakeup&) = delete;

  template<class Callback>
  std::unique_ptr<Notifier> watch(QObject* receiver, Callback&& callback) const {
#ifdef _WIN32
    auto notifier = std::make_unique<Notifier>(event_, receiver);
#else
    auto notifier = std::make_unique<Notifier>(read_, Notifier::Read, receiver);
#endif
    QObject::connect(notifier.get(), &Notifier::activated, receiver, std::forward<Callback>(callback));
    return notifier;
  }

private:
#ifdef _WIN32
  HANDLE event_ = nullptr;
#else
  int read_ = -1;
  int write_ = -1;
#endif
};

} // namespace biglybtc::platform
