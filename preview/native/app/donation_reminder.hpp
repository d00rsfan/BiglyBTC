// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace biglybtc::app {

struct DonationReminder {
  static constexpr std::int64_t Day = 24 * 60 * 60 * 1000;
  static constexpr std::int64_t InitialHours = 48;
  static constexpr std::int64_t RepeatHours = 96;
  static constexpr std::int64_t PostponedHours = 168;

  std::int64_t nextHours = 0;
  std::int64_t minDate = 0;
  std::int64_t maxDate = 0;
  bool donated = false;

  bool operator==(const DonationReminder&) const = default;

  // DonationWindow.checkForDonationPopup runs only on a fresh persistent addition.
  bool onTorrentAdded(std::int64_t hours, std::int64_t now) {
    maxDate = std::min(maxDate, now + 180 * Day);
    const bool dueByDate = maxDate > 0 && now > maxDate;
    if (dueByDate) { maxDate += Day; }
    if (donated && !dueByDate) { return false; }
    if (nextHours == 0) {
      nextHours = hours + InitialHours;
      return false;
    }
    if (hours < nextHours && !dueByDate) { return false; }
    minDate = std::min(minDate, now + 60 * Day);
    if (minDate > 0 && minDate > now) { return false; }
    if (donated) { setCalendar(now, false); }
    nextHours = hours + RepeatHours;
    return true;
  }

  void postpone(std::int64_t hours, std::int64_t now) {
    nextHours = hours + PostponedHours;
    setCalendar(now, false);
  }

  void markDonated(std::int64_t now) {
    donated = true;
    setCalendar(now, true);
  }

private:
  void setCalendar(std::int64_t now, bool afterDonation) {
    minDate = now + (afterDonation ? 60 : 30) * Day;
    maxDate = now + (afterDonation ? 180 : 120) * Day;
  }
};

} // namespace biglybtc::app
