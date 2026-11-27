// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../platform/utf8_path.hpp"
#include "saved_subscription_source.hpp"
#include "saved_subscription_seed.hpp"
#include "subscription_values.hpp"
#include "subscription_literal_filter.hpp"
#include "subscription_automatic_values.hpp"
#include "subscription_store_internal.hpp"
#include "saved_search_profile.hpp"
#include "library_http_trust.hpp"
#include <QCryptographicHash>
#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <thread>

namespace biglybtc::search {
namespace {
#include "rss_source_internal.inc"
namespace state = storage::detail;
using state::StateDescriptor;
using state::StateHandle;
constexpr std::string_view PendingWrite=".subscriptions-native-write";
E valueError(SubscriptionValueError error){switch(error){case SubscriptionValueError::Ambiguous:return E::Ambiguous;case SubscriptionValueError::Unsupported:return E::Unsupported;case SubscriptionValueError::Limit:return E::Limit;case SubscriptionValueError::Memory:return E::Memory;default:return E::Malformed;}}
const File* findFile(const std::vector<File>& files,std::string_view name){const auto p=std::ranges::find(files,name,&File::name);return p==files.end()?nullptr:&*p;}
std::expected<StateDescriptor, E> subscriptionDirectory(const storage::ProfileLease& lease, StateStamp& identity) {
  auto dir = state::openStateDirectory(lease.directoryHandle(), "subs");
  if (!dir) { return std::unexpected(E::Malformed); }
  const auto held = state::stateStamp(dir->get());
  const auto label = state::stateStampAt(lease.directoryHandle(), "subs");
  if (!held || !label || !*label || !sameDirectory(*held, **label)) { return std::unexpected(E::Malformed); }
  identity = *held;
  return std::move(*dir);
}
bool matchingBody(const core::Document& body,const core::Node& row,const SavedSubscriptionSelector& selected){
  const auto* vuze=body.find("vuze");const auto* list=vuze?vuze->find("components"):nullptr;if(!list||list->type!=core::Node::Type::List||list->children.size()!=1){return false;}
  const auto* content=list->children[0].find("content");const auto* details=content?content->find("details"):nullptr;if(!details||!content){return false;}
  const auto identical=[](const core::Node& a,std::string_view x,const core::Node& b,std::string_view y){const auto* left=a.find(x);const auto* right=b.find(y);return left&&right&&left->type==core::Node::Type::Bytes&&right->type==core::Node::Type::Bytes&&left->bytes==right->bytes;};
  const auto* size=content->find("size");const auto* expected=row.find("sig_data_size");if(!identical(*content,"hash",row,"hash")||!identical(*content,"sig",row,"sig")||!size||!expected||size->type!=core::Node::Type::Integer||expected->type!=core::Node::Type::Integer||size->integer!=expected->integer||size->integer<=0||size->integer>4*1024*1024){return false;}
  // Existing local Original bodies are bencoded. Bind the actual stored signed
  // details bytes/hash/size as well as the local metadata; never rewrite them.
  const auto detailBytes=body.originalBytes().substr(details->begin,details->end-details->begin);
  const auto actualHash=QCryptographicHash::hash(QByteArray::fromStdString(detailBytes),QCryptographicHash::Sha1).toStdString();
  const auto* storedHash=content->find("hash");if(!storedHash||storedHash->bytes!=actualHash||size->integer!=static_cast<std::int64_t>(detailBytes.size())){return false;}
  if(!integer(*details,"az_version",1,1,1)||!zero(*details,"is_anonymous")){return false;}
  const auto* singleton=details->find("sin_details");if(selected.singletonKey){const auto* key=singleton?singleton->find("key"):nullptr;return key&&key->type==core::Node::Type::Bytes&&key->bytes==*selected.singletonKey;}return !singleton;
}
#include "subscription_store_internal.inc"

std::expected<void,E> prepareVersionedSeed(StateHandle directory,const std::string& leaf,std::string_view payload) {
  auto existing=named(directory,leaf,MetaBytes);
  if(!existing) { return std::unexpected(existing.error()); }
  if(existing->present) {
    if(existing->bytes!=payload||!stillNamed(directory,*existing)) { return std::unexpected(E::SourceChanged); }
    return {};
  }
  const auto staging=leaf+".saving";
  auto staged=named(directory,staging,MetaBytes);
  if(!staged) { return std::unexpected(staged.error()); }
  if(!staged->present) {
    auto written=writePrivate(directory,staging,payload);
    if(!written) { return written; }
    staged=named(directory,staging,MetaBytes);
  }
  if(!staged||!staged->present||staged->bytes!=payload||!stillNamed(directory,*staged)) {
    return std::unexpected(E::SourceChanged);
  }
#ifdef _WIN32
  staged->witness.releaseWitness();
#endif
  if(!state::publishStateFile(directory,staging,leaf,state::InvalidStateHandle,false,&*staged->witness.stamp)
      ||!state::syncStateDirectory(directory)) { return std::unexpected(E::SourceChanged); }
  return {};
}

bool subscriptionSeedWritePending(StateHandle root) {
  for(const auto* marker:{"subscriptions.config.saving",PendingWrite.data(),
      ".subscriptions-native-write.bak",".subscriptions-native-write.saving"}) {
    const auto pending=state::stateStampAt(root,marker);
    if(!pending||*pending) { return true; }
  }
  return false;
}

bool eligibleSubscriptionSeed(const core::Node& row,SubscriptionSeedNetwork network) {
  const int anonymous=network==SubscriptionSeedNetwork::I2P?1:0;
  return integer(row,"subscribed",1,1,0)&&!row.find("sin_details")
      &&integer(row,"is_anonymous",anonymous,anonymous,0);
}

struct VersionedSeedAccess {
  std::filesystem::path& directory;
  std::shared_ptr<const StateDescriptor>& directoryHandle;
  SubscriptionSeedFile operation;
};

std::expected<std::optional<SavedSubscriptionSeed>,E> subscriptionSeedFromProfile(
    const storage::ProfileLease& lease,const storage::ProfileSourceAccess& access,
    std::string_view key,SubscriptionSeedKey kind,SubscriptionSeedNetwork network,
    std::int64_t creationSeconds,std::optional<VersionedSeedAccess> versioned) try {
  if(key.size()!=20||creationSeconds<0) { return std::unexpected(E::Malformed); }
  if(!lease.authorizesObservation(access)) { return std::unexpected(E::SourceChanged); }
  const auto root=lease.directoryHandle();
  if(subscriptionSeedWritePending(root)) { return std::unexpected(E::SourceChanged); }
  auto config=named(root,"subscriptions.config",MetaBytes);
  if(!config) { return std::unexpected(config.error()); }
  if(!config->present) { return std::optional<SavedSubscriptionSeed>{}; }
  auto settings=named(root,"biglybt.config",MetaBytes);
  if(!settings) { return std::unexpected(settings.error()); }
  if(kind==SubscriptionSeedKey::DdbLookup&&settings->present){
    const auto configured=core::Document::parse(settings->bytes,ConfigLimits);
    if(!configured||configured->root().type!=core::Node::Type::Dictionary||!unique(configured->root())) {
      return std::unexpected(E::Malformed);
    }
    if(!integer(configured->root(),"Plugin.DHT.dht.torrent_xfer.enable",1,1,1)) {
      return std::optional<SavedSubscriptionSeed>{};
    }
  }
  const auto catalog=core::Document::parse(config->bytes,ConfigLimits);
  if(!catalog||!unique(catalog->root())) { return std::unexpected(E::Malformed); }
  const auto* list=catalog->find("subs");
  if(!list||list->type!=core::Node::Type::List) { return std::unexpected(E::Malformed); }
  if(list->children.size()>4096) { return std::unexpected(E::Limit); }
  const core::Node* selected=nullptr;
  for(const auto& row:list->children) {
    const auto* hash=row.find("hash");
    if(!bytes(hash,20)||hash->bytes.size()!=20) { continue; }
    if(kind==SubscriptionSeedKey::Publication) {
      if(hash->bytes!=key) { continue; }
    }else{
      const auto hashBytes=QByteArrayView(hash->bytes.data(),static_cast<qsizetype>(hash->bytes.size()));
      const auto digest=QCryptographicHash::hash(hashBytes,QCryptographicHash::Sha1);
      if(std::string_view(digest.constData(),static_cast<std::size_t>(digest.size()))!=key) { continue; }
    }
    if(selected) { return std::unexpected(E::Ambiguous); }
    selected=&row;
  }
  if(!selected||!eligibleSubscriptionSeed(*selected,network)) {
    return std::optional<SavedSubscriptionSeed>{};
  }
  const auto* publicKey=selected->find("public_key");
  if(!bytes(publicKey,49)||publicKey->bytes.size()<25) { return std::unexpected(E::Malformed); }
  const auto publicBytes=QByteArrayView(publicKey->bytes.data(),static_cast<qsizetype>(publicKey->bytes.size()));
  const auto stem=QCryptographicHash::hash(publicBytes,QCryptographicHash::Sha1).first(10).toHex().toUpper().toStdString();
  StateStamp directoryIdentity;
  auto subs=subscriptionDirectory(lease,directoryIdentity);
  if(!subs) { return std::unexpected(subs.error()); }
  auto body=named(subs->get(),stem+".biglybt",MetaBytes);
  if(!body) { return std::unexpected(body.error()); }
  if(!body->present) { return std::optional<SavedSubscriptionSeed>{}; }
  auto seed=makeSubscriptionSeed(*selected,body->bytes,creationSeconds);
  if(!seed) { return std::unexpected(E::Malformed); }
  const auto currentDirectory=state::stateStampAt(root,"subs");
  if(!currentDirectory||!*currentDirectory||!sameDirectory(**currentDirectory,directoryIdentity)
      ||!stillNamed(root,*config)||!stillNamed(root,*settings)||!stillNamed(subs->get(),*body)
      ||subscriptionSeedWritePending(root)) {
    return std::unexpected(E::SourceChanged);
  }
  if(versioned) {
    auto path=state::stateDirectoryPath(subs->get());
    if(!path) { return std::unexpected(E::SourceChanged); }
    if(versioned->operation==SubscriptionSeedFile::Prepare) {
      const auto prepared=prepareVersionedSeed(subs->get(),seed->fileName,body->bytes);
      if(!prepared) { return std::unexpected(prepared.error()); }
      versioned->directoryHandle=std::make_shared<const StateDescriptor>(std::move(*subs));
    }else{
      const auto existing=named(subs->get(),seed->fileName,MetaBytes);
      if(!existing) { return std::unexpected(existing.error()); }
      if(!existing->present||existing->bytes!=body->bytes||!stillNamed(subs->get(),*existing)) {
        return std::unexpected(E::SourceChanged);
      }
    }
    versioned->directory=std::move(*path);
  }
  return std::optional{SavedSubscriptionSeed{std::move(*seed),std::move(body->bytes)}};
} catch(const std::bad_alloc&) { return std::unexpected(E::Memory); }
} // namespace

std::expected<std::vector<SavedSubscriptionSeedCandidate>,E> savedSubscriptionSeedCandidates(
    const storage::ProfileLease& lease,SubscriptionSeedNetwork network) try {
  const auto access=lease.sourceAccess();
  const auto root=lease.directoryHandle();
  if(!access||!access->observationsAllowed()||subscriptionSeedWritePending(root)) {
    return std::unexpected(E::SourceChanged);
  }
  std::vector<SavedSubscriptionSeedCandidate> result;
  const auto config=named(root,"subscriptions.config",MetaBytes);
  if(!config) { return std::unexpected(config.error()); }
  if(!config->present) { return result; }
  const auto catalog=core::Document::parse(config->bytes,ConfigLimits);
  if(!catalog||!unique(catalog->root())) { return std::unexpected(E::Malformed); }
  const auto* list=catalog->find("subs");
  if(!list||list->type!=core::Node::Type::List) { return std::unexpected(E::Malformed); }
  if(list->children.size()>4096) { return std::unexpected(E::Limit); }

  std::set<std::string_view> seen,duplicates;
  for(const auto& row:list->children) {
    const auto* hash=row.find("hash");
    if(!bytes(hash,20)||hash->bytes.size()!=20) { continue; }
    if(!seen.emplace(hash->bytes).second) { duplicates.emplace(hash->bytes); }
    if(!eligibleSubscriptionSeed(row,network)) { continue; }
    const auto* publicKey=row.find("public_key");
    if(!bytes(publicKey,49)||publicKey->bytes.size()<25) { continue; }
    SavedSubscriptionSeedCandidate seed{};
    std::ranges::copy(hash->bytes,seed.hash.begin());
    seed.publicKey=publicKey->bytes;
    const auto* popularity=row.find("pop");
    seed.popularity=popularity&&popularity->type==core::Node::Type::Integer?popularity->integer:-1;
    result.push_back(std::move(seed));
  }
  std::erase_if(result,[&](const auto& seed){return duplicates.contains({seed.hash.data(),seed.hash.size()});});
  if(!stillNamed(root,*config)||subscriptionSeedWritePending(root)) { return std::unexpected(E::SourceChanged); }
  return result;
} catch(const std::bad_alloc&) { return std::unexpected(E::Memory); }

std::expected<std::optional<SavedSubscriptionSeed>,SavedRssSourceError> readSavedSubscriptionSeed(
    const storage::ProfileLease& lease,std::string_view key,SubscriptionSeedKey kind,SubscriptionSeedNetwork network,
    std::int64_t creationSeconds) {
  const auto access=lease.sourceAccess();
  if(!access) { return std::unexpected(E::SourceChanged); }
  return readSavedSubscriptionSeed(lease,*access,key,kind,network,creationSeconds);
}

std::expected<std::optional<SavedSubscriptionSeed>,SavedRssSourceError> readSavedSubscriptionSeed(
    const storage::ProfileLease& lease,const storage::ProfileSourceAccess& access,std::string_view key,
    SubscriptionSeedKey kind,SubscriptionSeedNetwork network,std::int64_t creationSeconds) {
  return subscriptionSeedFromProfile(lease,access,key,kind,network,creationSeconds,std::nullopt);
}

std::expected<std::optional<PreparedSubscriptionSeed>,SavedRssSourceError> resolveVersionedSubscriptionSeed(
    const storage::ProfileLease& lease,std::string_view publicationHash,SubscriptionSeedNetwork network,
    std::int64_t creationSeconds,SubscriptionSeedFile operation) {
  const auto access=lease.sourceAccess(operation==SubscriptionSeedFile::Prepare);
  if(!access) { return std::unexpected(E::SourceChanged); }
  return resolveVersionedSubscriptionSeed(lease,*access,publicationHash,network,creationSeconds,operation);
}

std::expected<std::optional<PreparedSubscriptionSeed>,SavedRssSourceError> resolveVersionedSubscriptionSeed(
    const storage::ProfileLease& lease,const storage::ProfileSourceAccess& access,std::string_view publicationHash,
    SubscriptionSeedNetwork network,std::int64_t creationSeconds,SubscriptionSeedFile operation) {
  if (operation==SubscriptionSeedFile::Prepare && !lease.authorizesMutation(access)) {
    return std::unexpected(E::SourceChanged);
  }
  std::filesystem::path directory;
  std::shared_ptr<const StateDescriptor> directoryHandle;
  auto checked=subscriptionSeedFromProfile(lease,access,publicationHash,SubscriptionSeedKey::Publication,network,
      creationSeconds,VersionedSeedAccess{directory,directoryHandle,operation});
  if(!checked) { return std::unexpected(checked.error()); }
  if(!*checked) { return std::optional<PreparedSubscriptionSeed>{}; }
  return std::optional{PreparedSubscriptionSeed{
      std::move((**checked).metainfo),std::move(directory),std::move(directoryHandle)}};
}

struct SavedSubscriptionSourceSnapshot::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;
  std::filesystem::path profile;
  std::uint64_t session=0;
  std::thread::id worker;
  StateStamp directoryIdentity{},subsIdentity{};
  StateDescriptor subs;
  std::vector<File> rootFiles,subFiles;
  std::optional<detail::SubscriptionAutomaticPolicyValues> automaticPolicy;
  SubscriptionIndex index;
  SubscriptionResults stored;
  std::shared_ptr<const SubscriptionNumericFilter> numericFilter;
  std::shared_ptr<const SubscriptionLiteralFilter> literalFilter;
  SubscriptionRssDescription description;
  std::string resultsName;
  std::expected<void,E> stable(const storage::ProfileLease& caller)const{
    const auto root = directory(caller, profile);
    const auto held = state::stateStamp(subs.get());
    const auto label = state::stateStampAt(caller.directoryHandle(), "subs");
    if (!root || !sameDirectory(*root, directoryIdentity) || !held || !label || !*label
        || !sameDirectory(*held, subsIdentity) || !sameDirectory(**label, subsIdentity)) {
      return std::unexpected(E::SourceChanged);
    }
    auto roots=unchanged(caller.directoryHandle(),rootFiles);if(!roots){return std::unexpected(roots.error());}return unchanged(subs.get(),subFiles);
  }
};
SavedSubscriptionSourceSnapshot::SavedSubscriptionSourceSnapshot(std::unique_ptr<Impl> p)noexcept:impl_(std::move(p)){}
SavedSubscriptionSourceSnapshot::SavedSubscriptionSourceSnapshot(SavedSubscriptionSourceSnapshot&&)noexcept=default;
SavedSubscriptionSourceSnapshot& SavedSubscriptionSourceSnapshot::operator=(SavedSubscriptionSourceSnapshot&&)noexcept=default;
SavedSubscriptionSourceSnapshot::~SavedSubscriptionSourceSnapshot()=default;
const SubscriptionRssDescription& SavedSubscriptionSourceSnapshot::description()const noexcept{return impl_->description;}
const detail::SubscriptionAutomaticPolicyValues* SavedSubscriptionSourceSnapshot::automaticPolicy()const noexcept{return impl_->automaticPolicy?&*impl_->automaticPolicy:nullptr;}
const SubscriptionSavedRow& SavedSubscriptionSourceSnapshot::metadata()const noexcept{return impl_->index.rows[impl_->description.selected.sourceIndex];}
const SubscriptionResults& SavedSubscriptionSourceSnapshot::results()const noexcept{return impl_->stored;}
const core::Node& SavedSubscriptionSourceSnapshot::selectedSourceRow()const noexcept{return impl_->index.source->find("subs")->children[impl_->description.selected.sourceIndex];}
std::string_view SavedSubscriptionSourceSnapshot::selectedSourceBytes()const noexcept{const auto& row=selectedSourceRow();return std::string_view(impl_->index.source->originalBytes()).substr(row.begin,row.end-row.begin);}
const SubscriptionNumericFilter* SavedSubscriptionSourceSnapshot::numericFilter()const noexcept{return impl_->numericFilter.get();}
const SubscriptionLiteralFilter* SavedSubscriptionSourceSnapshot::literalFilter()const noexcept{return impl_->literalFilter.get();}
std::expected<std::string,E> SavedSubscriptionSourceSnapshot::freezeAcquisitionSource(const storage::ProfileLease& caller,std::uint64_t session)const{try{
  if(!impl_||impl_->worker!=std::this_thread::get_id()){return std::unexpected(E::WrongWorker);}if(session!=impl_->session||!caller.sharesOwnership(*impl_->lease)){return std::unexpected(E::Lease);}auto access=caller.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(E::SourceChanged);}auto stable=impl_->stable(caller);if(!stable){return std::unexpected(stable.error());}
  const auto entry=[&](const File& file){return file.present?bDictionary({{"bytes",bString(file.bytes)},{"stamp",bString(profile_files::stamp(*file.witness.stamp))}}):std::string("i0e");};std::map<std::string,std::string> roots,children;
  for(const auto& file:impl_->rootFiles){if(!file.name.starts_with("subscriptions.config")){roots[file.name]=entry(file);}}
  for(const auto& file:impl_->subFiles){if(!file.name.starts_with(impl_->resultsName)){children[file.name]=entry(file);}}
  std::map<std::string,std::string> fields{{"schema",bString("BiglyBTC.Subscription.Acquisition.Source.V1")},{"profile",bString(platform::pathToUtf8(impl_->profile))},{"root_scope",bString(profile_files::scope(impl_->directoryIdentity))},{"subs_scope",bString(profile_files::scope(impl_->subsIdentity))},{"roots",bDictionary(roots)},{"children",bDictionary(children)}};
  const auto append=[&](std::string key,const File* file){fields[std::move(key)]=file&&file->present?bString(file->bytes):"i0e";};append("configuration",findFile(impl_->rootFiles,"subscriptions.config"));append("configuration_backup",findFile(impl_->rootFiles,"subscriptions.config.bak"));append("results",findFile(impl_->subFiles,impl_->resultsName));append("results_backup",findFile(impl_->subFiles,impl_->resultsName+".bak"));return bDictionary(fields);
}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}

