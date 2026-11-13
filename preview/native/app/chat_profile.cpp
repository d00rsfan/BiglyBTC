// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_profile.hpp"
#include "file_io.hpp"
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <cerrno>
#include <set>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::app {
namespace {
using storage::Error;using storage::ErrorCode;using Node=core::Node;using storage::detail::Descriptor;
struct Failure{Error error;};
void need(bool ok,ErrorCode c,const char* m){if(!ok){throw Failure{{c,m}};}}
template<class T>T take(std::expected<T,Error> v){if(!v){throw Failure{v.error()};}return std::move(*v);}
void done(std::expected<void,Error> v){if(!v){throw Failure{v.error()};}}
template<class F>auto guarded(F&& f)->decltype(f()){try{return f();}catch(const Failure& e){return std::unexpected(e.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Chat profile operation failed; existing data retained"});}}
std::string b(std::string_view v){return std::to_string(v.size())+":"+std::string(v);}
std::string n(std::int64_t v){return "i"+std::to_string(v)+"e";}
std::string encoded(const Node& v){auto s=core::encodeValue(v,storage::compatibleConfigLimits());need(bool(s),ErrorCode::InvalidFormat,"Chat field exceeds its bound");return std::move(*s);}
core::Document dict(const Node* node){auto result=core::Document::parse(node?encoded(*node):"de",storage::compatibleConfigLimits());need(bool(result)&&result->root().type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid original chat settings map");return std::move(*result);}
void unique(const Node& node){need(node.type==Node::Type::Dictionary&&node.children.size()%2==0,ErrorCode::InvalidFormat,"Invalid chat map");std::set<std::string_view> keys;for(std::size_t i=0;i<node.children.size();i+=2){need(node.children[i].type==Node::Type::Bytes&&keys.insert(node.children[i].bytes).second,ErrorCode::InvalidFormat,"Ambiguous chat map");}}
std::string text(const Node* n,std::string fallback={}){return n&&n->type==Node::Type::Bytes?std::string(n->bytes):fallback;}
bool boolean(const Node* n,bool fallback=false){return n&&n->type==Node::Type::Integer?static_cast<std::uint32_t>(n->integer)!=0:fallback;}
std::string base32(std::string_view data){constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned buffer=0,bits=0;for(const unsigned char c:data){buffer=(buffer<<8)|c;bits+=8;while(bits>=5){bits-=5;out+=alphabet[(buffer>>bits)&31];}}if(bits){out+=alphabet[(buffer<<(5-bits))&31];}return out;}
std::optional<std::string> unbase32(std::string_view data){std::string out;unsigned buffer=0,bits=0;for(char c:data){const auto p=std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567").find(c);if(p==std::string_view::npos){return {};}buffer=(buffer<<5)|static_cast<unsigned>(p);bits+=5;if(bits>=8){bits-=8;out+=static_cast<char>(buffer>>bits);}}if(bits&&(buffer&((1U<<bits)-1U))){return {};}return out;}
std::string hex(std::string_view data){constexpr char chars[]="0123456789ABCDEF";std::string out;out.reserve(data.size()*2);for(unsigned char c:data){out+=chars[c>>4];out+=chars[c&15];}return out;}
ChatRoomSettings settings(const core::Document& cfg,std::string key){
 ChatRoomSettings s;s.key=std::move(key);s.nickname=text(cfg.find("azbuddy.chat.shared_nick"));
 const auto* options=cfg.find("azbuddy.dchat.optsmap");const auto* value=options?options->find(ChatProfile::optionKey(s.key)):nullptr;
 std::string ownNick;
 if(options){unique(*options);}if(value){unique(*value);if(const auto* marker=value->find("lmi")){need(marker->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid original chat read marker");s.lastMessageInfo=std::string(marker->bytes);}}
 if(value){s.favourite=boolean(value->find("fave"));s.saveHistory=boolean(value->find("save"));s.sharedNickname=boolean(value->find("sn"),true);ownNick=text(value->find("nick"));}
 const auto prefix="azbuddy.chat.Public: "+s.key;
 if(const auto* legacy=cfg.find(prefix+".shared")){s.sharedNickname=boolean(legacy,true);}
 if(const auto* legacy=cfg.find(prefix+".nick");legacy&&!text(legacy).empty()){ownNick=text(legacy);}
 for(auto [map,flag]:{std::pair{"azbuddy.dchat.favemap",&s.favourite},{"azbuddy.dchat.savemsgmap",&s.saveHistory}}){
  if(const auto* old=cfg.find(map)){const auto* item=old->find("Public:"+s.key);if(item&&item->type==Node::Type::Integer&&item->integer==1){*flag=true;}}
 }
 if(!s.sharedNickname){s.nickname=std::move(ownNick);}
 const auto policy=services::msgsync::roomPolicy(s.key);
 s.readOnly=!policy||policy->readOnly;
 return s;
}
Descriptor directory(const storage::ProfileLease& lease,bool create){
 Descriptor current(::fcntl(lease.directoryHandle(),F_DUPFD_CLOEXEC,3));need(current.get()>=0,ErrorCode::Io,"Cannot retain chat directory");
 for(const char* component:{"plugins","azmsgsync","save"}){
  if(create&&::mkdirat(current.get(),component,0700)!=0&&errno!=EEXIST){throw Failure{{ErrorCode::Io,"Cannot create original chat directory"}};}
  Descriptor next(::openat(current.get(),component,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
  if(next.get()<0&&!create&&errno==ENOENT){return Descriptor(-1);}struct stat st{};need(next.get()>=0&&::fstat(next.get(),&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==::geteuid(),ErrorCode::UnsafePath,"Unsafe original chat directory");current=std::move(next);
 }return current;
}
// Preserve extensions on retained signed records while refreshing wire fields.
// A different encrypted content IV does not change the signed message identity.
void mergeHistoryExtensions(core::Document& incoming,const core::Document& previous){
 const auto* oldNodes=previous.find("nodes");const auto* oldMessages=previous.find("messages");
 const auto* newNodes=incoming.find("nodes");const auto* newMessages=incoming.find("messages");
 if(!oldNodes||!oldMessages||!newNodes||!newMessages){return;}
 unique(*oldNodes);unique(*newNodes);auto nodes=dict(newNodes);std::string messages="l";
 need(oldMessages->type==Node::Type::List&&newMessages->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid chat history records");
 for(std::size_t i=0;i<newNodes->children.size();i+=2){const auto& fresh=newNodes->children[i+1];
  for(std::size_t j=0;j<oldNodes->children.size();j+=2){const auto& old=oldNodes->children[j+1];
   if(text(old.find("u"))!=text(fresh.find("u"))||text(old.find("p"))!=text(fresh.find("p"))){continue;}
   auto merged=dict(&old);unique(merged.root());for(const auto field:{"u","p","c"}){const auto* v=fresh.find(field);need(v&&bool(merged.replaceValue(field,encoded(*v))),ErrorCode::InvalidFormat,"Invalid retained chat node");}
   need(bool(nodes.replaceValue(newNodes->children[i].bytes,merged.serialize())),ErrorCode::InvalidFormat,"Cannot retain chat node extensions");break;
  }
 }
 for(const auto& fresh:newMessages->children){auto merged=dict(&fresh);unique(merged.root());
  for(const auto& old:oldMessages->children){if(text(old.find("i"))!=text(fresh.find("i"))||text(old.find("s"))!=text(fresh.find("s"))){continue;}
   merged=dict(&old);unique(merged.root());for(std::size_t i=0;i<fresh.children.size();i+=2){need(bool(merged.replaceValue(fresh.children[i].bytes,encoded(fresh.children[i+1]))),ErrorCode::InvalidFormat,"Cannot refresh chat history record");}break;
  }messages+=merged.serialize();
 }
 messages+='e';need(bool(incoming.replaceValue("nodes",nodes.serialize()))&&bool(incoming.replaceValue("messages",messages)),ErrorCode::InvalidFormat,"Cannot preserve chat history extensions");
}
void regular(int dir,const std::string& name){struct stat s{};if(::fstatat(dir,name.c_str(),&s,AT_SYMLINK_NOFOLLOW)!=0){need(errno==ENOENT,ErrorCode::Io,"Cannot inspect chat history");}else{need(S_ISREG(s.st_mode)&&s.st_uid==::geteuid()&&s.st_nlink==1,ErrorCode::UnsafePath,"Unsafe original chat history file");}}
}
std::string ChatProfile::configKey(std::string_view key){return "core.crypto.msgsync.Public."+hex(key);}
std::string ChatProfile::optionKey(std::string_view key){return "Public:"+base32(key);}
std::string ChatProfile::historyLeaf(std::string_view key){if(key.size()>64){std::string hash(20,'\0');unsigned length=0;need(EVP_Digest(key.data(),key.size(),reinterpret_cast<unsigned char*>(hash.data()),&length,EVP_sha1(),nullptr)==1&&length==20,ErrorCode::Io,"Cannot derive original chat history name");return base32(hash)+"p.dat";}return base32(key)+"p.dat";}
std::expected<std::unique_ptr<ChatProfile>,Error> ChatProfile::open(const storage::ProfileLease& lease){return guarded([&]()->std::expected<std::unique_ptr<ChatProfile>,Error>{return std::unique_ptr<ChatProfile>(new ChatProfile(take(lease.retain())));});}
namespace {
constexpr std::string_view HideRatings="azbuddy.dchat.ui.hide.ratings",HideSubscriptions="azbuddy.dchat.ui.hide.search_subs";
bool propertyBoolean(const Node* node){return node&&node->type==Node::Type::Integer&&node->integer==1;}
using RootIndex=std::map<std::string_view,const Node*>;
RootIndex index(const core::Document& doc){unique(doc.root());RootIndex result;for(std::size_t n=0;n<doc.root().children.size();n+=2){result.emplace(doc.root().children[n].bytes,&doc.root().children[n+1]);}return result;}
const Node* get(const RootIndex& root,std::string_view key){const auto found=root.find(key);return found==root.end()?nullptr:found->second;}
ChatParticipantSettings participant(const RootIndex& root,const std::string& key){const auto* props=get(root,key);if(props&&props->type!=Node::Type::Dictionary){props=nullptr;}if(props){unique(*props);}const auto legacy="azbuddy.chat.pinned."+key.substr(std::string_view("azbuddy.chat.props.").size());return {propertyBoolean(props?props->find("ignored"):nullptr),propertyBoolean(props?props->find("pinned"):nullptr)||boolean(get(root,legacy))};}
void remove(core::Document& doc,std::string_view key){need(bool(doc.eraseValue(key)),ErrorCode::InvalidFormat,"Cannot preserve chat preference map");}
}
std::expected<std::string,Error> ChatProfile::participantKey(std::string_view publicKey){return guarded([&]()->std::expected<std::string,Error>{need(publicKey.size()>=16&&publicKey.size()<=512,ErrorCode::Unsupported,"Participant public key exceeds its bound");return "azbuddy.chat.props."+hex(publicKey.substr(0,16));});}
std::expected<ChatPreferences,Error> ChatProfile::preferences(std::span<const std::string> publicKeys){return guarded([&]()->std::expected<ChatPreferences,Error>{
 need(publicKeys.size()<=2048,ErrorCode::Unsupported,"Too many participant preferences requested");auto access=take(lease_->sourceAccess());auto loaded=storage::ConfigFile::load(*lease_,"biglybt.config");ChatPreferences result;if(!loaded&&loaded.error().code!=ErrorCode::Missing){return std::unexpected(loaded.error());}std::optional<RootIndex> root;if(loaded){root=index(loaded->document());result.display={boolean(get(*root,HideRatings)),boolean(get(*root,HideSubscriptions))};}for(const auto& publicKey:publicKeys){auto key=take(participantKey(publicKey));result.participants.insert_or_assign(publicKey,root?participant(*root,key):ChatParticipantSettings{});}return result;
 });}
std::expected<bool,Error> ChatProfile::saveDisplay(ChatDisplayField field,bool expected,bool value){return guarded([&]()->std::expected<bool,Error>{
 const auto key=field==ChatDisplayField::Ratings?HideRatings:HideSubscriptions;need(field==ChatDisplayField::Ratings||field==ChatDisplayField::Subscriptions,ErrorCode::Unsupported,"Unknown chat display preference");auto access=take(lease_->sourceAccess(true));auto cfg=take(storage::ConfigFile::load(*lease_,"biglybt.config"));need(cfg.recoverySource()==storage::RecoverySource::Primary,ErrorCode::Conflict,"Resolve chat preference configuration recovery first");auto root=index(cfg.document());const bool current=boolean(get(root,key));need(current==expected,ErrorCode::Conflict,"Chat display preference changed");if(current==value){return false;}need(bool(cfg.document().replaceValue(key,n(value?1:0))),ErrorCode::InvalidFormat,"Cannot retain chat display preference");done(cfg.save(*lease_));return true;
 });}
std::expected<bool,Error> ChatProfile::saveParticipant(std::string publicKey,ChatParticipantField field,bool expected,bool value){return guarded([&]()->std::expected<bool,Error>{
 const auto key=take(participantKey(publicKey));need(field==ChatParticipantField::Ignored||field==ChatParticipantField::Pinned,ErrorCode::Unsupported,"Unknown participant preference");auto access=take(lease_->sourceAccess(true));auto cfg=take(storage::ConfigFile::load(*lease_,"biglybt.config"));need(cfg.recoverySource()==storage::RecoverySource::Primary,ErrorCode::Conflict,"Resolve participant preference recovery first");auto root=index(cfg.document());const auto current=participant(root,key);const bool old=field==ChatParticipantField::Ignored?current.ignored:current.pinned;need(old==expected,ErrorCode::Conflict,"Participant preference changed");if(old==value){return false;}
 auto props=dict(get(root,key));unique(props.root());const auto legacy="azbuddy.chat.pinned."+key.substr(std::string_view("azbuddy.chat.props.").size());const bool migrate=boolean(get(root,legacy));if(migrate&&current.pinned){need(bool(props.replaceValue("pinned","i1e")),ErrorCode::InvalidFormat,"Cannot preserve legacy participant pin");}const auto name=field==ChatParticipantField::Ignored?"ignored":"pinned";if(value){need(bool(props.replaceValue(name,"i1e")),ErrorCode::InvalidFormat,"Cannot retain participant preference");}else{remove(props,name);}if(props.root().children.empty()){remove(cfg.document(),key);}else{need(bool(cfg.document().replaceValue(key,props.serialize())),ErrorCode::InvalidFormat,"Cannot retain participant settings");}if(migrate){remove(cfg.document(),legacy);}done(cfg.save(*lease_));return true;
 });}
std::expected<std::vector<ChatRoomSettings>,Error> ChatProfile::rooms(){return guarded([&]()->std::expected<std::vector<ChatRoomSettings>,Error>{
 auto access=take(lease_->sourceAccess());auto cfg=take(storage::ConfigFile::load(*lease_,"biglybt.config"));std::set<std::string> keys;
 if(const auto* options=cfg.document().find("azbuddy.dchat.optsmap")){
  unique(*options);
  for(std::size_t i=0;i<options->children.size();i+=2){const auto& key=options->children[i].bytes;if(!key.starts_with("Public:")){continue;}auto decoded=unbase32(key.substr(7));if(decoded&&!decoded->empty()&&decoded->size()<=4096){keys.insert(std::move(*decoded));}}
 }
 for(const auto map:{"azbuddy.dchat.favemap","azbuddy.dchat.savemsgmap"}){
  if(const auto* legacy=cfg.document().find(map)){unique(*legacy);for(std::size_t i=0;i<legacy->children.size();i+=2){const auto key=legacy->children[i].bytes;const auto& value=legacy->children[i+1];if(key.starts_with("Public:")&&key.size()>7&&key.size()<=4103&&value.type==Node::Type::Integer&&value.integer==1){keys.insert(std::string(key.substr(7)));}}}
 }
 std::vector<ChatRoomSettings> out,incidental;
 for(const auto& key:keys){auto value=settings(cfg.document(),key);if(value.favourite||value.saveHistory){out.push_back(std::move(value));need(out.size()<=256,ErrorCode::Unsupported,"Saved favourite/history catalogue exceeds its bound");}else if(incidental.size()<256){incidental.push_back(std::move(value));}}
 for(auto& value:incidental){if(out.size()==256){break;}out.push_back(std::move(value));}
 return out;
 });}
std::expected<ChatIdentity,Error> ChatProfile::identity(std::string key,bool create){return guarded([&]()->std::expected<ChatIdentity,Error>{
 need(!key.empty()&&key.size()<=4096,ErrorCode::Unsupported,"Chat-room key exceeds its bound");auto access=take(lease_->sourceAccess(create));auto cfg=take(storage::ConfigFile::load(*lease_,"biglybt.config"));need(cfg.recoverySource()!=storage::RecoverySource::Saving,ErrorCode::Conflict,"Resolve interrupted configuration before opening chat");
 const auto initialPolicy=services::msgsync::roomPolicy(key);need(bool(initialPolicy),ErrorCode::Unsupported,"Invalid managed-room policy");
 const auto name=configKey(key);auto value=dict(cfg.document().find(name));unique(value.root());const bool exists=cfg.document().find(name)!=nullptr;services::msgsync::Uid uid{};
 std::string pub=text(value.find("pub")),priv=text(value.find("pri"));
 struct Wipe{std::string& v;~Wipe(){if(!v.empty()){OPENSSL_cleanse(v.data(),v.size());}}} wipe{priv};
 if(!exists){need(create,ErrorCode::Missing,"This room has no saved identity");if(initialPolicy->manager){const auto* related=cfg.document().find(configKey(initialPolicy->baseKey));if(related&&text(related->find("pub"))==*initialPolicy->manager){pub=text(related->find("pub"));priv=text(related->find("pri"));}}
  if(pub.empty()){auto generated=plugins::msgsync::generateMessageIdentity();need(bool(generated),ErrorCode::Io,"Cannot create chat identity");pub=generated->publicKey;priv=generated->privateKey;}
  need(RAND_priv_bytes(uid.data(),static_cast<int>(uid.size()))==1,ErrorCode::Io,"Cannot create chat node identity");
  need(bool(value.replaceValue("uid",b({reinterpret_cast<const char*>(uid.data()),uid.size()})))&&bool(value.replaceValue("pub",b(pub)))&&bool(value.replaceValue("pri",b(priv))),ErrorCode::InvalidFormat,"Cannot encode chat identity");
 }else{const auto* saved=value.find("uid");need(saved&&saved->type==Node::Type::Bytes&&saved->bytes.size()==8&&!pub.empty()&&!priv.empty(),ErrorCode::Unsupported,"Existing chat identity is unreadable; it was not replaced");std::copy(saved->bytes.begin(),saved->bytes.end(),uid.begin());}
 auto signer=plugins::msgsync::MessageSigner::open(pub,priv);if(!priv.empty()){OPENSSL_cleanse(priv.data(),priv.size());}need(bool(signer),ErrorCode::Unsupported,"Existing chat key pair is invalid; it was not replaced");
 if(!exists){need(bool(cfg.document().replaceValue(name,value.serialize())),ErrorCode::InvalidFormat,"Cannot retain chat identity");done(cfg.save(*lease_));}
 auto selected=settings(cfg.document(),std::move(key));const auto policy=services::msgsync::roomPolicy(selected.key);
 if(policy&&policy->manager&&*policy->manager==pub){selected.readOnly=false;}
 return ChatIdentity{std::move(selected),uid,std::move(*signer),take(storage::chatIdentityStamp(cfg.document(),name))};
 });}
namespace {
void updateLegacy(core::Document& cfg,std::string_view map,std::string_view key,bool value){
 const auto* prior=cfg.find(map);if(!prior){return;}auto saved=dict(prior);const auto name="Public:"+std::string(key);
 if(saved.find(name)){need(bool(saved.replaceValue(name,n(value?1:0)))&&bool(cfg.replaceValue(map,saved.serialize())),ErrorCode::InvalidFormat,"Cannot preserve legacy chat option");}
}
std::expected<void,Error> nickname(const storage::ProfileLease& lease,std::string_view key,std::optional<bool> shared,std::optional<std::string> value,std::optional<bool> expectedShared={}){
 return guarded([&]()->std::expected<void,Error>{
  need(!key.empty()&&key.size()<=4096&&(!value||value->size()<=128),ErrorCode::Unsupported,"Chat nickname request exceeds its bound");
  auto access=take(lease.sourceAccess(true));auto cfg=take(storage::ConfigFile::load(lease,"biglybt.config"));
  need(cfg.recoverySource()!=storage::RecoverySource::Saving,ErrorCode::Conflict,"Resolve interrupted configuration before changing a nickname");
  auto options=dict(cfg.document().find("azbuddy.dchat.optsmap"));auto room=dict(options.find(ChatProfile::optionKey(key)));
  const auto old="azbuddy.chat.Public: "+std::string(key);
  if(shared){
   need(bool(room.replaceValue("sn",n(*shared?1:0))),ErrorCode::InvalidFormat,"Cannot encode nickname mode");
   if(cfg.document().find(old+".shared")){need(bool(cfg.document().replaceValue(old+".shared",n(*shared?1:0))),ErrorCode::InvalidFormat,"Cannot preserve legacy nickname mode");}
  }
  if(value){
   const auto selected=settings(cfg.document(),std::string(key));
   need(!expectedShared||selected.sharedNickname==*expectedShared,ErrorCode::Conflict,"Nickname mode changed while the editor was open");
   if(selected.sharedNickname){need(bool(cfg.document().replaceValue("azbuddy.chat.shared_nick",b(*value))),ErrorCode::InvalidFormat,"Cannot encode shared nickname");}
   else{
    need(bool(room.replaceValue("nick",b(*value))),ErrorCode::InvalidFormat,"Cannot encode room nickname");
    if(cfg.document().find(old+".nick")){need(bool(cfg.document().replaceValue(old+".nick",b(*value))),ErrorCode::InvalidFormat,"Cannot preserve legacy room nickname");}
   }
  }
  need(bool(options.replaceValue(ChatProfile::optionKey(key),room.serialize()))&&bool(cfg.document().replaceValue("azbuddy.dchat.optsmap",options.serialize())),ErrorCode::InvalidFormat,"Cannot encode nickname options");
  done(cfg.save(lease));return {};
 });
}
}
std::expected<void,Error> ChatProfile::setSharedNickname(std::string_view key,bool shared){return nickname(*lease_,key,shared,{});}
std::expected<void,Error> ChatProfile::setNickname(std::string_view key,std::string value,std::optional<bool> expectedShared){return nickname(*lease_,key,{},std::move(value),expectedShared);}
std::expected<bool,Error> ChatProfile::saveReadMarker(std::string key,std::optional<std::string> expected,std::string value){return guarded([&]()->std::expected<bool,Error>{
 need(!key.empty()&&key.size()<=4096&&value.size()<=4096,ErrorCode::Unsupported,"Chat read marker exceeds its bound");auto access=take(lease_->sourceAccess(true));auto cfg=take(storage::ConfigFile::load(*lease_,"biglybt.config"));need(cfg.recoverySource()==storage::RecoverySource::Primary,ErrorCode::Conflict,"Resolve read marker configuration recovery first");unique(cfg.document().root());auto options=dict(cfg.document().find("azbuddy.dchat.optsmap"));unique(options.root());auto room=dict(options.find(optionKey(key)));unique(room.root());std::optional<std::string> current;if(const auto* node=room.find("lmi")){need(node->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid original chat read marker");current=std::string(node->bytes);}need(current==expected,ErrorCode::Conflict,"Chat read marker changed");if(current==value){return false;}need(bool(room.replaceValue("lmi",b(value)))&&bool(options.replaceValue(optionKey(key),room.serialize()))&&bool(cfg.document().replaceValue("azbuddy.dchat.optsmap",options.serialize())),ErrorCode::InvalidFormat,"Cannot preserve original chat read marker");done(cfg.save(*lease_));return true;
 });}
std::expected<void,Error> ChatProfile::saveOptions(const ChatRoomSettings& s){return guarded([&]()->std::expected<void,Error>{
 need(!s.key.empty()&&s.key.size()<=4096,ErrorCode::Unsupported,"Invalid chat room");auto access=take(lease_->sourceAccess(true));auto cfg=take(storage::ConfigFile::load(*lease_,"biglybt.config"));need(cfg.recoverySource()!=storage::RecoverySource::Saving,ErrorCode::Conflict,"Resolve interrupted configuration before saving chat settings");auto options=dict(cfg.document().find("azbuddy.dchat.optsmap"));auto room=dict(options.find(optionKey(s.key)));
 updateLegacy(cfg.document(),"azbuddy.dchat.favemap",s.key,s.favourite);updateLegacy(cfg.document(),"azbuddy.dchat.savemsgmap",s.key,s.saveHistory);
 need(bool(room.replaceValue("fave",n(s.favourite?1:0)))&&bool(room.replaceValue("save",n(s.saveHistory?1:0)))&&bool(options.replaceValue(optionKey(s.key),room.serialize()))&&bool(cfg.document().replaceValue("azbuddy.dchat.optsmap",options.serialize())),ErrorCode::InvalidFormat,"Cannot encode chat-room options");done(cfg.save(*lease_));return {};
 });}
std::expected<std::optional<std::string>,Error> ChatProfile::history(std::string_view key){return guarded([&]()->std::expected<std::optional<std::string>,Error>{
 auto access=take(lease_->sourceAccess());auto dir=directory(*lease_,false);if(dir.get()<0){histories_[std::string(key)]={};return std::optional<std::string>{};}const auto leaf=historyLeaf(key);
 regular(dir.get(),leaf);auto raw=take(storage::detail::readFile(dir.get(),leaf,1024*1024));History state;state.primary=raw;
 if(raw){auto parsed=core::Document::parse(*raw);need(bool(parsed),ErrorCode::InvalidFormat,"Original chat history needs review; it was not changed");state.original=*raw;}else{
  regular(dir.get(),leaf+".bak");auto backup=take(storage::detail::readFile(dir.get(),leaf+".bak",1024*1024));if(backup){state.original=*backup;state.writable=false;raw=std::move(backup);}
 }
 histories_[std::string(key)]=std::move(state);return raw;
 });}
std::expected<void,Error> ChatProfile::saveHistory(std::string_view key,std::string data){return guarded([&]()->std::expected<void,Error>{
 auto access=take(lease_->sourceAccess(true));const auto found=histories_.find(std::string(key));need(found!=histories_.end()&&found->second.writable,ErrorCode::Conflict,"Load primary chat history before saving");auto& snapshot=found->second;
 auto incoming=core::Document::parse(data);need(bool(incoming),ErrorCode::InvalidFormat,"Invalid chat history snapshot");auto merged=core::Document::parse(snapshot.original.empty()?"de":snapshot.original);need(bool(merged),ErrorCode::InvalidFormat,"Invalid retained chat history");
 mergeHistoryExtensions(*incoming,*merged);
 for(const auto field:{"time","nodes","messages"}){const auto* v=incoming->find(field);need(v&&bool(merged->replaceValue(field,encoded(*v))),ErrorCode::InvalidFormat,"Invalid chat history fields");}data=merged->serialize();need(data.size()<=1024*1024,ErrorCode::Unsupported,"Chat history exceeds its bound");
 auto dir=directory(*lease_,true);const auto leaf=historyLeaf(key);for(const auto& name:{leaf,leaf+".bak",leaf+".saving"}){regular(dir.get(),name);}auto temporary=take(storage::detail::readFile(dir.get(),leaf+".saving",1024*1024));need(!temporary,ErrorCode::Conflict,"An interrupted chat history write needs review");auto current=take(storage::detail::readFile(dir.get(),leaf,1024*1024));need(current==snapshot.primary,ErrorCode::Conflict,"Chat history changed since it was read");
 auto staged=take(storage::detail::writeExclusiveOwned(dir.get(),leaf+".saving",data,0600));struct stat held{},named{};need(::fstat(staged.get(),&held)==0&&::fstatat(dir.get(),(leaf+".saving").c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&held.st_dev==named.st_dev&&held.st_ino==named.st_ino&&held.st_nlink==1,ErrorCode::Conflict,"Chat history staging file changed");
 need(take(storage::detail::readFile(dir.get(),leaf,1024*1024))==snapshot.primary,ErrorCode::Conflict,"Chat history changed before publication");
 if(snapshot.primary){need(::renameat(dir.get(),leaf.c_str(),dir.get(),(leaf+".bak").c_str())==0,ErrorCode::Io,"Cannot retain original chat-history backup");}
 need(::renameat(dir.get(),(leaf+".saving").c_str(),dir.get(),leaf.c_str())==0&&::fsync(dir.get())==0,ErrorCode::Io,"Cannot publish chat history");snapshot.primary=data;snapshot.original=std::move(data);return {};
 });}
}
