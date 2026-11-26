// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_remote_diversification.hpp"
#include "azureus_local_diversification.hpp"
#include "azureus_key_block_area.hpp"
#include "azureus_recent_address_area.hpp"
#include "azureus_profile_files.hpp"
#include <set>
#include <map>
#include <algorithm>
namespace biglybtc::storage {
namespace {
using detail::StateDescriptor;
using detail::StateHandle;
using Capture = detail::ProfileFileSnapshot;
namespace files = detail::azureusFiles;using Record=core::AzureusRemoteDiversification;
auto err(ErrorCode code,const char* message){return std::unexpected(Error{code,message});}
struct Paths {
  std::shared_ptr<const ProfileLease> lease;
  StateDescriptor dht, net;
  std::string network;

  StateHandle dir() const { return network.empty() ? dht.get() : net.get(); }
  bool current() const {
    return files::currentDirectory(lease->directoryHandle(), "dht", dht.get())
        && (network.empty() || files::currentDirectory(dht.get(), network, net.get()));
  }
};

std::expected<Paths, Error> paths(const AzureusValueVersions& versions) {
  auto lease = versions.retainProfile();
  if (!lease) { return std::unexpected(lease.error()); }
  auto dht = files::openDirectory((*lease)->directoryHandle(), "dht");
  if (!dht) { return std::unexpected(dht.error()); }
  Paths path{std::move(*lease), std::move(*dht), {}, {}};
  if (versions.network()) {
    path.network = "net" + std::to_string(versions.network());
    auto net = files::openDirectory(path.dht.get(), path.network);
    if (!net) { return std::unexpected(net.error()); }
    path.net = std::move(*net);
  }
  if (!path.current()) { return err(ErrorCode::UnsafePath, "Unsafe Azureus diversification directory"); }
  return path;
}

std::expected<Capture, Error> capture(StateHandle directory, const std::string& name) {
  return files::capture(directory, name, compatibleConfigLimits().inputBytes);
}
std::expected<core::Document,Error> parse(std::string bytes){auto d=core::Document::parse(std::move(bytes),compatibleConfigLimits());if(!d||d->root().type!=core::Node::Type::Dictionary){return err(ErrorCode::InvalidFormat,"Invalid Azureus diversification map");}std::set<std::string_view> keys;for(std::size_t i=0;i<d->root().children.size();i+=2){if(!keys.insert(d->root().children[i].bytes).second){return err(ErrorCode::InvalidFormat,"Duplicate Azureus diversification map key");}}if(const auto* remote=d->find("remote");remote&&remote->type!=core::Node::Type::List){return err(ErrorCode::InvalidFormat,"Invalid Azureus remote diversification list");}return std::move(*d);}
std::optional<Record> record(const core::Node& n){if(n.type!=core::Node::Type::Dictionary){return {};}const auto* key=n.find("key"),*type=n.find("type"),*exp=n.find("exp"),*fpo=n.find("fpo");if(!key||key->type!=core::Node::Type::Bytes||key->bytes.size()!=20||!type||type->type!=core::Node::Type::Integer||(type->integer!=2&&type->integer!=3)||!exp||exp->type!=core::Node::Type::Integer||!fpo||fpo->type!=core::Node::Type::List||fpo->children.size()!=2){return {};}std::set<std::string_view> keys;for(std::size_t i=0;i<n.children.size();i+=2){if(!keys.insert(n.children[i].bytes).second){return {};}}Record r;std::copy_n(reinterpret_cast<const std::uint8_t*>(key->bytes.data()),20,r.key.begin());r.type=std::int8_t(type->integer);r.expiry=exp->integer;for(unsigned i=0;i<2;++i){const auto& v=fpo->children[i];if(v.type!=core::Node::Type::Integer||v.integer<0||v.integer>=10){return {};}r.offsets[i]=unsigned(v.integer);}if(r.offsets[0]==r.offsets[1]){return {};}return r;}
std::string encoded(const Record& r){return "d3:expi"+std::to_string(r.expiry)+"e3:fpoli"+std::to_string(r.offsets[0])+"ei"+std::to_string(r.offsets[1])+"ee3:key20:"+std::string(reinterpret_cast<const char*>(r.key.data()),20)+"4:typei"+std::to_string(r.type)+"ee";}
std::expected<void, Error> publish(Paths& path, const AzureusValueVersions& versions,
    const Capture& primary, const core::Document& doc) {
  const auto bytes = doc.serialize();
  if (primary.bytes == std::optional{bytes}) { return {}; }
  const auto maximum = compatibleConfigLimits().inputBytes;
  const auto stage = files::stage(path.dir(), "diverse.saving", bytes, maximum);
  if (!stage) { return std::unexpected(stage.error()); }
  const auto live = capture(path.dir(), "diverse.dat");
  if (!live || !primary.matches(*live) || !path.current() || !versions.validate()) {
    return err(ErrorCode::Conflict, "Azureus diversification preimage changed");
  }
  const auto published = files::publish(path.dir(), "diverse.saving", "diverse.dat", *stage,
      primary.bytes.has_value(), maximum);
  if (!published) { return std::unexpected(published.error()); }
  if (!path.current()) { return err(ErrorCode::Conflict, "Azureus diversification directory changed"); }
  return {};
}
using LocalRecord=core::AzureusLocalDiversification;
std::optional<LocalRecord> localRecord(const core::Node& n){
 if(n.type!=core::Node::Type::Dictionary){return {};}std::set<std::string_view> names;for(std::size_t i=0;i<n.children.size();i+=2){if(!names.insert(n.children[i].bytes).second){return {};}}
 const auto* key=n.find("key"),*type=n.find("type"),*expiry=n.find("exp");if(!key||key->type!=core::Node::Type::Bytes||key->bytes.size()!=20||!type||type->type!=core::Node::Type::Integer||(type->integer!=2&&type->integer!=3)||!expiry||expiry->type!=core::Node::Type::Integer){return {};}
 LocalRecord r;std::copy_n(reinterpret_cast<const std::uint8_t*>(key->bytes.data()),20,r.key.begin());r.type=std::int8_t(type->integer);r.expiry=expiry->integer;return r;
}
std::string encodedLocal(const LocalRecord& r){return "d3:expi"+std::to_string(r.expiry)+"e3:key20:"+std::string(reinterpret_cast<const char*>(r.key.data()),20)+"4:typei"+std::to_string(r.type)+"ee";}

}
std::expected<std::vector<core::AzureusRemoteDiversification>,Error> readAzureusRemoteDiversifications(const AzureusValueVersions& versions,std::int64_t now){try{auto p=paths(versions);if(!p){return std::unexpected(p.error());}auto access=p->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto primary=capture(p->dir(),"diverse.dat"),pending=capture(p->dir(),"diverse.saving");if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}auto doc=parse(primary->bytes.value_or(pending->bytes.value_or("de")));if(!doc){return std::unexpected(doc.error());}std::map<std::array<std::uint8_t,20>,Record> items;if(const auto* remote=doc->find("remote")){for(const auto& node:remote->children){auto item=record(node);if(item&&item->expiry>now){items.insert_or_assign(item->key,*item);if(items.size()>core::AzureusDiversificationLedger::MaximumRecords){return err(ErrorCode::InvalidFormat,"Too many Azureus diversification records");}}}}if(!p->current()){return err(ErrorCode::Conflict,"Azureus diversification directory changed");}std::vector<Record> out;for(const auto& [key,item]:items){(void)key;out.push_back(item);}return out;}catch(...){return err(ErrorCode::Io,"Cannot read Azureus remote diversifications");}}
std::expected<void,Error> saveAzureusRemoteDiversifications(const AzureusValueVersions& versions,std::span<const core::AzureusRemoteDiversification> updates){try{if(updates.empty()){return {};}if(updates.size()>512){return err(ErrorCode::InvalidFormat,"Too many Azureus diversification updates");}std::set<std::array<std::uint8_t,20>> updateKeys;for(const auto& r:updates){if(!core::validAzureusDiversification(r)||!updateKeys.insert(r.key).second){return err(ErrorCode::InvalidFormat,"Invalid Azureus diversification update");}}auto p=paths(versions);if(!p){return std::unexpected(p.error());}auto access=p->lease->sourceAccess(true);if(!access){return std::unexpected(access.error());}auto primary=capture(p->dir(),"diverse.dat"),pending=capture(p->dir(),"diverse.saving");if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}if(pending->bytes){return err(ErrorCode::Conflict,"Review pending Azureus diversification publication");}auto doc=parse(primary->bytes.value_or("de"));if(!doc){return std::unexpected(doc.error());}std::set<std::array<std::uint8_t,20>> written;std::string list="l";
// Retain duplicate/unrecognised rows; Java selects the last live record. Patch
// the last recognised row for each updated key and preserve its unknown fields.
std::map<std::array<std::uint8_t,20>,const core::Node*> last;
if(const auto* remote=doc->find("remote")){for(const auto& node:remote->children){if(auto old=record(node)){last[old->key]=&node;}
 else if(node.type==core::Node::Type::Dictionary){for(std::size_t n=0;n<node.children.size();n+=2){if(node.children[n].bytes!="key"){continue;}const auto& key=node.children[n+1];if(key.type!=core::Node::Type::Bytes||key.bytes.size()!=20){continue;}for(const auto& update:updates){if(key.bytes==std::string_view(reinterpret_cast<const char*>(update.key.data()),20)){return err(ErrorCode::Conflict,"Ambiguous existing Azureus diversification entry");}}}}
}
 for(const auto& node:remote->children){auto raw=core::encodeValue(node);if(!raw){return err(ErrorCode::InvalidFormat,"Cannot retain Azureus diversification entry");}auto old=record(node);const auto found=old?std::ranges::find(updates,old->key,&Record::key):updates.end();
  if(found!=updates.end()&&last[found->key]==&node){auto patched=core::Document::parse(*raw,compatibleConfigLimits());auto known=core::Document::parse(encoded(*found));if(!patched||!known){return err(ErrorCode::InvalidFormat,"Cannot patch Azureus diversification entry");}for(const auto name:{"key","type","exp","fpo"}){auto value=core::encodeValue(*known->find(name));if(!value||!patched->replaceValue(name,*value)){return err(ErrorCode::InvalidFormat,"Cannot retain Azureus diversification fields");}}list+=patched->serialize();written.insert(found->key);
  }else{list+=*raw;}
 }
}for(const auto& r:updates){if(written.insert(r.key).second){list+=encoded(r);}}list+='e';if(!doc->replaceValue("remote",list)){return err(ErrorCode::InvalidFormat,"Cannot merge Azureus diversifications");}return publish(*p,versions,*primary,*doc);}catch(...){return err(ErrorCode::Io,"Cannot save Azureus remote diversifications");}}
std::expected<std::vector<core::AzureusLocalDiversification>,Error> readAzureusLocalDiversifications(const AzureusValueVersions& versions,std::int64_t now){try{
 auto p=paths(versions);if(!p){return std::unexpected(p.error());}auto access=p->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto primary=capture(p->dir(),"diverse.dat"),pending=capture(p->dir(),"diverse.saving");if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}auto doc=parse(primary->bytes.value_or(pending->bytes.value_or("de")));if(!doc){return std::unexpected(doc.error());}
 std::map<std::array<std::uint8_t,20>,LocalRecord> records;
 if(const auto* list=doc->find("local")){if(list->type!=core::Node::Type::List){return err(ErrorCode::InvalidFormat,"Invalid Azureus local diversification list");}for(const auto& node:list->children){if(auto r=localRecord(node);r&&r->expiry>now){records.insert_or_assign(r->key,*r);if(records.size()>core::AzureusDiversificationLedger::MaximumRecords){return err(ErrorCode::InvalidFormat,"Too many Azureus local diversification records");}}}}
 if(!p->current()){return err(ErrorCode::Conflict,"Azureus diversification directory changed");}std::vector<LocalRecord> out;for(const auto& [key,r]:records){(void)key;out.push_back(r);}return out;
}catch(...){return err(ErrorCode::Io,"Cannot read Azureus local diversifications");}}
std::expected<void,Error> saveAzureusLocalDiversifications(const AzureusValueVersions& versions,std::span<const core::AzureusLocalDiversification> updates){try{
 if(updates.empty()){return {};}if(updates.size()>512){return err(ErrorCode::InvalidFormat,"Too many Azureus local diversification updates");}std::set<std::array<std::uint8_t,20>> keys;for(const auto& r:updates){if((r.type!=1&&r.type!=2&&r.type!=3)||(r.type!=1&&r.expiry<=0)||!keys.insert(r.key).second){return err(ErrorCode::InvalidFormat,"Invalid Azureus local diversification update");}}
 auto p=paths(versions);if(!p){return std::unexpected(p.error());}auto access=p->lease->sourceAccess(true);if(!access){return std::unexpected(access.error());}auto primary=capture(p->dir(),"diverse.dat"),pending=capture(p->dir(),"diverse.saving");if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}if(pending->bytes){return err(ErrorCode::Conflict,"Review pending Azureus diversification publication");}auto doc=parse(primary->bytes.value_or("de"));if(!doc){return std::unexpected(doc.error());}
 const auto* list=doc->find("local");if(list&&list->type!=core::Node::Type::List){return err(ErrorCode::InvalidFormat,"Invalid Azureus local diversification list");}if(!list&&std::ranges::all_of(updates,[](const auto& r){return r.type==1;})){return {};}
 std::map<std::array<std::uint8_t,20>,const core::Node*> last;std::set<std::array<std::uint8_t,20>> written;std::string result="l";
 if(list){for(const auto& node:list->children){if(auto r=localRecord(node)){last[r->key]=&node;}
  else if(node.type==core::Node::Type::Dictionary){for(std::size_t i=0;i<node.children.size();i+=2){if(node.children[i].bytes!="key"){continue;}const auto& k=node.children[i+1];if(k.type!=core::Node::Type::Bytes||k.bytes.size()!=20){continue;}for(const auto& r:updates){if(k.bytes==std::string_view(reinterpret_cast<const char*>(r.key.data()),20)){return err(ErrorCode::Conflict,"Ambiguous existing Azureus local diversification entry");}}}}
 }
 for(const auto& node:list->children){auto raw=core::encodeValue(node);if(!raw){return err(ErrorCode::InvalidFormat,"Cannot retain Azureus local diversification entry");}auto old=localRecord(node);const auto found=old?std::ranges::find(updates,old->key,&LocalRecord::key):updates.end();
  if(found==updates.end()){result+=*raw;continue;}written.insert(found->key);if(found->type==1){continue;}if(last[found->key]!=&node){result+=*raw;continue;}
  auto patched=core::Document::parse(*raw,compatibleConfigLimits());auto known=core::Document::parse(encodedLocal(*found));if(!patched||!known){return err(ErrorCode::InvalidFormat,"Cannot patch Azureus local diversification entry");}for(const auto key:{"key","type","exp"}){auto value=core::encodeValue(*known->find(key));if(!value||!patched->replaceValue(key,*value)){return err(ErrorCode::InvalidFormat,"Cannot retain Azureus local diversification fields");}}result+=patched->serialize();
 }}
 for(const auto& r:updates){if(r.type!=1&&written.insert(r.key).second){result+=encodedLocal(r);}}result+='e';if(!doc->replaceValue("local",result)){return err(ErrorCode::InvalidFormat,"Cannot merge Azureus local diversifications");}return publish(*p,versions,*primary,*doc);
}catch(...){return err(ErrorCode::Io,"Cannot save Azureus local diversifications");}}