std::expected<RssContext,E> SavedSubscriptionSourceSnapshot::resultUrlContext(std::string_view value){return context(value);}
std::expected<void,E> SavedSubscriptionSourceSnapshot::resultHeaderPolicy()const{
  const auto* file=findFile(impl_->rootFiles,"biglybt.config");auto settings=core::Document::parse(file&&file->present?file->bytes:"de",{SettingsBytes,SettingsBytes,65536,64});if(!settings){return std::unexpected(E::Malformed);}
  if(const auto* headers=settings->find("metasearch.web.last.headers")){if(headers->type!=core::Node::Type::Bytes){return std::unexpected(E::Malformed);}if(!headers->bytes.empty()){return std::unexpected(E::Unsupported);}}return {};
}

std::expected<std::optional<QList<QSslCertificate>>,E> SavedSubscriptionSourceSnapshot::resultTrust(const storage::ProfileLease& caller,std::uint64_t session)const{
  if(!impl_||impl_->worker!=std::this_thread::get_id()){return std::unexpected(E::WrongWorker);}if(session!=impl_->session||!caller.sharesOwnership(*impl_->lease)){return std::unexpected(E::Lease);}
  auto access=caller.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(E::SourceChanged);}auto stable=impl_->stable(caller);if(!stable){return std::unexpected(stable.error());}
  auto trust=engine::detail::libraryHttpTrust(caller);if(!trust){return std::unexpected(E::Unsupported);}stable=impl_->stable(caller);if(!stable){return std::unexpected(stable.error());}return std::move(trust->anchors);
}

