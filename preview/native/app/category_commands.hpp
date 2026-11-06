// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../ui/library_model.hpp"
#include "biglybtc/saved_categories.hpp"
namespace biglybtc::ui {
enum class CategoryCommandKind {Reload,Create,Assign,Remove};
enum class CategoryCommandStatus {Loaded,Saved,Unchanged,Rejected,Cancelled};
enum class CategoryReadState {Ready,Missing,Failed};
struct CategoryCommand {
  quint64 session=0,request=0,generation=0;
  CategoryCommandKind kind=CategoryCommandKind::Reload;
  // Null for Assign means original setCategory(null), not an empty user name.
  std::optional<QString> name;
  Target target;
};
struct CategoryCommandPacket {
  quint64 session=0,request=0,generation=0,libraryRevision=0;
  CategoryCommandKind kind=CategoryCommandKind::Reload;
  CategoryCommandStatus status=CategoryCommandStatus::Rejected;
  CategoryReadState readState=CategoryReadState::Failed;
  bool writable=false,changed=false,deletionSupported=false,recoveryRequired=false;
  std::shared_ptr<const core::SavedCategories> categories;
  Target requestedTarget;
  std::optional<Target> target; // Fresh actual selection after library refresh.
  std::optional<QString> assignedName;
  bool assignmentLoaded=false,assignmentResolved=false;
  QString failure,refreshFailure;
};
using CategoryCommandPresentation=std::shared_ptr<const CategoryCommandPacket>;
// Observational owning values only. No CategoryTargetSnapshot/file/lease enters
// the GUI. Host rechecks the latest generation, target and actual source/CAS.
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::CategoryCommand)
Q_DECLARE_METATYPE(biglybtc::ui::CategoryCommandPresentation)
