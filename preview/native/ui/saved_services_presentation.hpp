#pragma once

#include "saved_service_pages.hpp"

#include <QMetaType>

namespace biglybtc::ui {

// Independent saved-file results; only Ready carries source and snapshot.
// Failure strings are bounded owner messages, never raw configuration bytes.
template<typename Snapshot>
struct SavedServiceResult {
  SavedPageState state = SavedPageState::Failed;
  std::optional<SavedPageSource> source;
  std::shared_ptr<const Snapshot> snapshot;
  QString failure;
};
using SavedHostingPresentation = SavedServiceResult<core::SavedHosting>;
using SavedSharesPresentation = SavedServiceResult<core::SavedShares>;
struct SavedServicesPacket {
  quint64 request = 0;
  SavedHostingPresentation hosting;
  SavedSharesPresentation sharing;
};
using SavedServicesPresentation = std::shared_ptr<const SavedServicesPacket>;

} // namespace biglybtc::ui

Q_DECLARE_METATYPE(biglybtc::ui::SavedServicesPresentation)
