// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>

namespace biglybtc::services::http {
// Closed pure-decoder profiles. Neither profile grants a route or performs IO.
// Tracker preserves the existing128KiB contract. RSS and binary metainfo have
// separate closed4MiB profiles. Transport aggregate quotas are separate.
enum class ResponseProfile {Tracker,RssDocument,TorrentMetainfo};
struct Response {
  bool complete=false,unsupported=false;
  QByteArray bytes;
  bool failed=false;
  QByteArray location={};
  QByteArray basicRealm={};
  bool basicAuthentication=false;
};
// Owning result values only. Header limit16KiB, at most8gzip members, strict
// existing framing/CRC/truncation policy. Unknown profiles fail closed. Caller
// bounds its input/aggregate transport bytes; this function opens no endpoint.
Response decodeResponse(const QByteArray&,bool eof,
    ResponseProfile=ResponseProfile::Tracker);
} // namespace biglybtc::services::http
