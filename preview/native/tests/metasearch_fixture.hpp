// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "rss_http_fetch_fixture.hpp"
#include "saved_metasearch_service.hpp"
#include <QJsonObject>
#include <QJsonDocument>
#include <map>
namespace metasearch_fixture {
namespace s=biglybtc::search;namespace w=rss_fixture;
inline QByteArray dictionary(std::initializer_list<std::pair<QByteArray,QByteArray>> input){std::map<QByteArray,QByteArray> sorted(input.begin(),input.end());QByteArray out="d";for(const auto& [key,value]:sorted){out+=w::b(key)+value;}return out+'e';}
inline QByteArray integer(qint64 value){return 'i'+QByteArray::number(value)+'e';}
inline QByteArray mapping(const char* name,int field){return dictionary({{"name",w::b(name)},{"field",integer(field)}});}
inline QByteArray engine(const std::string& url,int id=7,const QByteArray& uid="owned-json",int type=2){
  const auto maps=type==4?QByteArray("le"):"l"+mapping("title",1)+mapping("size",3)+mapping("seeds",5)+mapping("peers",4)+mapping("download",102)+"e";
  return dictionary({{"type",integer(type)},{"id",integer(id)},{"uid",w::b(uid)},{"name",w::b("Owned search")},{"selected",integer(2)},{"source",integer(2)},{"az_version",integer(5)},{"version",integer(1)},{"future",w::b("retain-opaque")},{"update_url",w::b("https://updates.example.org/template")},{"web.maps",maps},{"web.search_url_format",w::b(QByteArray::fromStdString(url))},{"json.path",w::b("rows")}});
}
inline QByteArray catalog(const QByteArray& rows){return dictionary({{"engines","l"+rows+"e"},{"future_catalog",w::b("keep")}});}
inline QByteArray bytes(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Cannot read private metasearch file");return file.readAll();}
struct Profile:w::Profile {
  QByteArray primary;
  explicit Profile(const std::string& url,const QSslCertificate* trust=nullptr):w::Profile(url,trust){setRows(engine(url));}
  static s::SavedMetasearchSelector selector(std::size_t index=0,int id=7,std::string uid="owned-json"){return {index,id,std::move(uid)};}
  void setRows(const QByteArray& rows){primary=catalog(rows);w::write(path+"/metasearch.config",primary);}
};
inline s::MetasearchFetchFactory factory(Profile& area,int& made){return [&area,&made](const biglybtc::storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const s::SavedMetasearchSelector& selected,const std::string& query,std::stop_token stop){auto owner=s::MetasearchHttpFetch::openFixture(lease,path,session,selected,query,area.endpoint(),stop);if(owner){++made;return std::move(*owner);}return std::unique_ptr<s::MetasearchHttpFetch>{};};}
inline void pump(s::SavedMetasearchService& service,const biglybtc::storage::ProfileLease& lease,std::uint64_t session,std::vector<s::MetasearchSearchUpdate>& updates){if(!updates.empty()&&updates.back().done){return;}auto value=service.poll(lease,session);w::require(bool(value),"Actual metasearch service poll failed");if(*value){updates.push_back(std::move(**value));}}
} // namespace metasearch_fixture
