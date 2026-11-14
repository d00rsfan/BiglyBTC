// SPDX-License-Identifier: GPL-2.0-or-later
#include "activity_profile.hpp"
#include <QCryptographicHash>
#include <QByteArray>
#include <algorithm>
#include <set>
namespace biglybtc::app {
namespace {
using storage::Error;using storage::ErrorCode;using Node=core::Node;
constexpr std::size_t MaximumEntries=10000;
constexpr auto Callback="com.biglybt.plugin.net.buddy.BuddyPluginBeta$ActivityCallback";
struct Failure {Error error;};
void need(bool condition,ErrorCode code,const char* message){if(!condition){throw Failure{{code,message}};}}
template<class T>T take(std::expected<T,Error> value){if(!value){throw Failure{value.error()};}return std::move(*value);}
template<class F>auto guarded(F&& f)->decltype(f()){try{return f();}catch(const Failure& e){return std::unexpected(e.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Notification profile operation failed; existing file retained"});}}
std::string b(std::string_view v){return std::to_string(v.size())+":"+std::string(v);}
std::string number(std::int64_t v){return "i"+std::to_string(v)+"e";}
std::string encode(const Node& n){auto v=core::encodeValue(n,storage::compatibleConfigLimits());need(bool(v),ErrorCode::InvalidFormat,"Notification field exceeds its bound");return std::move(*v);}
core::Document document(std::string s){auto d=core::Document::parse(std::move(s),storage::compatibleConfigLimits());need(bool(d),ErrorCode::InvalidFormat,"Invalid notification value");return std::move(*d);}
std::string sortedMap(const Node& root){std::map<std::string,std::string> fields;for(std::size_t i=0;i+1<root.children.size();i+=2){fields.emplace(std::string(root.children[i].bytes),encode(root.children[i+1]));}std::string out="d";for(const auto& [key,value]:fields){out+=b(key)+value;}return out+'e';}
void replace(core::Document& d,std::string_view key,std::string value){need(bool(d.replaceValue(key,std::move(value))),ErrorCode::InvalidFormat,"Cannot retain notification field");}
std::string text(const Node* n){return n&&n->type==Node::Type::Bytes?std::string(n->bytes):std::string{};}
std::int64_t integer(const Node* n,std::int64_t fallback=0){return n&&n->type==Node::Type::Integer?n->integer:fallback;}
const Node* field(const Node& n,std::string_view key){if(n.type!=Node::Type::Dictionary){return nullptr;}for(std::size_t i=0;i+1<n.children.size();i+=2){if(n.children[i].bytes==key){return &n.children[i+1];}}return nullptr;}
void unique(const Node& n){need(n.type==Node::Type::Dictionary&&n.children.size()%2==0,ErrorCode::InvalidFormat,"Invalid notification map");std::set<std::string_view> keys;for(std::size_t i=0;i<n.children.size();i+=2){need(n.children[i].type==Node::Type::Bytes&&keys.insert(n.children[i].bytes).second,ErrorCode::InvalidFormat,"Ambiguous notification map");}}
std::string stamp(const Node& n){const auto bytes=encode(n);return QCryptographicHash::hash(QByteArray::fromStdString(bytes),QCryptographicHash::Sha256).toStdString();}
std::string list(const std::vector<std::string>& entries){std::string out="l";for(const auto& e:entries){out+=e;}return out+'e';}
std::vector<std::string> values(const core::Document& d,std::string_view key){std::vector<std::string> out;if(const auto* rows=d.find(key)){need(rows->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid notification list");need(rows->children.size()<=MaximumEntries,ErrorCode::Unsupported,"Notification list exceeds the supported bound");for(const auto& row:rows->children){out.push_back(encode(row));}}return out;}
std::int64_t cutoff(std::int64_t now){return now-ActivityProfile::MaximumAge;}
void validate(const core::Document& d){unique(d.root());const auto version=integer(d.find("version"),d.root().children.empty()?2:0);need(version==2,ErrorCode::Unsupported,"Notification format version is unsupported; existing data retained");}
ActivityEntry entry(const Node& row,std::int64_t now){
 unique(row);ActivityEntry e;e.source={text(field(row,"id")),stamp(row)};e.type=text(field(row,"typeID"));e.icon=text(field(row,"icon"));e.text=text(field(row,"text"));e.timestamp=integer(field(row,"timestamp"));if(!e.timestamp){e.timestamp=now;}e.readOn=integer(field(row,"readOn"),now);e.viewed=integer(field(row,"viewed"))!=0;e.callbackClass=text(field(row,"cb_class"));
 if(const auto* actions=field(row,"actions");actions&&actions->type==Node::Type::List){for(const auto& action:actions->children){if(action.type==Node::Type::Bytes){e.actions.emplace_back(action.bytes);}}}
 if(const auto* data=field(row,"cb_data")){need(data->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid notification callback data");unique(*data);for(std::size_t i=0;i<data->children.size();i+=2){if(data->children[i+1].type==Node::Type::Bytes){e.callbackData.emplace(std::string(data->children[i].bytes),std::string(data->children[i+1].bytes));}}}return e;
}
ActivitySnapshot snapshot(const core::Document& d,std::int64_t now,bool recovered){
 validate(d);ActivitySnapshot out;out.recovered=recovered;const auto* rows=d.find("entries");if(!rows){return out;}need(rows->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid notification entries");for(const auto& row:rows->children){if(out.entries.size()>=MaximumEntries){out.truncated=true;break;}if(row.type!=Node::Type::Dictionary){continue;}ActivityEntry e;try{e=entry(row,now);}catch(const Failure&){++out.malformed;continue;}if(e.source.id.empty()||e.type=="Header"||e.timestamp<=cutoff(now)){continue;}out.entries.push_back(std::move(e));}std::stable_sort(out.entries.begin(),out.entries.end(),[](const auto& a,const auto& b){return a.timestamp>b.timestamp;});return out;
}
ActivitySnapshot captured(storage::ConfigFile& f,std::int64_t now){return snapshot(f.document(),now,f.recoverySource()!=storage::RecoverySource::Primary);}
std::string empty(){auto d=document("de");replace(d,"version","i2e");replace(d,"LastChecks","de");replace(d,"entries","le");replace(d,"removed-entries","le");return d.serialize();}
void writable(storage::ConfigFile& f){validate(f.document());need(f.recoverySource()==storage::RecoverySource::Primary,ErrorCode::Conflict,"Resolve notification profile recovery before changing it");}
std::string chat(const ChatActivityPost& p,const Node* old){
 auto d=document(old?encode(*old):"de");if(old){const auto e=entry(*old,p.timestamp);need(e.publicChat()&&e.callbackData.at("key")==p.key,ErrorCode::Conflict,"Notification ID belongs to another source");}
 for(const auto* key:{"assetHash","assetImageURL","imageBytes","torrent","torrent-name","playable"}){if(d.find(key)){need(bool(d.eraseValue(key)),ErrorCode::InvalidFormat,"Cannot replace notification content");}}
 replace(d,"id",b("Public: "+p.key));replace(d,"text",b(p.text));replace(d,"timestamp",number(p.timestamp));replace(d,"typeID",b("LOCAL_NEWS_ITEM"));replace(d,"icon",b("image.sidebar.chat-overview"));replace(d,"showThumb","i1e");replace(d,"readOn","i0e");replace(d,"viewed","i0e");replace(d,"actions","l"+b(p.action)+"e");replace(d,"cb_class",b(Callback));auto cb=document("de");replace(cb,"allowReAdd",b("true"));replace(cb,"net",b("Public"));replace(cb,"key",b(p.key));replace(d,"cb_data",sortedMap(cb.root()));return sortedMap(d.root());
}
void save(storage::ConfigFile& f,const storage::ProfileLease& lease){auto result=f.save(lease);if(!result){throw Failure{result.error()};}}
struct Selected {std::size_t index;core::Document value;};
std::vector<Selected> selected(const std::vector<std::string>& entries,std::span<const ActivityReference> refs){
 need(refs.size()<=MaximumEntries,ErrorCode::Unsupported,"Too many notification selections");std::vector<Selected> out;std::set<std::string> ids;
 for(const auto& ref:refs){need(!ref.id.empty()&&ref.stamp.size()==32&&ids.insert(ref.id).second,ErrorCode::Conflict,"Invalid or repeated notification selection");std::optional<Selected> match;for(std::size_t i=0;i<entries.size();++i){auto d=document(entries[i]);if(d.root().type!=Node::Type::Dictionary||text(d.find("id"))!=ref.id){continue;}need(!match,ErrorCode::Conflict,"Notification ID is ambiguous");need(stamp(d.root())==ref.stamp,ErrorCode::Conflict,"Notification changed since it was selected");match=Selected{i,std::move(d)};}need(match.has_value(),ErrorCode::Conflict,"Notification no longer exists");out.push_back(std::move(*match));}return out;
}
std::expected<ActivitySnapshot,Error> change(const storage::ProfileLease& lease,std::span<const ActivityReference> refs,std::int64_t now,int mode,bool read){return guarded([&]()->std::expected<ActivitySnapshot,Error>{
 need(now>0,ErrorCode::InvalidFormat,"Invalid notification clock");auto access=take(lease.sourceAccess(true));auto f=take(storage::ConfigFile::load(lease,ActivityProfile::Leaf));writable(f);auto entries=values(f.document(),"entries");auto changes=selected(entries,refs);auto removed=values(f.document(),"removed-entries");for(auto& value:changes){const auto parsed=entry(value.value.root(),now);if(mode==2){const auto& root=value.value.root();const auto* data=field(root,"cb_data");const bool readd=data&&data->type==Node::Type::Dictionary&&field(*data,"allowReAdd");if(!readd&&parsed.timestamp>cutoff(now)&&text(field(root,"typeID"))!="Header"){auto dead=document("de");replace(dead,"id",b(text(field(root,"id"))));replace(dead,"timestamp",number(parsed.timestamp));removed.push_back(sortedMap(dead.root()));}entries[value.index].clear();}
  else{if(!integer(value.value.find("timestamp"))){replace(value.value,"timestamp",number(parsed.timestamp));}replace(value.value,mode==0?"readOn":"viewed",mode==0?number(read?now:-now):"i1e");entries[value.index]=value.value.serialize();}}
 if(!changes.empty()){std::erase(entries,std::string{});replace(f.document(),"entries",list(entries));if(mode==2){need(removed.size()<=MaximumEntries,ErrorCode::Unsupported,"Removed notification list exceeds its bound");replace(f.document(),"removed-entries",list(removed));}save(f,lease);}return captured(f,now);
 });}
}
bool ActivityEntry::publicChat()const{return type=="LOCAL_NEWS_ITEM"&&callbackClass==Callback&&callbackData.contains("net")&&callbackData.at("net")=="Public"&&callbackData.contains("key")&&!callbackData.at("key").empty()&&source.id=="Public: "+callbackData.at("key");}
std::expected<ActivitySnapshot,Error> ActivityProfile::read(const storage::ProfileLease& lease,std::int64_t now){return guarded([&]()->std::expected<ActivitySnapshot,Error>{need(now>0,ErrorCode::InvalidFormat,"Invalid notification clock");auto access=take(lease.sourceAccess());auto f=storage::ConfigFile::load(lease,Leaf);if(!f){if(f.error().code==ErrorCode::Missing){return ActivitySnapshot{};}return std::unexpected(f.error());}return captured(*f,now);});}
std::expected<ActivitySnapshot,Error> ActivityProfile::post(const storage::ProfileLease& lease,std::span<const ChatActivityPost> posts,std::int64_t now){return guarded([&]()->std::expected<ActivitySnapshot,Error>{
 need(now>0&&posts.size()<=256,ErrorCode::Unsupported,"Invalid notification batch");for(const auto& p:posts){need(!p.privateChat&&!p.key.empty()&&p.key.size()<=4096&&p.text.size()<=64*1024&&!p.action.empty()&&p.action.size()<=256&&p.timestamp>0,ErrorCode::Unsupported,"Notification post is not a supported Public chat event");}auto access=take(lease.sourceAccess(true));auto loaded=storage::ConfigFile::load(lease,Leaf);if(!loaded){if(loaded.error().code!=ErrorCode::Missing){return std::unexpected(loaded.error());}if(posts.empty()||std::ranges::all_of(posts,[&](const auto& p){return p.timestamp<cutoff(now);})){return ActivitySnapshot{};}loaded=storage::ConfigFile::createMissing(lease,Leaf,empty());if(!loaded){return std::unexpected(loaded.error());}}
 auto& f=*loaded;writable(f);auto entries=values(f.document(),"entries"),removed=values(f.document(),"removed-entries");bool changed=false;
 for(const auto& p:posts){if(p.timestamp<cutoff(now)){continue;}const auto id="Public: "+p.key;bool deleted=false;for(const auto& raw:removed){auto d=document(raw);if(d.root().type==Node::Type::Dictionary&&text(d.find("id"))==id&&integer(d.find("timestamp"))>cutoff(now)){deleted=true;break;}}if(deleted){continue;}std::optional<std::size_t> index;
  for(std::size_t i=0;i<entries.size();++i){auto d=document(entries[i]);if(d.root().type==Node::Type::Dictionary&&text(d.find("id"))==id){need(!index,ErrorCode::Conflict,"Notification ID is ambiguous");index=i;}}
  if(index){auto old=document(entries[*index]);if(integer(old.find("timestamp"))>=p.timestamp){continue;}entries[*index]=chat(p,&old.root());changed=true;}
  else{need(entries.size()<MaximumEntries,ErrorCode::Unsupported,"Notification list exceeds its bound");entries.push_back(chat(p,nullptr));changed=true;}
 }
 if(changed){replace(f.document(),"entries",list(entries));if(!f.document().find("version")){replace(f.document(),"version","i2e");}save(f,lease);}return captured(f,now);
 });}
std::expected<ActivitySnapshot,Error> ActivityProfile::readState(const storage::ProfileLease& lease,std::span<const ActivityReference> refs,bool read,std::int64_t now){return change(lease,refs,now,0,read);}
std::expected<ActivitySnapshot,Error> ActivityProfile::viewed(const storage::ProfileLease& lease,std::span<const ActivityReference> refs,std::int64_t now){return change(lease,refs,now,1,false);}
std::expected<ActivitySnapshot,Error> ActivityProfile::remove(const storage::ProfileLease& lease,std::span<const ActivityReference> refs,std::int64_t now){return change(lease,refs,now,2,false);}
}
