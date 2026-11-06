// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_subscription_service.hpp"
#include <biglybtc/bencode.hpp>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {
namespace c=biglybtc::core;
namespace s=biglybtc::search;
namespace st=biglybtc::storage;
using Node=c::Node;
constexpr std::uint64_t Session=421;
constexpr std::int64_t DayMillis=86400000;
constexpr c::Limits Limits{1024*1024,1024*1024,100000,64};
const QString Keep=QString::fromUtf8("Keep \xCE\xBC");
const QString Delete=QString::fromUtf8("Delete \xCE\xA9");
void require(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
template<class T,class E>T take(std::expected<T,E> value,const char* message){require(bool(value),message);return std::move(*value);}
std::string text(const Node* node){require(node&&node->type==Node::Type::Bytes,"Expected actual encoded bytes");return std::string(node->bytes);}
QString qpath(const std::filesystem::path& path){return QString::fromStdString(path.string());}
std::string hex(std::string_view value){return QByteArray(value.data(),static_cast<qsizetype>(value.size())).toHex().toUpper().toStdString();}
void privateDirectory(const std::filesystem::path& path){
  struct stat info{};require(::lstat(path.c_str(),&info)==0&&S_ISDIR(info.st_mode)&&info.st_uid==::geteuid()&&(info.st_mode&0077)==0,"Private fixture directory required");
}
QByteArray bytes(const std::filesystem::path& path){
  struct stat info{};require(::lstat(path.c_str(),&info)==0&&S_ISREG(info.st_mode)&&info.st_uid==::geteuid()&&info.st_nlink==1&&info.st_size>=0&&info.st_size<=static_cast<off_t>(Limits.inputBytes),"Bounded regular fixture file required");
  QFile file(qpath(path));require(file.open(QIODevice::ReadOnly),"Cannot read private fixture file");
  auto result=file.readAll();require(result.size()==info.st_size,"Fixture changed during bounded read");return result;
}
c::Document document(const std::filesystem::path& path){return take(c::Document::parse(bytes(path).toStdString(),Limits),"Invalid bounded fixture bencode");}
std::map<std::string_view,const Node*> fields(const Node& node){
  require(node.type==Node::Type::Dictionary&&node.children.size()%2==0,"Expected actual dictionary");std::map<std::string_view,const Node*> result;
  for(std::size_t i=0;i<node.children.size();i+=2){require(node.children[i].type==Node::Type::Bytes,"Non-byte dictionary key");require(result.emplace(node.children[i].bytes,&node.children[i+1]).second,"Duplicate dictionary key");}return result;
}
bool equal(const Node& left,const Node& right){
  if(left.type!=right.type){return false;}
  if(left.type==Node::Type::Integer){return left.integer==right.integer;}if(left.type==Node::Type::Bytes){return left.bytes==right.bytes;}
  if(left.type==Node::Type::List){if(left.children.size()!=right.children.size()){return false;}for(std::size_t i=0;i<left.children.size();++i){if(!equal(left.children[i],right.children[i])){return false;}}return true;}
  const auto a=fields(left),b=fields(right);if(a.size()!=b.size()){return false;}for(const auto& [key,value]:a){const auto at=b.find(key);if(at==b.end()||!equal(*value,*at->second)){return false;}}return true;
}
Node* mutableField(Node& node,std::string_view name){
  require(node.type==Node::Type::Dictionary,"Expected mutable comparison dictionary");for(std::size_t i=0;i+1<node.children.size();i+=2){if(node.children[i].bytes==name){return &node.children[i+1];}}return nullptr;
}
void integer(Node& node,std::string_view name,std::int64_t value){
  if(auto* old=mutableField(node,name)){require(old->type==Node::Type::Integer,"Unexpected comparison field type");old->integer=value;return;}
  Node key(Node::Type::Bytes);key.bytes=name;Node numberNode(Node::Type::Integer);numberNode.integer=value;node.children.push_back(std::move(key));node.children.push_back(std::move(numberNode));
}
void remove(Node& node,std::string_view name){
  require(node.type==Node::Type::Dictionary,"Expected comparison dictionary");for(std::size_t i=0;i+1<node.children.size();i+=2){if(node.children[i].bytes==name){node.children.erase(node.children.begin()+static_cast<std::ptrdiff_t>(i),node.children.begin()+static_cast<std::ptrdiff_t>(i+2));return;}}
}
QJsonObject json(std::string_view encoded){
  QJsonParseError error;auto parsed=QJsonDocument::fromJson(QByteArray(encoded.data(),static_cast<qsizetype>(encoded.size())),&error);
  require(error.error==QJsonParseError::NoError&&parsed.isObject(),"Invalid actual result JSON");return parsed.object();
}
QString resultName(const Node& row){return json(text(row.find("result_json"))).value("n").toString();}
const Node& baseline(const c::Document& state,std::string_view sid){
  const auto* rows=state.find("rows");require(rows&&rows->type==Node::Type::List&&rows->children.size()==2,"Expected two immutable Original rows");
  const Node* found=nullptr;for(const auto& row:rows->children){if(text(row.find("sid"))==sid){require(!found,"Ambiguous Original SID");found=&row;}}require(found,"Actual saved SID absent from Original state");return *found;
}
const s::SubscriptionResultValue& named(const s::SubscriptionResults& rows,const QString& name){
  const s::SubscriptionResultValue* found=nullptr;for(const auto& row:rows.rows){if(!row.deleted&&json(row.json).value("n").toString()==name){require(!found,"Duplicate actual named result");found=&row;}}
  require(found&&found->key.size()==10,"Actual named raw result key missing");return *found;
}
std::string baselineKey(const Node& original,const QString& name){
  const auto* list=original.find("results");require(list&&list->type==Node::Type::List&&list->children.size()==3,"Expected three immutable Original result rows");
  const Node* found=nullptr;for(const auto& row:list->children){if(row.find("result_json")&&resultName(row)==name){require(!found,"Duplicate Original result name");found=&row;}}
  require(found,"Original result name missing");auto key=text(found->find("key"));require(key.size()==10,"Invalid Original result key");return key;
}
Node expectedResults(const Node& original,std::string_view keep,std::string_view deleted,std::int64_t day,bool unread){
  Node result=original;auto* list=mutableField(result,"results");require(list&&list->type==Node::Type::List,"Expected results comparison list");bool sawKeep=false,sawDelete=false;
  for(auto& row:list->children){const auto key=text(row.find("key"));if(key==keep){integer(row,"read",unread?0:1);sawKeep=true;}if(key==deleted){integer(row,"deleted",1);integer(row,"dls",day);remove(row,"result_json");remove(row,"tz");sawDelete=true;}}
  require(sawKeep&&sawDelete,"Missing comparison result identity");return result;
}
Node expectedConfiguration(const Node& original,bool unread){
  Node result=original;auto* rows=mutableField(result,"subs");require(rows&&rows->type==Node::Type::List&&rows->children.size()==2,"Expected comparison subscriptions");
  for(auto& row:rows->children){auto* history=mutableField(row,"history");require(history,"Missing original history");integer(*history,"num_read",unread?0:1);integer(*history,"num_unread",unread?1:0);}return result;
}
c::Document currentConfiguration(const st::ProfileLease& lease){
  auto access=take(lease.sourceAccess(),"Cannot observe actual configuration");require(access.observationsAllowed(),"Nested actual source observation");
  auto config=take(st::ConfigFile::load(lease,"subscriptions.config",Limits),"Cannot read actual configuration");
  require(config.recoverySource()==st::RecoverySource::Primary,"Healthy primary configuration required");
  return take(c::Document::parse(config.document().originalBytes(),Limits),"Invalid actual configuration");
}
void healthy(const std::filesystem::path& profile,const std::string& stem){
  for(const auto& name:{std::string(".subscriptions-native-write"),std::string("subscriptions.config"),std::string("subs/")+stem+".results"}){
    for(const auto* suffix:{".saving"}){struct stat info{};const auto path=profile/(name+suffix);require(::lstat(path.c_str(),&info)!=0&&errno==ENOENT,"Unexpected unfinished saving file");}
  }
  for(const auto* suffix:{"",".bak"}){struct stat info{};const auto path=profile/(std::string(".subscriptions-native-write")+suffix);require(::lstat(path.c_str(),&info)!=0&&errno==ENOENT,"Unexpected pending store intent");}
  // Genuine healthy .bak files are retained; no recovery or cleanup is faked.
}
QJsonObject loadProof(const std::filesystem::path& root){
  auto proof=json(bytes(root/"native-delete-day-bounds.json").toStdString());
  require(proof.value("schema")=="BIGLYBTC_SUBSCRIPTION_NATIVE_DELETE_DAY_BOUNDS_V1","Wrong Native day proof schema");return proof;
}
QJsonObject proofRow(const QJsonObject& proof,std::string_view sid){
  const auto values=proof.value("rows").toArray();require(values.size()==2,"Expected two Native day bounds");QJsonObject found;int matches=0;
  for(const auto& value:values){require(value.isObject(),"Invalid Native day bound row");const auto row=value.toObject();if(row.value("sid_hex").toString().toStdString()==hex(sid)){found=row;++matches;}}
  require(matches==1,"Actual SID missing from Native day proof");return found;
}
std::int64_t proofInteger(const QJsonObject& object,const char* name){
  const auto value=object.value(QLatin1String(name));require(value.isDouble(),"Missing numeric Native clock evidence");const auto number=value.toInteger(-1);require(number>=0&&static_cast<double>(number)==value.toDouble(),"Invalid Native clock evidence");return number;
}
std::int64_t boundedDay(const QJsonObject& row){
  const auto before=proofInteger(row,"before_millis"),after=proofInteger(row,"after_millis"),day=proofInteger(row,"observed_dls");
  require(after>=before&&after-before<=60000&&before/DayMillis==proofInteger(row,"before_utc_day")&&after/DayMillis==proofInteger(row,"after_utc_day")&&day>=before/DayMillis&&day<=after/DayMillis,"Deletion day outside actual Native API call bounds");return day;
}
void saveProof(const std::filesystem::path& root,const QJsonObject& value){
  QFile output(qpath(root/"native-delete-day-bounds.json"));require(output.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Native day proof must be new");
  require(output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner),"Cannot restrict Native day proof");const auto wire=QJsonDocument(value).toJson(QJsonDocument::Indented);
  require(output.write(wire)==wire.size()&&output.flush(),"Cannot retain Native day proof");
}
void run(const std::filesystem::path& root,bool mutate){
  privateDirectory(root);require(std::filesystem::canonical(root)==root,"Canonical private root required");const auto profile=root/"profile";
  privateDirectory(profile);require(std::filesystem::canonical(profile)==profile,"Canonical private profile required");privateDirectory(profile/"subs");
  const auto state=document(root/"oracle-state.bencode");require(state.find("configuration"),"Immutable Original configuration missing");
  const auto* initialRows=state.find("rows");require(initialRows&&initialRows->type==Node::Type::List&&initialRows->children.size()==2,"Expected two Original fixture rows");
  for(const auto& row:initialRows->children){const auto sid=text(row.find("sid"));require(sid.size()==10,"Invalid Original SID");healthy(profile,hex(sid));}
  std::optional<st::ProfileLease> lease;lease.emplace(take(st::ProfileLease::acquire(profile),"Cannot acquire actual private profile lease"));
  auto service=take(s::SavedSubscriptionService::open(*lease,profile,Session,mutate?s::SubscriptionServiceMode::Normal:s::SubscriptionServiceMode::ReadOnlyPreview),"Cannot open actual subscription service");
  require(!service->active(),"Codec adapter must not own an HTTP operation");auto catalog=take(service->catalog(*lease,Session),"Cannot read actual subscription catalog");require(catalog.size()==2,"Expected two actual subscriptions");
  QJsonObject proof;if(!mutate){proof=loadProof(root);}QJsonArray measured;const auto started=QDateTime::currentMSecsSinceEpoch();
  if(mutate){require(!QFile::exists(qpath(root/"native-delete-day-bounds.json")),"Native mutation proof already exists");require(equal(currentConfiguration(*lease).root(),*state.find("configuration")),"Unexpected Original input configuration");}
  for(const auto& row:catalog){
    require(!row.issue&&row.subscribed&&!row.autoDownload&&!row.anonymous,"Unsupported actual subscription row");const auto& original=baseline(state,row.selected.shortId);const auto stem=hex(row.selected.shortId);
    require(text(original.find("stem"))==stem,"Unexpected actual short-ID filename");healthy(profile,stem);
    const auto body=bytes(profile/"subs"/(stem+".biglybt"));require(QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex().toStdString()==text(original.find("body_sha256")),"Signed body changed");
    auto values=take(service->results(*lease,Session,row.selected),"Cannot read actual result store");require(values.source&&values.rows.size()==3,"Expected three actual result rows");
    const auto* originalResults=original.find("results");require(originalResults,"Immutable Original results missing");
    std::string keep,deleted;std::int64_t day=0;
    if(mutate){
      require(equal(values.source->root(),*originalResults),"Unexpected Original result input");keep=named(values,Keep).key;deleted=named(values,Delete).key;
      require(keep==baselineKey(*originalResults,Keep)&&deleted==baselineKey(*originalResults,Delete),"Actual result identity differs from Original");
      require(bool(service->changeResultState(*lease,Session,row.selected,{keep},s::SubscriptionResultAction::MarkRead)),"Actual MarkRead failed");
      const auto before=QDateTime::currentMSecsSinceEpoch();require(bool(service->changeResultState(*lease,Session,row.selected,{deleted},s::SubscriptionResultAction::Delete)),"Actual Delete failed");const auto after=QDateTime::currentMSecsSinceEpoch();
      values=take(service->results(*lease,Session,row.selected),"Cannot reread actual mutation");const auto at=std::ranges::find(values.rows,deleted,&s::SubscriptionResultValue::key);require(at!=values.rows.end()&&at->deleted,"Actual deletion tombstone missing");day=at->deletedLastSeenDay;
      QJsonObject bound{{"sid_hex",QString::fromStdString(stem)},{"keep_key_hex",QString::fromStdString(hex(keep))},{"delete_key_hex",QString::fromStdString(hex(deleted))},
        {"before_millis",before},{"after_millis",after},{"before_utc_day",before/DayMillis},{"after_utc_day",after/DayMillis},{"observed_dls",qint64{day}}};
      require(boundedDay(bound)==day,"Actual deletion day not bound");measured.append(bound);
    }else{
      const auto bound=proofRow(proof,row.selected.shortId);day=boundedDay(bound);keep=baselineKey(*originalResults,Keep);deleted=baselineKey(*originalResults,Delete);
      require(bound.value("keep_key_hex").toString().toStdString()==hex(keep)&&bound.value("delete_key_hex").toString().toStdString()==hex(deleted),"Native day proof changed result identity");
    }
    require(equal(values.source->root(),expectedResults(*originalResults,keep,deleted,day,!mutate)),"Result flags/tombstones or opaque values changed beyond contract");
    require(bytes(profile/"subs"/(stem+".biglybt"))==body,"Body mutated by state operation");healthy(profile,stem);require(!service->active(),"State API created an HTTP operation");
  }
  require(equal(currentConfiguration(*lease).root(),expectedConfiguration(*state.find("configuration"),!mutate)),"History counters or opaque configuration values changed beyond contract");
  require(bool(service->close()),"Actual service close failed");require(!service->active(),"Closed service retained HTTP work");lease.reset();
  auto reacquired=take(st::ProfileLease::acquire(profile),"Closed still-alive service retained the actual profile lock");
  (void)reacquired;
  if(mutate){saveProof(root,QJsonObject{{"schema","BIGLYBTC_SUBSCRIPTION_NATIVE_DELETE_DAY_BOUNDS_V1"},{"before_millis",started},{"after_millis",QDateTime::currentMSecsSinceEpoch()},
      {"rows",measured},{"keys_from_actual_public_results",true},{"check_poll_or_factory_invoked",false},{"body_or_private_key_rebuilt",false}});}
}
} // namespace
int main(int argc,char** argv){
  try{
    require(argc==3,"Expected one explicit oracle mode and private fixture root");const std::string_view mode=argv[1];
    const bool mutate=mode=="--oracle-mutate-mark-read-delete";require(mutate||mode=="--oracle-verify-original-return","Unknown codec adapter mode");
    run(std::filesystem::path(argv[2]),mutate);
    std::cout<<(mutate?"PASS_NATIVE_SUBSCRIPTION_ORACLE_MARK_READ_DELETE":"PASS_NATIVE_SUBSCRIPTION_ORACLE_ORIGINAL_RETURN")<<'\n';return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL_NATIVE_SUBSCRIPTION_ORACLE: "<<error.what()<<'\n';return 1;}
}