std::expected<SavedSubscriptionSourceSnapshot,E> SavedSubscriptionSourceSnapshot::read(
    const storage::ProfileLease& lease,const std::filesystem::path& profile,std::uint64_t session,SavedSubscriptionSelector selected){return readImpl(lease,profile,session,std::move(selected),Purpose::Manual);}
std::expected<SavedSubscriptionSourceSnapshot,E> SavedSubscriptionSourceSnapshot::readAutomatic(
    const storage::ProfileLease& lease,const std::filesystem::path& profile,std::uint64_t session,SavedSubscriptionSelector selected){return readImpl(lease,profile,session,std::move(selected),Purpose::Automatic);}
std::expected<SavedSubscriptionSourceSnapshot,E> SavedSubscriptionSourceSnapshot::readImpl(
    const storage::ProfileLease& lease,const std::filesystem::path& profile,std::uint64_t session,SavedSubscriptionSelector selected,Purpose purpose){
  try{
    if(!session||selected.shortId.size()!=10||selected.publicKey.empty()||selected.publicKey.size()>4096){return std::unexpected(E::Ambiguous);}
    auto access=lease.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(E::Lease);}auto root=directory(lease,profile);if(!root){return std::unexpected(root.error());}
    auto alias=lease.retain();if(!alias){return std::unexpected(E::Lease);}auto data=std::make_unique<Impl>();data->lease=std::move(*alias);data->profile=profile;data->session=session;data->worker=std::this_thread::get_id();data->directoryIdentity=*root;
    for(const auto stem:{"subscriptions.config","metasearch.config","biglybt.config",".certs"}){
      for(const auto suffix:{"",".bak",".saving"}){const auto name=std::string(stem)+suffix;auto file=named(lease.directoryHandle(),name,cap(name));if(!file){return std::unexpected(file.error());}if(std::string_view(suffix)==".saving"&&file->present){return std::unexpected(E::SourceChanged);}data->rootFiles.push_back(std::move(*file));}
      const auto* primary=findFile(data->rootFiles,stem);const auto* backup=findFile(data->rootFiles,std::string(stem)+".bak");if(primary&&!primary->present&&backup&&backup->present){return std::unexpected(E::SourceChanged);}
    }
    auto lock=named(lease.directoryHandle(),".azlock",4096);if(!lock||!lock->present){return std::unexpected(E::Lease);}data->rootFiles.push_back(std::move(*lock));
    for(const auto suffix:{"",".bak",".saving"}){auto pending=named(lease.directoryHandle(),std::string(PendingWrite)+suffix,4*1024*1024);if(!pending){return std::unexpected(pending.error());}if(pending->present){return std::unexpected(E::SourceChanged);}data->rootFiles.push_back(std::move(*pending));}
    const auto* config=findFile(data->rootFiles,"subscriptions.config");if(!config||!config->present){return std::unexpected(E::Malformed);}auto index=parseSubscriptionIndex(config->bytes,purpose==Purpose::Automatic?SubscriptionIndexPurpose::AutomaticAcquisition:SubscriptionIndexPurpose::Manual);if(!index){return std::unexpected(valueError(index.error()));}data->index=std::move(*index);
    if(selected.sourceIndex>=data->index.rows.size()){return std::unexpected(E::Ambiguous);}const auto& selectedRow=data->index.rows[selected.sourceIndex];if(selectedRow.issue){return std::unexpected(valueError(*selectedRow.issue));}if(selectedRow.selected!=selected){return std::unexpected(E::Ambiguous);}if(purpose==Purpose::Automatic&&(!selectedRow.autoDownload||!selectedRow.enabled||!selectedRow.subscribed)){return std::unexpected(E::Unsupported);}
    auto subs=subscriptionDirectory(lease,data->subsIdentity);if(!subs){return std::unexpected(subs.error());}data->subs=std::move(*subs);
    const auto stem=QByteArray::fromStdString(selected.shortId).toHex().toUpper().toStdString();const auto bodyName=stem+".biglybt";data->resultsName=stem+".results";
    for(const auto& name:{bodyName,data->resultsName}){for(const auto suffix:{"",".bak",".saving"}){auto file=named(data->subs.get(),name+suffix,4*1024*1024);if(!file){return std::unexpected(file.error());}if(std::string_view(suffix)==".saving"&&file->present){return std::unexpected(E::SourceChanged);}data->subFiles.push_back(std::move(*file));}const auto* primary=findFile(data->subFiles,name);const auto* backup=findFile(data->subFiles,name+".bak");if(primary&&!primary->present&&backup&&backup->present){return std::unexpected(E::SourceChanged);}}
    const auto* bodyFile=findFile(data->subFiles,bodyName);if(!bodyFile||!bodyFile->present){return std::unexpected(E::Malformed);}auto body=core::Document::parse(bodyFile->bytes,ConfigLimits);const auto* rows=data->index.source->find("subs");if(!body||!rows||!matchingBody(*body,rows->children[selected.sourceIndex],selected)){return std::unexpected(E::Malformed);}
    const auto* meta=findFile(data->rootFiles,"metasearch.config");std::optional<core::Document> catalog;if(meta&&meta->present){auto parsed=core::Document::parse(meta->bytes,ConfigLimits);if(!parsed){return std::unexpected(E::Malformed);}catalog.emplace(std::move(*parsed));}
    auto definition=parseSubscriptionDefinition(bodyFile->bytes,selectedRow,catalog?&*catalog:nullptr);if(!definition){return std::unexpected(valueError(definition.error()));}const auto& engineRow=definition->engine->root();const auto* uid=engineRow.find("uid");if(!bytes(uid,256,true)){return std::unexpected(E::Malformed);}
    std::expected<RssGetDescription,E> transport=describeRow(engineRow);
    if(!definition->embedded){const auto* list=catalog->find("engines");std::optional<std::size_t> at;for(std::size_t i=0;i<list->children.size();++i){const auto* id=list->children[i].find("id");if(id&&id->type==core::Node::Type::Integer&&id->integer==definition->engineId){at=i;break;}}if(!at){return std::unexpected(E::Ambiguous);}auto actual=describe(*catalog,{*at,definition->engineId,std::string(uid->bytes)});if(!actual){return std::unexpected(actual.error());}transport=static_cast<RssGetDescription&&>(*actual);}
    if(!transport){return std::unexpected(transport.error());}const auto* settingsFile=findFile(data->rootFiles,"biglybt.config");auto settingsDoc=core::Document::parse(settingsFile&&settingsFile->present?settingsFile->bytes:"de",{SettingsBytes,SettingsBytes,65536,64});if(!settingsDoc){return std::unexpected(E::Malformed);}auto policy=settings(settingsDoc->root(),definition->engineId);if(!policy){return std::unexpected(policy.error());}
    if(purpose==Purpose::Automatic){auto automatic=detail::readSubscriptionAutomaticPolicy(*settingsDoc,platform::pathToUtf8(profile));if(!automatic){switch(automatic.error()){case detail::SubscriptionAutomaticValueError::Memory:return std::unexpected(E::Memory);case detail::SubscriptionAutomaticValueError::Limit:return std::unexpected(E::Limit);case detail::SubscriptionAutomaticValueError::Malformed:return std::unexpected(E::Malformed);default:return std::unexpected(E::Unsupported);}}data->automaticPolicy=std::move(*automatic);}
    if(!zero(settingsDoc->root(),"subscriptions.config.activate.sub.on.change")||!emptyBytes(settingsDoc->root(),"subscriptions.gmar.id")){return std::unexpected(E::Unsupported);}const auto* libraryRead=settingsDoc->find("subscriptions.config.mark.lib.results.read");if(libraryRead&&(libraryRead->type!=core::Node::Type::Integer||(libraryRead->integer!=0&&libraryRead->integer!=1))){return std::unexpected(E::Malformed);}data->index.markLibraryRead=!libraryRead||libraryRead->integer==1;
    data->index.rows[selected.sourceIndex].intervalMinutes=definition->intervalMinutes;data->numericFilter=definition->numericFilter;data->literalFilter=definition->literalFilter;
    const auto* defaultPeriod=settingsDoc->find("subscriptions.check.period.mins.default");if(defaultPeriod&&defaultPeriod->type!=core::Node::Type::Integer){return std::unexpected(E::Malformed);}data->index.rows[selected.sourceIndex].defaultCheckMinutes=defaultPeriod&&defaultPeriod->integer>0?defaultPeriod->integer:120;
    if(data->index.rows[selected.sourceIndex].maxResults<0){const auto* maximum=settingsDoc->find("subscriptions.max.non.deleted.results");if(maximum&&(maximum->type!=core::Node::Type::Integer||maximum->integer<0||maximum->integer>std::numeric_limits<std::int32_t>::max())){return std::unexpected(E::Unsupported);}data->index.rows[selected.sourceIndex].maxResults=maximum?maximum->integer:512;}
    if(transport->literalGetUrl.starts_with("https://")){auto trust=engine::detail::libraryHttpTrust(lease);if(!trust){return std::unexpected(E::Unsupported);}transport->trustAnchors=std::move(trust->anchors);}
    static_cast<RssGetDescription&>(data->description)=std::move(*transport);data->description.selected=std::move(selected);data->description.engineId=definition->engineId;data->description.engineUid=std::string(uid->bytes);data->description.embeddedEngine=definition->embedded;
    const auto* resultFile=findFile(data->subFiles,data->resultsName);auto results=parseSubscriptionResults(resultFile&&resultFile->present?resultFile->bytes:"d7:resultslee");if(!results){return std::unexpected(valueError(results.error()));}data->stored=std::move(*results);
    auto stable=data->stable(lease);if(!stable){return std::unexpected(stable.error());}return SavedSubscriptionSourceSnapshot(std::move(data));
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
std::expected<std::uint64_t,E> SavedSubscriptionSourceSnapshot::current(const storage::ProfileLease& caller,std::uint64_t session)const{
  try{if(!impl_||impl_->worker!=std::this_thread::get_id()){return std::unexpected(E::WrongWorker);}if(session!=impl_->session||!caller.sharesOwnership(*impl_->lease)){return std::unexpected(E::Lease);}auto access=caller.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(E::SourceChanged);}auto stable=impl_->stable(caller);if(!stable){return std::unexpected(stable.error());}return access->revision();}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
std::expected<SubscriptionHistoryCommit,E> SavedSubscriptionSourceSnapshot::prepareCheck(const RssDocument& document,std::int64_t now)const{if(impl_->automaticPolicy&&document.channels.empty()){return std::unexpected(E::Unsupported);}auto value=reconcileSubscriptionResults(impl_->index,description().selected.sourceIndex,impl_->stored,document,description().engineId,now,impl_->numericFilter.get(),impl_->literalFilter.get(),impl_->automaticPolicy?std::optional{document.channels.back().declaredAutoDownload}:std::nullopt);if(!value){return std::unexpected(valueError(value.error()));}return std::move(*value);}
std::expected<SubscriptionHistoryCommit,E> SavedSubscriptionSourceSnapshot::prepareState(const std::vector<std::string>& keys,bool read,bool deleted,std::int64_t now)const{auto value=changeSubscriptionResultState(impl_->index,description().selected.sourceIndex,impl_->stored,keys,read,deleted,now);if(!value){return std::unexpected(valueError(value.error()));}return std::move(*value);}

std::expected<void,E> SavedSubscriptionSourceSnapshot::publish(const storage::ProfileLease& caller,std::uint64_t session,const SubscriptionHistoryCommit& value){
  try{
    if(!impl_||impl_->worker!=std::this_thread::get_id()){return std::unexpected(E::WrongWorker);}if(session!=impl_->session||!caller.sharesOwnership(*impl_->lease)){return std::unexpected(E::Lease);}
    auto access=caller.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(E::SourceChanged);}auto stable=impl_->stable(caller);if(!stable){return stable;}if(value.configuration==impl_->index.source->originalBytes()&&value.results==impl_->stored.source->originalBytes()){return {};}
    Intent intent;intent.automatic=impl_->automaticPolicy.has_value();intent.selected=description().selected.sourceIndex;intent.sid=description().selected.shortId;intent.stem=QByteArray::fromStdString(intent.sid).toHex().toUpper().toStdString();intent.beforeConfig=impl_->index.source->originalBytes();intent.afterConfig=value.configuration;intent.afterResults=value.results;
    const auto optionalBytes=[](const File* file)->std::optional<std::string>{return file&&file->present?std::optional{file->bytes}:std::nullopt;};intent.beforeConfigBackup=optionalBytes(findFile(impl_->rootFiles,"subscriptions.config.bak"));intent.beforeResults=optionalBytes(findFile(impl_->subFiles,impl_->resultsName));intent.beforeResultsBackup=optionalBytes(findFile(impl_->subFiles,impl_->resultsName+".bak"));
    for(const auto& file:impl_->rootFiles){if(file.name.starts_with("metasearch.config")||file.name.starts_with("biglybt.config")||file.name.starts_with(".certs")){intent.roots[file.name]=file.present?std::optional{sha256(file.bytes)}:std::nullopt;}}
    for(const auto& file:impl_->subFiles){if(file.name.starts_with(intent.stem+".biglybt")){intent.children[file.name]=file.present?std::optional{sha256(file.bytes)}:std::nullopt;}}
    auto raw=intentBytes(intent);auto verified=parseIntent(raw);if(!verified){return std::unexpected(verified.error());}
    // This is the bounded synchronous commit point. Like ConfigFile.save, it
    // completes or leaves an owned recoverable intent; no parser/network waits.
    auto mutation = caller.sourceAccess(true);
    if (!mutation) { return std::unexpected(E::Lease); }
    const auto saved = writePrivate(caller.directoryHandle(), std::string(PendingWrite), raw);
    if (!saved || !state::syncStateDirectory(caller.directoryHandle())) { return std::unexpected(E::SourceChanged); }
    auto owned = named(caller.directoryHandle(), std::string(PendingWrite), 28*1024*1024);
    if (!owned || !owned->present || owned->bytes != raw) { return std::unexpected(E::SourceChanged); }
    auto applied = recoverIntent(caller, impl_->subs.get(), *verified);
    if (!applied) { return applied; }
    auto removed = unlinkOwned(caller.directoryHandle(), *owned);
    if (!removed || !state::syncStateDirectory(caller.directoryHandle())) { return std::unexpected(E::SourceChanged); }
    return {};
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
namespace detail {
std::expected<SubscriptionResults,E> readSubscriptionHistory(const storage::ProfileLease& lease,const std::filesystem::path& path,const SavedSubscriptionSelector& selected){
  try{auto access=lease.sourceAccess();if(!access||!access->observationsAllowed()||!directory(lease,path)){return std::unexpected(E::Lease);}for(const auto* suffix:{"",".bak",".saving"}){auto pending=named(lease.directoryHandle(),std::string(PendingWrite)+suffix,28*1024*1024);if(!pending||pending->present){return std::unexpected(E::SourceChanged);}}
    auto config=storage::ConfigFile::load(lease,"subscriptions.config",ConfigLimits);if(!config||config->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(E::SourceChanged);}auto index=parseSubscriptionIndex(config->document().originalBytes());if(!index||selected.sourceIndex>=index->rows.size()){return std::unexpected(E::Malformed);}const auto& row=index->rows[selected.sourceIndex];if(row.selected!=selected||(row.issue&&*row.issue!=SubscriptionValueError::Unsupported)){return std::unexpected(E::Ambiguous);}
    StateStamp identity{};auto dir=subscriptionDirectory(lease,identity);if(!dir){return std::unexpected(dir.error());}const auto name=QByteArray::fromStdString(selected.shortId).toHex().toUpper().toStdString()+".results";auto primary=named(dir->get(),name,4*1024*1024),backup=named(dir->get(),name+".bak",4*1024*1024),saving=named(dir->get(),name+".saving",4*1024*1024);if(!primary||!backup||!saving||saving->present){return std::unexpected(E::SourceChanged);}auto values=parseSubscriptionResults(primary->present?primary->bytes:backup->present?backup->bytes:"d7:resultslee");if(!values){return std::unexpected(valueError(values.error()));}return std::move(*values);
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
std::expected<void,E> recoverSubscriptionWrite(const storage::ProfileLease& lease,const std::filesystem::path& path){
  try{auto access=lease.sourceAccess();if(!access||!access->observationsAllowed()||!directory(lease,path)){return std::unexpected(E::Lease);}auto pending=named(lease.directoryHandle(),std::string(PendingWrite),28*1024*1024);if(!pending){return std::unexpected(pending.error());}for(const auto* suffix:{".bak",".saving"}){auto other=named(lease.directoryHandle(),std::string(PendingWrite)+suffix,28*1024*1024);if(!other||other->present){return std::unexpected(E::SourceChanged);}}if(!pending->present){return {};}
    auto intent=parseIntent(pending->bytes);if(!intent){return std::unexpected(intent.error());}StateStamp identity{};auto subs=subscriptionDirectory(lease,identity);if(!subs){return std::unexpected(subs.error());}auto mutation=lease.sourceAccess(true);if(!mutation){return std::unexpected(E::Lease);}auto applied=recoverIntent(lease,subs->get(),*intent);if(!applied){return applied;}auto removed=unlinkOwned(lease.directoryHandle(),*pending);if(!removed||!state::syncStateDirectory(lease.directoryHandle())){return std::unexpected(E::SourceChanged);}return {};
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
}
} // namespace biglybtc::search
