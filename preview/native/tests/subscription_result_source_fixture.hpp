// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "subscription_rss_http_fixture.hpp"
#include "saved_subscription_result_source.hpp"
namespace subscription_result_fixture {
namespace f=subscription_http_fixture;namespace w=rss_fixture;namespace s=biglybtc::search;
struct Profile: f::Profile {
  QByteArray resultKey="result-key";
  explicit Profile(QJsonObject row,const QJsonObject& filters={},bool referer=true):f::Profile("http://feeds.example.org/feed",nullptr,false,filters){
    auto metadata=biglybtc::core::Document::parse(metadataRow.toStdString());w::require(bool(metadata),"Saved result metadata missing");auto raw=biglybtc::core::encodeValue(*metadata->find("history"));w::require(bool(raw),"Saved result history missing");auto history=biglybtc::core::Document::parse(*raw);w::require(bool(history),"Saved result history malformed");
    w::require(bool(history->replaceValue("num_unread","i1e"))&&bool(history->replaceValue("last_scan","i1000e"))&&bool(history->replaceValue("dl_with_ref",referer?"i1e":"i0e")),"Cannot save actual result policy");w::require(bool(metadata->replaceValue("history",history->serialize())),"Cannot preserve metadata");metadataRow=QByteArray::fromStdString(metadata->serialize());configuration=f::dictionary({{"subs","l"+metadataRow+"e"}});w::write(path+"/subscriptions.config",configuration);save(std::move(row));
  }
  void save(QJsonObject row,bool read=false){const auto json=QJsonDocument(row).toJson(QJsonDocument::Compact);w::write(resultsPath,f::dictionary({{"results","l"+f::dictionary({{"future",w::b("opaque")},{"key",w::b(resultKey)},{"read",f::integer(read?1:0)},{"result_json",w::b(json)},{"tz",f::integer(1)}})+"e"}}));}
  s::SavedSubscriptionResultSelector resultSelector()const{return {selector(),resultKey.toStdString()};}
};
} // namespace subscription_result_fixture