// Key blocks share the existing fixed-directory capture and publication guards.
// The service layer owns signatures/policy; this adapter grants no such trust.
struct AzureusKeyBlockArea::Data {Paths paths;Capture primary,pending;std::string prefix;};
struct AzureusDhtAreaAccess {
 static std::expected<AzureusKeyBlockArea,Error> read(const AzureusValueVersions&,std::string_view prefix);
 static std::expected<AzureusKeyBlockArea,Error> save(const AzureusValueVersions&,const AzureusKeyBlockArea&,std::string,std::string_view prefix);
};
const std::optional<std::string>& AzureusKeyBlockArea::primary()const noexcept{return data_->primary.bytes;}
const std::optional<std::string>& AzureusKeyBlockArea::pending()const noexcept{return data_->pending.bytes;}
std::expected<AzureusKeyBlockArea,Error> AzureusDhtAreaAccess::read(const AzureusValueVersions& versions,std::string_view prefix){try{
 if(prefix!="block"&&prefix!="addresses"){return err(ErrorCode::Unsupported,"Unsupported Azureus storage area");}const auto dat=std::string(prefix)+".dat",saving=std::string(prefix)+".saving";
 auto p=paths(versions);if(!p){return std::unexpected(p.error());}auto access=p->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto primary=capture(p->dir(),dat.c_str()),pending=capture(p->dir(),saving.c_str());if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}if((primary->bytes&&primary->bytes->size()>4*1024*1024)||(pending->bytes&&pending->bytes->size()>4*1024*1024)){return err(ErrorCode::InvalidFormat,"Azureus DHT storage area exceeds its bound");}if(!p->current()||!versions.validate()){return err(ErrorCode::Conflict,"Azureus DHT storage directory changed");}return AzureusKeyBlockArea(std::make_shared<AzureusKeyBlockArea::Data>(std::move(*p),std::move(*primary),std::move(*pending),std::string(prefix)));
}catch(...){return err(ErrorCode::Io,"Cannot read Azureus DHT storage areas");}}
std::expected<AzureusKeyBlockArea,Error> AzureusDhtAreaAccess::save(const AzureusValueVersions& versions,const AzureusKeyBlockArea& before,std::string bytes,std::string_view prefix){try{
 if(prefix!="block"&&prefix!="addresses"){return err(ErrorCode::Unsupported,"Unsupported Azureus storage area");}const auto dat=std::string(prefix)+".dat",saving=std::string(prefix)+".saving";
 auto retained=versions.retainProfile();if(!retained){return std::unexpected(retained.error());}const auto& old=*before.data_;if(old.prefix!=prefix){return err(ErrorCode::Conflict,"Azureus storage-area role differs");}const auto here = detail::stateStamp((*retained)->directoryHandle());
 const auto previous = detail::stateStamp(old.paths.lease->directoryHandle());
 if (!here || !previous || !detail::sameStateDirectory(*here, *previous) ||
     old.paths.network != (versions.network() ? "net" + std::to_string(versions.network()) : "")) {
   return err(ErrorCode::Conflict, "Azureus DHT storage source owner differs");
 }
 auto access=old.paths.lease->sourceAccess(true);if(!access){return std::unexpected(access.error());}if(!old.paths.current()||old.pending.bytes){return err(ErrorCode::Conflict,"Review pending Azureus DHT storage publication");}
 auto parsed=core::Document::parse(bytes);if(!parsed||parsed->root().type!=core::Node::Type::Dictionary){return err(ErrorCode::InvalidFormat,"Invalid Azureus DHT storage map");}auto live=capture(old.paths.dir(),dat.c_str()),pending=capture(old.paths.dir(),saving.c_str());if(!live){return std::unexpected(live.error());}if(!pending){return std::unexpected(pending.error());}if(!old.primary.matches(*live)||!old.pending.matches(*pending)){return err(ErrorCode::Conflict,"Azureus DHT storage preimage changed");}if(live->bytes==std::optional{bytes}){return before;}
 const auto maximum = compatibleConfigLimits().inputBytes;
 const auto staged = files::stage(old.paths.dir(), saving, bytes, maximum);
 if (!staged) { return std::unexpected(staged.error()); }
 const auto current = capture(old.paths.dir(), dat);
 if (!current || !old.primary.matches(*current) || !old.paths.current() || !versions.validate()) {
   return err(ErrorCode::Conflict, "Azureus DHT storage preimage changed before publication");
 }
 auto published = files::publish(old.paths.dir(), saving, dat, *staged, old.primary.bytes.has_value(), maximum);
 if (!published) { return std::unexpected(published.error()); }
 auto next=paths(versions);if(!next||!old.paths.current()){return err(ErrorCode::Conflict,"Azureus DHT storage directory changed after publication");}return AzureusKeyBlockArea(std::make_shared<AzureusKeyBlockArea::Data>(std::move(*next),std::move(*published),Capture{},std::string(prefix)));
}catch(...){return err(ErrorCode::Io,"Cannot save Azureus DHT storage areas");}}

std::expected<AzureusKeyBlockArea,Error> readAzureusKeyBlockArea(const AzureusValueVersions& versions){return AzureusDhtAreaAccess::read(versions,"block");}
std::expected<AzureusKeyBlockArea,Error> saveAzureusKeyBlockArea(const AzureusValueVersions& versions,const AzureusKeyBlockArea& before,std::string bytes){return AzureusDhtAreaAccess::save(versions,before,std::move(bytes),"block");}
std::expected<AzureusRecentAddressArea,Error> readAzureusRecentAddressArea(const AzureusValueVersions& versions){auto read=AzureusDhtAreaAccess::read(versions,"addresses");if(!read){return std::unexpected(read.error());}return AzureusRecentAddressArea(std::move(*read));}
std::expected<AzureusRecentAddressArea,Error> saveAzureusRecentAddressArea(const AzureusValueVersions& versions,const AzureusRecentAddressArea& before,std::string bytes){auto saved=AzureusDhtAreaAccess::save(versions,before.captured_,std::move(bytes),"addresses");if(!saved){return std::unexpected(saved.error());}return AzureusRecentAddressArea(std::move(*saved));}
}
