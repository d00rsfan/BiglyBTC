// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../ui/library_model.hpp"
#include "../ui/manual_tag_presentation.hpp"
#include "biglybtc/saved_download_tags.hpp"
#include <array>

namespace biglybtc::ui {
enum class ManualTagCommandKind { Reload, Create, Rename, Remove, Visibility, AddMember, RemoveMember, SetConstraint, SetGroup, SetColour };
enum class ManualTagCommandStatus { Loaded, Saved, Unchanged, Rejected, Cancelled };
struct ManualTagCommand {
  quint64 session=0,request=0,generation=0;
  ManualTagCommandKind kind=ManualTagCommandKind::Reload;
  qint32 tagId=0;
  QString name;
  bool visible=true;
  // Only membership commands use this actual current library selection.
  // It grants neither a torrent handle nor payload/rule/network permission.
  Target member;
  QString constraintExpression,constraintOptions;
  bool constraintEnabled=true;
  std::optional<QString> group; // Null clears; explicit empty remains distinct in the typed API.
  std::optional<std::array<qint32,3>> colour; // Null requests original default/clear.
};
struct ManualTagCommandPacket {
  quint64 session=0,request=0,generation=0,libraryRevision=0;
  ManualTagCommandKind kind=ManualTagCommandKind::Reload;
  ManualTagCommandStatus status=ManualTagCommandStatus::Rejected;
  ManualTagReadState readState=ManualTagReadState::Failed;
  // Hint from actual captured candidates only; worker CAS remains mandatory.
  bool writable=false,changed=false;
  std::shared_ptr<const core::SavedDownloadTags> tags;
  std::optional<qint32> createdId;
  std::optional<qint64> createdUid;
  QString failure;
  QString constraintStatus; // Configuration diagnostic only, never evaluator/provider authority.
};
using ManualTagCommandPresentation=std::shared_ptr<const ManualTagCommandPacket>;
// One request slot. Reload ignores generation; all edits require the latest
// Host-scoped generation. Requests are nonzero and increasing per profile.
// Results own immutable saved data, never a retained profile/file capability.
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::ManualTagCommand)
Q_DECLARE_METATYPE(biglybtc::ui::ManualTagCommandPresentation)
