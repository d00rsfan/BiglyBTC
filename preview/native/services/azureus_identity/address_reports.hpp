// SPDX-License-Identifier: GPL-2.0-or-later
// Original transport policy: Copyright (C) Azureus Software, Inc.
#pragma once
#include <QHostAddress>
#include <cstdint>
#include <expected>
namespace biglybtc::services::azureus_identity {
struct ReportState {std::int64_t lastChange=0;std::int32_t minimumPeriod=300000;bool deferred=false;bool operator==(const ReportState&)const=default;};
struct ReportInput {bool ipv6=false;QHostAddress current,reported,reporter;std::int64_t wallMilliseconds=0;std::size_t observedContacts=0;bool force=false;};
enum class ReportAction {Ignore,Defer,Recheck};
struct ReportDecision {ReportState state;ReportAction action=ReportAction::Ignore;};
enum class ReportError {Unresolved};
// A recheck decision is not an accepted address or network/source authority.
std::expected<ReportDecision,ReportError> addressReport(ReportState,const ReportInput&);
}
