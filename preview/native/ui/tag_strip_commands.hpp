// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "library_model.hpp"
namespace biglybtc::ui {
struct TagStripSettingsCommand {quint64 session=0,request=0;TagStripSettings expected,value;};
struct TagStripSettingsResult {quint64 session=0,request=0;TagStripSettings value;QString failure;};
enum class TagChipAssignmentKind {Category,Manual};
struct TagChipAssignmentCommand {quint64 session=0,request=0;TagChipAssignmentKind kind=TagChipAssignmentKind::Category;std::optional<QString> category;std::optional<qint32> tagId;DownloadTargets targets;bool longPress=false;};
struct TagChipAssignmentResult {quint64 session=0,request=0;std::size_t total=0,applied=0,changed=0;bool uncertain=false;QString failure;};
}
Q_DECLARE_METATYPE(biglybtc::ui::TagStripSettingsCommand)
Q_DECLARE_METATYPE(biglybtc::ui::TagStripSettingsResult)
Q_DECLARE_METATYPE(biglybtc::ui::TagChipAssignmentCommand)
Q_DECLARE_METATYPE(biglybtc::ui::TagChipAssignmentResult)
