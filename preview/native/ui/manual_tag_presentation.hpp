#pragma once
#include "biglybtc/manual_tag_statistics.hpp"

#include <QMetaType>
#include <QString>

namespace biglybtc::ui {
enum class ManualTagReadState { Ready, Missing, Failed };
struct ManualTagStatisticsPacket {
  quint64 session = 0, epoch = 0, sequence = 0;
  quint64 libraryRevision = 0, tagRevision = 0;
  ManualTagReadState readState = ManualTagReadState::Failed;
  std::shared_ptr<const core::ManualTagMembership> membership;
  core::ManualTagTraffic traffic{};
  QString failure;
};
// Epoch0 is permitted only for unavailable/no-owner metadata. Once an actual
// epoch is accepted it cannot regress to0 within this profile binding. Reuse the
// same immutable membership pointer for unchanged tag/library revision values.
using ManualTagStatisticsPresentation = std::shared_ptr<const ManualTagStatisticsPacket>;
// Shared Window/View admission. Scalar watermarks are carried on every state,
// including Missing/Failed. Sequence is global to this profile Host, not epoch.
// Ready-to-Ready equal revisions must reuse the same sealed membership join.
inline bool acceptManualTagStatistics(const ManualTagStatisticsPresentation& previous,
    const ManualTagStatisticsPresentation& incoming, quint64 session) noexcept {
  if (!incoming || !session || incoming->session != session || !incoming->sequence
      || !incoming->libraryRevision || !incoming->tagRevision
      || incoming->failure.size() > 8192 || !core::validManualTagTraffic(incoming->traffic)
      || (!incoming->epoch && incoming->traffic.state != core::ManualTagTrafficState::Unavailable)) { return false; }
  if (previous && (previous->session != session || incoming->epoch < previous->epoch
      || incoming->sequence <= previous->sequence || incoming->libraryRevision < previous->libraryRevision
      || incoming->tagRevision < previous->tagRevision)) { return false; }
  try {
    switch (incoming->readState) {
      case ManualTagReadState::Ready: {
        const auto& member = incoming->membership;
        if (!member || member->session() != session || member->libraryRevision() != incoming->libraryRevision
            || member->tagRevision() != incoming->tagRevision || !incoming->failure.isEmpty()
            || member->rows().size() > core::MaximumManualTagRows
            || (incoming->traffic.target && !member->contains(incoming->traffic.target->hash))) { return false; }
        if (previous && previous->readState == ManualTagReadState::Ready && previous->membership) {
          const auto& old = previous->membership;
          if (member->libraryRevision() < old->libraryRevision() || member->tagRevision() < old->tagRevision()
              || (member->libraryRevision() == old->libraryRevision() && member->tagRevision() == old->tagRevision()
                  && member != old)) { return false; }
        }
        return true;
      }
      case ManualTagReadState::Missing: case ManualTagReadState::Failed:
        return !incoming->membership && incoming->traffic.state == core::ManualTagTrafficState::Unavailable
            && (incoming->readState != ManualTagReadState::Missing || incoming->failure.isEmpty());
    }
  } catch (...) { return false; }
  return false;
}
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::ManualTagStatisticsPresentation)
