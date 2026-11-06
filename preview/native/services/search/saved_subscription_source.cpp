// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_subscription_source.hpp"
#include "subscription_values.hpp"
#include "subscription_store_internal.hpp"
#include "file_io.hpp"
#include "library_http_trust.hpp"
#include <QCryptographicHash>
#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <map>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <new>
#include <set>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace biglybtc::search {
namespace {
#include "rss_source_internal.inc"
constexpr std::string_view PendingWrite=".subscriptions-native-write";
E valueError(SubscriptionValueError error){switch(error){case SubscriptionValueError::Ambiguous:return E::Ambiguous;case SubscriptionValueError::Unsupported:return E::Unsupported;case SubscriptionValueError::Limit:return E::Limit;case SubscriptionValueError::Memory:return E::Memory;default:return E::Malformed;}}
const File* findFile(const std::vector<File>& files,std::string_view name){const auto p=std::ranges::find(files,name,&File::name);return p==files.end()?nullptr:&*p;}
bool directorySame(const struct stat& a,const struct stat& b){return S_ISDIR(a.st_mode)&&S_ISDIR(b.st_mode)&&a.st_uid==::geteuid()&&b.st_uid==::geteuid()&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino;}
std::expected<Descriptor,E> subscriptionDirectory(const storage::ProfileLease& lease,struct stat& identity){
  Descriptor dir(::openat(lease.directoryHandle(),"subs",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));struct stat label{};
  if(dir.get()<0||::fstat(dir.get(),&identity)!=0||::fstatat(lease.directoryHandle(),"subs",&label,AT_SYMLINK_NOFOLLOW)!=0||!directorySame(identity,label)){return std::unexpected(E::Malformed);}return dir;
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
} // namespace

struct SavedSubscriptionSourceSnapshot::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;
  std::filesystem::path profile;
  std::uint64_t session=0;
  std::thread::id worker;
  struct stat directoryIdentity{},subsIdentity{};
  Descriptor subs{-1};
  std::vector<File> rootFiles,subFiles;
  SubscriptionIndex index;
  SubscriptionResults stored;
  SubscriptionRssDescription description;
  std::string resultsName;
  std::expected<void,E> stable(const storage::ProfileLease& caller)const{
    auto root=directory(caller,profile);if(!root||!directorySame(*root,directoryIdentity)){return std::unexpected(E::SourceChanged);}struct stat held{},label{};
    if(::fstat(subs.get(),&held)!=0||::fstatat(caller.directoryHandle(),"subs",&label,AT_SYMLINK_NOFOLLOW)!=0||!directorySame(held,subsIdentity)||!directorySame(label,subsIdentity)){return std::unexpected(E::SourceChanged);}
    auto roots=unchanged(caller.directoryHandle(),rootFiles);if(!roots){return std::unexpected(roots.error());}return unchanged(subs.get(),subFiles);
  }
};
SavedSubscriptionSourceSnapshot::SavedSubscriptionSourceSnapshot(std::unique_ptr<Impl> p)noexcept:impl_(std::move(p)){}
SavedSubscriptionSourceSnapshot::SavedSubscriptionSourceSnapshot(SavedSubscriptionSourceSnapshot&&)noexcept=default;
SavedSubscriptionSourceSnapshot& SavedSubscriptionSourceSnapshot::operator=(SavedSubscriptionSourceSnapshot&&)noexcept=default;
SavedSubscriptionSourceSnapshot::~SavedSubscriptionSourceSnapshot()=default;
const SubscriptionRssDescription& SavedSubscriptionSourceSnapshot::description()const noexcept{return impl_->description;}
const SubscriptionSavedRow& SavedSubscriptionSourceSnapshot::metadata()const noexcept{return impl_->index.rows[impl_->description.selected.sourceIndex];}
const SubscriptionResults& SavedSubscriptionSourceSnapshot::results()const noexcept{return impl_->stored;}
std::expected<SavedSubscriptionSourceSnapshot,E> SavedSubscriptionSourceSnapshot::read(const storage::ProfileLease& lease,
    const std::filesystem::path& profile,std::uint64_t session,SavedSubscriptionSelector selected){
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
    const auto* config=findFile(data->rootFiles,"subscriptions.config");if(!config||!config->present){return std::unexpected(E::Malformed);}auto index=parseSubscriptionIndex(config->bytes);if(!index){return std::unexpected(valueError(index.error()));}data->index=std::move(*index);
    if(selected.sourceIndex>=data->index.rows.size()){return std::unexpected(E::Ambiguous);}const auto& selectedRow=data->index.rows[selected.sourceIndex];if(selectedRow.issue){return std::unexpected(valueError(*selectedRow.issue));}if(selectedRow.selected!=selected){return std::unexpected(E::Ambiguous);}
    auto subs=subscriptionDirectory(lease,data->subsIdentity);if(!subs){return std::unexpected(subs.error());}data->subs=std::move(*subs);
    const auto stem=QByteArray::fromStdString(selected.shortId).toHex().toUpper().toStdString();const auto bodyName=stem+".biglybt";data->resultsName=stem+".results";
    for(const auto& name:{bodyName,data->resultsName}){for(const auto suffix:{"",".bak",".saving"}){auto file=named(data->subs.get(),name+suffix,4*1024*1024);if(!file){return std::unexpected(file.error());}if(std::string_view(suffix)==".saving"&&file->present){return std::unexpected(E::SourceChanged);}data->subFiles.push_back(std::move(*file));}const auto* primary=findFile(data->subFiles,name);const auto* backup=findFile(data->subFiles,name+".bak");if(primary&&!primary->present&&backup&&backup->present){return std::unexpected(E::SourceChanged);}}
    const auto* bodyFile=findFile(data->subFiles,bodyName);if(!bodyFile||!bodyFile->present){return std::unexpected(E::Malformed);}auto body=core::Document::parse(bodyFile->bytes,ConfigLimits);const auto* rows=data->index.source->find("subs");if(!body||!rows||!matchingBody(*body,rows->children[selected.sourceIndex],selected)){return std::unexpected(E::Malformed);}
    const auto* meta=findFile(data->rootFiles,"metasearch.config");std::optional<core::Document> catalog;if(meta&&meta->present){auto parsed=core::Document::parse(meta->bytes,ConfigLimits);if(!parsed){return std::unexpected(E::Malformed);}catalog.emplace(std::move(*parsed));}
    auto definition=parseSubscriptionDefinition(bodyFile->bytes,selectedRow,catalog?&*catalog:nullptr);if(!definition){return std::unexpected(valueError(definition.error()));}const auto& engineRow=definition->engine->root();const auto* uid=engineRow.find("uid");if(!bytes(uid,256,true)){return std::unexpected(E::Malformed);}
    std::expected<RssGetDescription,E> transport=describeRow(engineRow);
    if(!definition->embedded){const auto* list=catalog->find("engines");std::optional<std::size_t> at;for(std::size_t i=0;i<list->children.size();++i){const auto* id=list->children[i].find("id");if(id&&id->type==core::Node::Type::Integer&&id->integer==definition->engineId){at=i;break;}}if(!at){return std::unexpected(E::Ambiguous);}auto actual=describe(*catalog,{*at,definition->engineId,std::string(uid->bytes)});if(!actual){return std::unexpected(actual.error());}transport=static_cast<RssGetDescription&&>(*actual);}
    if(!transport){return std::unexpected(transport.error());}const auto* settingsFile=findFile(data->rootFiles,"biglybt.config");auto settingsDoc=core::Document::parse(settingsFile&&settingsFile->present?settingsFile->bytes:"de",{SettingsBytes,SettingsBytes,65536,64});if(!settingsDoc){return std::unexpected(E::Malformed);}auto policy=settings(settingsDoc->root(),definition->engineId);if(!policy){return std::unexpected(policy.error());}
    if(!zero(settingsDoc->root(),"subscriptions.config.activate.sub.on.change")||!emptyBytes(settingsDoc->root(),"subscriptions.gmar.id")){return std::unexpected(E::Unsupported);}const auto* libraryRead=settingsDoc->find("subscriptions.config.mark.lib.results.read");if(libraryRead&&(libraryRead->type!=core::Node::Type::Integer||(libraryRead->integer!=0&&libraryRead->integer!=1))){return std::unexpected(E::Malformed);}data->index.markLibraryRead=!libraryRead||libraryRead->integer==1;
    data->index.rows[selected.sourceIndex].intervalMinutes=definition->intervalMinutes;
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
std::expected<SubscriptionHistoryCommit,E> SavedSubscriptionSourceSnapshot::prepareCheck(const RssDocument& document,std::int64_t now)const{auto value=reconcileSubscriptionResults(impl_->index,description().selected.sourceIndex,impl_->stored,document,description().engineId,now);if(!value){return std::unexpected(valueError(value.error()));}return std::move(*value);}
std::expected<SubscriptionHistoryCommit,E> SavedSubscriptionSourceSnapshot::prepareState(const std::vector<std::string>& keys,bool read,bool deleted,std::int64_t now)const{auto value=changeSubscriptionResultState(impl_->index,description().selected.sourceIndex,impl_->stored,keys,read,deleted,now);if(!value){return std::unexpected(valueError(value.error()));}return std::move(*value);}

std::expected<void,E> SavedSubscriptionSourceSnapshot::publish(const storage::ProfileLease& caller,std::uint64_t session,const SubscriptionHistoryCommit& value){
  try{
    if(!impl_||impl_->worker!=std::this_thread::get_id()){return std::unexpected(E::WrongWorker);}if(session!=impl_->session||!caller.sharesOwnership(*impl_->lease)){return std::unexpected(E::Lease);}
    auto access=caller.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(E::SourceChanged);}auto stable=impl_->stable(caller);if(!stable){return stable;}if(value.configuration==impl_->index.source->originalBytes()&&value.results==impl_->stored.source->originalBytes()){return {};}
    Intent intent;intent.selected=description().selected.sourceIndex;intent.sid=description().selected.shortId;intent.stem=QByteArray::fromStdString(intent.sid).toHex().toUpper().toStdString();intent.beforeConfig=impl_->index.source->originalBytes();intent.afterConfig=value.configuration;intent.afterResults=value.results;
    const auto optionalBytes=[](const File* file)->std::optional<std::string>{return file&&file->present?std::optional{file->bytes}:std::nullopt;};intent.beforeConfigBackup=optionalBytes(findFile(impl_->rootFiles,"subscriptions.config.bak"));intent.beforeResults=optionalBytes(findFile(impl_->subFiles,impl_->resultsName));intent.beforeResultsBackup=optionalBytes(findFile(impl_->subFiles,impl_->resultsName+".bak"));
    for(const auto& file:impl_->rootFiles){if(file.name.starts_with("metasearch.config")||file.name.starts_with("biglybt.config")||file.name.starts_with(".certs")){intent.roots[file.name]=file.present?std::optional{sha256(file.bytes)}:std::nullopt;}}
    for(const auto& file:impl_->subFiles){if(file.name.starts_with(intent.stem+".biglybt")){intent.children[file.name]=file.present?std::optional{sha256(file.bytes)}:std::nullopt;}}
    auto raw=intentBytes(intent);auto verified=parseIntent(raw);if(!verified){return std::unexpected(verified.error());}
    // This is the bounded synchronous commit point. Like ConfigFile.save, it
    // completes or leaves an owned recoverable intent; no parser/network waits.
    auto mutation=caller.sourceAccess(true);if(!mutation){return std::unexpected(E::Lease);}auto journal=storage::detail::writeExclusiveOwned(caller.directoryHandle(),std::string(PendingWrite),raw,0600);if(!journal){return std::unexpected(E::SourceChanged);}if(::fchmod(journal->get(),0600)!=0||::fsync(journal->get())!=0||::fsync(caller.directoryHandle())!=0){return std::unexpected(E::SourceChanged);}
    auto owned=named(caller.directoryHandle(),std::string(PendingWrite),28*1024*1024);if(!owned||!owned->present||owned->bytes!=raw){return std::unexpected(E::SourceChanged);}auto applied=recoverIntent(caller,impl_->subs.get(),*verified);if(!applied){return applied;}auto removed=unlinkOwned(caller.directoryHandle(),*owned);if(!removed||::fsync(caller.directoryHandle())!=0){return std::unexpected(E::SourceChanged);}return {};
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
namespace detail {
std::expected<SubscriptionResults,E> readSubscriptionHistory(const storage::ProfileLease& lease,const std::filesystem::path& path,const SavedSubscriptionSelector& selected){
  try{auto access=lease.sourceAccess();if(!access||!access->observationsAllowed()||!directory(lease,path)){return std::unexpected(E::Lease);}for(const auto* suffix:{"",".bak",".saving"}){auto pending=named(lease.directoryHandle(),std::string(PendingWrite)+suffix,28*1024*1024);if(!pending||pending->present){return std::unexpected(E::SourceChanged);}}
    auto config=storage::ConfigFile::load(lease,"subscriptions.config",ConfigLimits);if(!config||config->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(E::SourceChanged);}auto index=parseSubscriptionIndex(config->document().originalBytes());if(!index||selected.sourceIndex>=index->rows.size()){return std::unexpected(E::Malformed);}const auto& row=index->rows[selected.sourceIndex];if(row.selected!=selected||(row.issue&&*row.issue!=SubscriptionValueError::Unsupported)){return std::unexpected(E::Ambiguous);}
    struct stat identity{};auto dir=subscriptionDirectory(lease,identity);if(!dir){return std::unexpected(dir.error());}const auto name=QByteArray::fromStdString(selected.shortId).toHex().toUpper().toStdString()+".results";auto primary=named(dir->get(),name,4*1024*1024),backup=named(dir->get(),name+".bak",4*1024*1024),saving=named(dir->get(),name+".saving",4*1024*1024);if(!primary||!backup||!saving||saving->present){return std::unexpected(E::SourceChanged);}auto values=parseSubscriptionResults(primary->present?primary->bytes:backup->present?backup->bytes:"d7:resultslee");if(!values){return std::unexpected(valueError(values.error()));}return std::move(*values);
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
std::expected<void,E> recoverSubscriptionWrite(const storage::ProfileLease& lease,const std::filesystem::path& path){
  try{auto access=lease.sourceAccess();if(!access||!access->observationsAllowed()||!directory(lease,path)){return std::unexpected(E::Lease);}auto pending=named(lease.directoryHandle(),std::string(PendingWrite),28*1024*1024);if(!pending){return std::unexpected(pending.error());}for(const auto* suffix:{".bak",".saving"}){auto other=named(lease.directoryHandle(),std::string(PendingWrite)+suffix,28*1024*1024);if(!other||other->present){return std::unexpected(E::SourceChanged);}}if(!pending->present){return {};}
    auto intent=parseIntent(pending->bytes);if(!intent){return std::unexpected(intent.error());}struct stat identity{};auto subs=subscriptionDirectory(lease,identity);if(!subs){return std::unexpected(subs.error());}auto mutation=lease.sourceAccess(true);if(!mutation){return std::unexpected(E::Lease);}auto applied=recoverIntent(lease,subs->get(),*intent);if(!applied){return applied;}auto removed=unlinkOwned(lease.directoryHandle(),*pending);if(!removed||::fsync(lease.directoryHandle())!=0){return std::unexpected(E::SourceChanged);}return {};
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
}
} // namespace biglybtc::search
