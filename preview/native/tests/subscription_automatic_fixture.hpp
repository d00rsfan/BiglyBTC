// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "subscription_rss_http_fixture.hpp"
#include "saved_subscription_service.hpp"
#include "subscription_metainfo_fetch.hpp"
#include <QFileInfo>
namespace subscription_automatic_fixture {
namespace f=subscription_http_fixture;namespace w=rss_fixture;namespace s=biglybtc::search;
inline QByteArray bytes(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Cannot read actual automatic fixture");return file.readAll();}
struct Profile:f::Profile {
  explicit Profile(const std::string& url,const QJsonObject& filters={},const QSslCertificate* trust=nullptr):f::Profile(url,trust,true,filters){
    const auto data=root.filePath("data");w::require(QDir().mkpath(data),"Cannot create owned automatic data directory");
    auto config=biglybtc::storage::ConfigFile::load(*lease,"biglybt.config");w::require(bool(config),"Actual settings missing");
    w::require(bool(config->document().replaceValue("Default save path",w::b(data.toUtf8()).toStdString()))&&bool(config->document().replaceValue("Save Torrent Files","i1e"))&&bool(config->document().replaceValue("subscriptions.config.mark.lib.results.read","i0e"))&&bool(config->save(*lease)),"Cannot prepare actual automatic settings");settings=bytes(path+"/biglybt.config");
  }
  void setting(std::string_view key,std::string value){auto config=biglybtc::storage::ConfigFile::load(*lease,"biglybt.config");w::require(bool(config)&&bool(config->document().replaceValue(key,std::move(value)))&&bool(config->save(*lease)),"Cannot publish real automatic setting");settings=bytes(path+"/biglybt.config");}
  void lastScan(qint64 value){auto metadata=biglybtc::core::Document::parse(metadataRow.toStdString());w::require(bool(metadata),"Actual metadata missing");auto encoded=biglybtc::core::encodeValue(*metadata->find("history"));w::require(bool(encoded),"Actual history missing");auto history=biglybtc::core::Document::parse(*encoded);w::require(bool(history)&&bool(history->replaceValue("last_scan",integer(value).toStdString()))&&bool(metadata->replaceValue("history",history->serialize())),"Cannot retain scan fixture");metadataRow=QByteArray::fromStdString(metadata->serialize());configuration=f::dictionary({{"subs","l"+metadataRow+"e"}});w::write(path+"/subscriptions.config",configuration);}
  void result(QJsonObject json,bool read=false){w::write(resultsPath,f::dictionary({{"future_results",w::b("keep")},{"results","l"+f::dictionary({{"key",w::b("result-key")},{"read",integer(read?1:0)},{"future_row",w::b("opaque")},{"result_json",w::b(QJsonDocument(json).toJson(QJsonDocument::Compact))},{"tz",integer(1)}})+"e"}}));}
  static QByteArray integer(qint64 value){return f::integer(value);}
};
inline s::SubscriptionFetchFactory feedFactory(Profile& area,int& made){return [&area,&made](const biglybtc::storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const s::SavedSubscriptionSelector& selected,std::stop_token stop){auto owner=s::RssHttpFetch::openAutomaticSubscriptionFixture(lease,path,session,selected,area.endpoint(),stop);if(owner){++made;return std::move(*owner);}return std::unique_ptr<s::RssHttpFetch>{};};}
inline void poll(s::SavedSubscriptionService& service,const biglybtc::storage::ProfileLease& lease,std::uint64_t session,std::optional<s::SubscriptionCheckOutcome>& output){if(output){return;}auto value=service.poll(lease,session);w::require(bool(value),"Actual automatic poll failed");if(*value){output=std::move(**value);}}
} // namespace subscription_automatic_fixture
