// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_history_store.hpp"
#include "durable_intake_receipt.hpp"
#include "durable_completion_receipt.hpp"
#include "durable_removal_receipt.hpp"
#include "history_reconstruction_receipt.hpp"
#include "profile_file_snapshot.hpp"
#include <openssl/evp.h>
#include <array>
#include <algorithm>
#include <limits>
#include <new>
#include <set>
#include <thread>

namespace biglybtc::storage {
namespace {
using detail::ProfileFileSnapshot;using detail::StateHandle;using detail::StateStamp;using Clock=std::chrono::steady_clock;
constexpr std::array<const char*,4> Names{"dlhistorya.config","dlhistoryd.config","biglybt.config","BiglyBTC.download-history.config"};
std::unexpected<Error> fail(ErrorCode code,const char* message){return std::unexpected(Error{code,message});}
void require(bool ok,ErrorCode code,const char* message){if(!ok){throw Error{code,message};}}
template<class T>T take(std::expected<T,Error> value){if(!value){throw value.error();}return std::move(*value);}
void take(std::expected<void,Error> value){if(!value){throw value.error();}}
void budget(std::stop_token stop,Clock::time_point end){require(!stop.stop_requested()&&Clock::now()<end,ErrorCode::Cancelled,"History operation cancelled/deadline before publication");}
std::size_t side(DownloadHistorySide value){require(value==DownloadHistorySide::Active||value==DownloadHistorySide::Dead,ErrorCode::InvalidFormat,"Invalid download-history side");return value==DownloadHistorySide::Active?0:1;}
using Input=ProfileFileSnapshot;
struct Hint {std::optional<std::array<std::uint64_t,11>> metadata;std::optional<std::string> bytes;bool operator==(const Hint&)const=default;};
Hint hint(const Input& in){
  Hint out;out.bytes=in.bytes;
  if (in.stamp) {
    const auto& s=*in.stamp;
    out.metadata=std::array<std::uint64_t,11>{s.device,s.inode,static_cast<std::uint64_t>(s.size),
        s.mode,s.owner,s.group,s.links,static_cast<std::uint64_t>(s.modified.tv_sec),
        static_cast<std::uint64_t>(s.modified.tv_nsec),static_cast<std::uint64_t>(s.changed.tv_sec),
        static_cast<std::uint64_t>(s.changed.tv_nsec)};
  }
  return out;
}
Input capture(StateHandle root,std::size_t group,std::size_t candidate){
  const auto name=std::string(Names[group])+(candidate==1?".bak":candidate==2?".saving":"");
  auto input=take(detail::captureProfileFile(root,name,core::Limits{}.inputBytes));
#ifdef _WIN32
  // Windows backup rotation must remain available while another view keeps
  // the verified identity and bytes of a current or former primary.
  if(group<3&&candidate<2){input.releaseWitness();}
#endif
  return input;
}
using Group=std::array<Input,3>;using Inputs=std::array<Group,4>;
bool equal(const Inputs& a,const Inputs& b){for(std::size_t g=0;g<a.size();++g){for(std::size_t n=0;n<3;++n){if(!a[g][n].matches(b[g][n])){return false;}}}return true;}
std::array<std::array<Hint,3>,4> hints(const Inputs& in){std::array<std::array<Hint,3>,4> out;for(std::size_t g=0;g<in.size();++g){for(std::size_t n=0;n<3;++n){out[g][n]=hint(in[g][n]);}}return out;}
Error projectionError(core::SavedDownloadHistoryError code){return {code==core::SavedDownloadHistoryError::Memory?ErrorCode::Io:code==core::SavedDownloadHistoryError::Limit?ErrorCode::Unsupported:ErrorCode::InvalidFormat,"Malformed/ambiguous/bounded download history; preserve for review"};}
core::SavedDownloadHistory project(std::string_view bytes){auto value=core::readSavedDownloadHistory(bytes);if(!value){throw projectionError(value.error());}return std::move(*value);}
struct Loaded {Inputs inputs;std::array<std::optional<ConfigFile>,3> files;std::array<core::SavedDownloadHistory,2> history;bool enabled=true;DownloadHistoryReadOnly review=DownloadHistoryReadOnly::None;};
Loaded load(const ProfileLease& lease){
  Loaded out;
  for(std::size_t g=0;g<4;++g){for(std::size_t n=0;n<3;++n){out.inputs[g][n]=capture(lease.directoryHandle(),g,n);}}
  for(std::size_t g=0;g<3;++g){const auto& primary=out.inputs[g][0];
    if(!primary.bytes){require(!out.inputs[g][1].bytes&&!out.inputs[g][2].bytes,ErrorCode::Conflict,"Missing history/settings primary with preserved recovery candidate");}
    else{
      auto parsed=core::Document::parse(*primary.bytes);require(bool(parsed)&&parsed->root().type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid primary history/settings; no fallback");
      out.files[g]=take(ConfigFile::load(lease,Names[g],core::Limits{}));require(out.files[g]->recoverySource()==RecoverySource::Primary&&out.files[g]->document().originalBytes()==*primary.bytes,ErrorCode::Conflict,"Primary history changed while loading");
    }
    if(out.inputs[g][2].bytes){out.review=DownloadHistoryReadOnly::PendingCandidate;}
  }
  for(std::size_t n=0;n<3;++n){if(out.inputs[3][n].bytes){out.review=DownloadHistoryReadOnly::PendingIntent;}}
  for(std::size_t g=0;g<2;++g){out.history[g]=project(out.inputs[g][0].bytes?std::string_view(*out.inputs[g][0].bytes):std::string_view("de"));}
  if(out.files[2]){if(const auto* enabled=out.files[2]->document().find("Download History Enabled")){require(enabled->type==core::Node::Type::Integer&&(enabled->integer==0||enabled->integer==1),ErrorCode::InvalidFormat,"Invalid original download-history enable flag");out.enabled=enabled->integer==1;}}
  std::set<std::int64_t> ids;for(const auto& row:out.history[0].records){ids.insert(row.uid);}for(const auto& row:out.history[1].records){if(ids.contains(row.uid)){out.review=DownloadHistoryReadOnly::DuplicatePairUid;}}
  for(std::size_t g=0;g<4;++g){for(std::size_t n=0;n<3;++n){const auto now=capture(lease.directoryHandle(),g,n);require(out.inputs[g][n].matches(now),ErrorCode::Conflict,"History source changed during load");}}
  return out;
}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string digest(const std::optional<std::string>& value){if(!value){return "missing";}std::array<unsigned char,32> out{};unsigned size=0;require(EVP_Digest(value->data(),value->size(),out.data(),&size,EVP_sha256(),nullptr)==1&&size==out.size(),ErrorCode::Io,"History intent digest failed");std::string text;for(const auto byte:out){text.push_back("0123456789abcdef"[byte>>4]);text.push_back("0123456789abcdef"[byte&15]);}return text;}
std::string intent(const Loaded& before,const std::array<std::optional<std::string>,2>& after){std::string out="d"+b("purpose")+b("download-history-review-intent-v1");for(std::size_t g=0;g<3;++g){out+=b("before"+std::to_string(g))+b(digest(before.inputs[g][0].bytes));if(g<2){out+=b("after"+std::to_string(g))+b(digest(after[g]?after[g]:before.inputs[g][0].bytes));}}return out+'e';}
}
struct DownloadHistorySnapshot::Data {std::uint64_t generation=0;bool enabled=true;DownloadHistoryReadOnly review=DownloadHistoryReadOnly::None;std::array<bool,2> missing{};std::array<core::SavedDownloadHistory,2> history;std::array<std::array<Hint,3>,4> hints;};
std::uint64_t DownloadHistorySnapshot::generation()const noexcept{return data_?data_->generation:0;}
bool DownloadHistorySnapshot::enabled()const noexcept{return data_&&data_->enabled;}
bool DownloadHistorySnapshot::writable()const noexcept{return data_&&data_->review==DownloadHistoryReadOnly::None;}
bool DownloadHistorySnapshot::missing(DownloadHistorySide value)const noexcept{return data_&&(value==DownloadHistorySide::Active||value==DownloadHistorySide::Dead)&&data_->missing[value==DownloadHistorySide::Active?0:1];}
DownloadHistoryReadOnly DownloadHistorySnapshot::review()const noexcept{return data_?data_->review:DownloadHistoryReadOnly::PendingIntent;}
const core::SavedDownloadHistory* DownloadHistorySnapshot::history(DownloadHistorySide value)const noexcept{return data_&&(value==DownloadHistorySide::Active||value==DownloadHistorySide::Dead)?&data_->history[value==DownloadHistorySide::Active?0:1]:nullptr;}
bool DownloadHistorySnapshot::sameSource(const DownloadHistorySnapshot& other)const noexcept{return data_&&other.data_&&data_->hints==other.data_->hints;}
struct DownloadHistoryStore::Impl {
  std::shared_ptr<const ProfileLease> lease;std::thread::id worker=std::this_thread::get_id();std::filesystem::path path;StateStamp directory{};std::optional<Loaded> basis;std::shared_ptr<const DownloadHistorySnapshot::Data> latest;std::uint64_t generation=0;bool poisoned=false,observing=false;
  void current()const{require(std::this_thread::get_id()==worker,ErrorCode::Unsupported,"History store requires construction worker");require(!observing,ErrorCode::Conflict,"History observer cannot reenter");require(bool(lease),ErrorCode::UnsafePath,"History store is closed");require(!poisoned,ErrorCode::Conflict,"History outcome uncertain; explicit close/open review, never replay");}
  void roots()const {
    (void)take(lease->retain());
    const auto held=take(detail::stateStamp(lease->directoryHandle()));
    const auto named=take(detail::openStatePathDirectory(path));
    const auto observed=take(detail::stateStamp(named.get()));
    require(held.owned&&detail::sameStateDirectory(directory,held)&&detail::sameStateDirectory(held,observed),
        ErrorCode::Conflict,"Named history profile/lock source changed");
  }
  void fresh(const DownloadHistorySnapshot& expected)const{current();require(expected.data_&&latest==expected.data_&&expected.generation()==generation&&basis,ErrorCode::Conflict,"Stale/foreign history snapshot");roots();}
  void publish(Loaded value){require(generation<std::numeric_limits<std::uint64_t>::max(),ErrorCode::Conflict,"History presentation generation exhausted");auto next=std::make_shared<DownloadHistorySnapshot::Data>();next->generation=generation+1;next->enabled=value.enabled;next->review=value.review;next->history=value.history;next->hints=hints(value.inputs);for(std::size_t g=0;g<2;++g){next->missing[g]=!value.inputs[g][0].bytes;}basis.emplace(std::move(value));latest=std::move(next);++generation;}
  void poison()noexcept{poisoned=true;basis.reset();latest.reset();}
};
DownloadHistoryStore::DownloadHistoryStore(std::unique_ptr<Impl> value):impl_(std::move(value)){}
DownloadHistoryStore::~DownloadHistoryStore()=default;
bool DownloadHistoryStore::recoveryRequired()const noexcept{return std::this_thread::get_id()!=impl_->worker||!impl_->lease||impl_->poisoned;}
bool DownloadHistoryStore::sharesOwnership(const ProfileLease& lease)const noexcept{return std::this_thread::get_id()==impl_->worker&&impl_->lease&&impl_->lease->sharesOwnership(lease);}
std::expected<std::unique_ptr<DownloadHistoryStore>,Error> DownloadHistoryStore::open(const ProfileLease& lease,std::stop_token stop,Deadline end){try{budget(stop,end);auto data=std::make_unique<Impl>();data->lease=take(lease.retain());data->path=take(detail::stateDirectoryPath(lease.directoryHandle()));data->directory=take(detail::stateStamp(lease.directoryHandle()));auto out=std::unique_ptr<DownloadHistoryStore>(new DownloadHistoryStore(std::move(data)));take(out->reload(stop,end));return out;}catch(const Error& error){return std::unexpected(error);}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"History allocation failed");}}
std::expected<DownloadHistorySnapshot,Error> DownloadHistoryStore::snapshot()const{try{impl_->current();require(bool(impl_->latest),ErrorCode::Conflict,"History snapshot absent");return DownloadHistorySnapshot(impl_->latest);}catch(const Error& error){return std::unexpected(error);}}
std::expected<DownloadHistorySnapshot,Error> DownloadHistoryStore::reload(std::stop_token stop,Deadline end){try{impl_->current();budget(stop,end);auto access=take(impl_->lease->sourceAccess());impl_->roots();auto loaded=load(*impl_->lease);impl_->roots();budget(stop,end);impl_->publish(std::move(loaded));return DownloadHistorySnapshot(impl_->latest);}catch(const Error& error){return std::unexpected(error);}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"History reload allocation failed");}}
std::expected<DownloadHistoryCommit,Error> DownloadHistoryStore::apply(const DownloadHistorySnapshot& expected,DownloadHistorySide which,const core::SavedDownloadHistoryEdit& edit,std::stop_token stop,Deadline end,DownloadHistoryObserver observer){try{
  impl_->fresh(expected);budget(stop,end);const auto index=side(which);require(!std::holds_alternative<core::DownloadHistoryAppend>(edit),ErrorCode::Unsupported,"Fresh history creation requires actual intake receipt integration");
  const bool deletion=std::holds_alternative<core::DownloadHistoryErase>(edit)||std::holds_alternative<core::DownloadHistoryClear>(edit);require(expected.enabled()||deletion,ErrorCode::Unsupported,"Disabled history does not accept lifecycle updates");
  auto result=core::editSavedDownloadHistory(expected.history(which)->source->originalBytes(),edit);if(!result){throw projectionError(result.error());}std::array<std::optional<std::string>,2> after;if(result->changed){after[index]=result->value.source->originalBytes();}return commit(expected,std::move(after),stop,end,observer);
}catch(const Error& error){return std::unexpected(error);}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"History edit allocation failed");}}
std::expected<DownloadHistoryCommit,Error> DownloadHistoryStore::appendIntake(const DownloadHistorySnapshot& expected,DurableIntakeReceipt receipt,std::stop_token stop,Deadline end){try{
  impl_->fresh(expected);budget(stop,end);
  {auto access=take(impl_->lease->sourceAccess());take(receipt.current(*impl_->lease,true));}
  // Disabled and excluded original flags consume the event without creating
  // history or manufacturing a later startup/backfill event.
  if(!expected.enabled()||!receipt.monitored()){return commit(expected,{},stop,end,nullptr,&receipt);}
  const auto& record=receipt.record();
  for(const auto& existing:expected.history(DownloadHistorySide::Dead)->records){require(existing.uid!=record.uid,ErrorCode::Conflict,"Intake history UID collides with dead history; preserve for review");}
  auto edited=core::editSavedDownloadHistory(expected.history(DownloadHistorySide::Active)->source->originalBytes(),core::DownloadHistoryAppend{record});
  if(!edited){throw projectionError(edited.error());}std::array<std::optional<std::string>,2> after;
  if(edited->changed){after[0]=edited->value.source->originalBytes();}
  return commit(expected,std::move(after),stop,end,nullptr,&receipt);
}catch(const Error& error){return std::unexpected(error);}catch(...){return fail(ErrorCode::Io,"Import remains saved; history intake could not be recorded");}}
std::expected<DownloadHistoryCommit,Error> DownloadHistoryStore::complete(const DownloadHistorySnapshot& expected,DurableCompletionReceipt receipt,std::stop_token stop,Deadline end){try{
  impl_->fresh(expected);budget(stop,end);
  {auto access=take(impl_->lease->sourceAccess());take(receipt.current(*impl_->lease,true));}
  if(!expected.enabled()||!receipt.monitored()){return commit(expected,{},stop,end,nullptr,nullptr,&receipt);}
  const auto& actual=receipt.record();const auto& rows=expected.history(DownloadHistorySide::Active)->records;
  const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.uid==actual.uid;});
  // Original completionChanged only updates a currently active history entry.
  // A removed/disabled/missing history record is never reconstructed here.
  if(found==rows.end()){return commit(expected,{},stop,end,nullptr,nullptr,&receipt);}
  require(found->hash==actual.hash&&found->hashV2==actual.hashV2&&found->addTime==actual.addTime,ErrorCode::Conflict,"Completion UID/full identity/add-time conflicts with active history");
  auto result=core::editSavedDownloadHistory(expected.history(DownloadHistorySide::Active)->source->originalBytes(),core::DownloadHistorySetCompleteTime{actual.uid,actual.completeTime});
  if(!result){throw projectionError(result.error());}std::array<std::optional<std::string>,2> after;if(result->changed){after[0]=result->value.source->originalBytes();}
  return commit(expected,std::move(after),stop,end,nullptr,nullptr,&receipt);
}catch(const Error& error){return std::unexpected(error);}catch(...){return fail(ErrorCode::Io,"Checkpoint remains saved; completion history needs review");}}
std::expected<DownloadHistoryCommit,Error> DownloadHistoryStore::remove(const DownloadHistorySnapshot& expected,DurableRemovalReceipt receipt,std::stop_token stop,Deadline end){try{
  impl_->fresh(expected);budget(stop,end);
  {auto access=take(impl_->lease->sourceAccess());take(receipt.current(*impl_->lease,true));}
  if(!expected.enabled()||!receipt.monitored()){return commit(expected,{},stop,end,nullptr,nullptr,nullptr,nullptr,&receipt);}
  const auto& actual=receipt.record();const auto* active=expected.history(DownloadHistorySide::Active);const auto* dead=expected.history(DownloadHistorySide::Dead);
  const auto found=std::find_if(active->records.begin(),active->records.end(),[&](const auto& row){return row.uid==actual.uid;});
  // Removing a saved torrent is not evidence of a missing history entry.
  if(found==active->records.end()){return commit(expected,{},stop,end,nullptr,nullptr,nullptr,nullptr,&receipt);}
  require(found->hash==actual.hash&&found->hashV2==actual.hashV2&&found->addTime==actual.addTime,ErrorCode::Conflict,"Removal UID/full identity/add-time conflicts with active history");
  require(std::none_of(dead->records.begin(),dead->records.end(),[&](const auto& row){return row.uid==actual.uid;}),ErrorCode::Conflict,"Removal history UID already occurs in dead history");
  // Original setRemoved replaces tags only for a nonempty actual pre-removal
  // capture. Empty/unavailable keeps the existing list and every unknown field.
  const auto removed=core::editSavedDownloadHistory(active->source->originalBytes(),core::DownloadHistorySetRemoved{actual.uid,actual.removeTime,actual.tags});if(!removed){throw projectionError(removed.error());}
  const auto* records=removed->value.source->find("records");require(records&&found->sourceIndex<records->children.size(),ErrorCode::InvalidFormat,"Removed history record unavailable");const auto& row=records->children[found->sourceIndex];
  const auto rawRecord=removed->value.source->originalBytes().substr(row.begin,row.end-row.begin);
  auto next=core::Document::parse(dead->source->originalBytes());require(bool(next),ErrorCode::InvalidFormat,"Invalid dead history source");std::string rows="l";
  if(const auto* existing=next->find("records")){for(const auto& value:existing->children){rows+=next->originalBytes().substr(value.begin,value.end-value.begin);}}
  require(rows.size()<=core::Limits{}.inputBytes&&rawRecord.size()<=core::Limits{}.inputBytes-rows.size(),ErrorCode::Unsupported,"Removed history byte bound");rows+=rawRecord;rows+='e';
  require(bool(next->replaceValue("records",rows)),ErrorCode::InvalidFormat,"Cannot append actual removed record");const auto deadAfter=next->serialize();require(bool(core::readSavedDownloadHistory(deadAfter)),ErrorCode::InvalidFormat,"Removed dead history is ambiguous/bounded");
  const auto erased=core::editSavedDownloadHistory(active->source->originalBytes(),core::DownloadHistoryErase{actual.uid});if(!erased){throw projectionError(erased.error());}
  std::array<std::optional<std::string>,2> after;after[0]=erased->value.source->originalBytes();after[1]=deadAfter;
  // Existing durable intent writes dead first, then removes active. An
  // interrupted duplicate/prefix stays read-only review; there is no replay.
  return commit(expected,std::move(after),stop,end,nullptr,nullptr,nullptr,nullptr,&receipt);
}catch(const Error& error){return std::unexpected(error);}catch(...){return fail(ErrorCode::Io,"Removal remains saved; history move needs review");}}
std::expected<DownloadHistoryCommit,Error> DownloadHistoryStore::reset(const DownloadHistorySnapshot& expected,HistoryReconstructionReceipt receipt,std::stop_token stop,Deadline end){try{
  impl_->fresh(expected);budget(stop,end);require(expected.enabled(),ErrorCode::Unsupported,"Enable history before an explicit Reset");
  {auto access=take(impl_->lease->sourceAccess());take(receipt.current(*impl_->lease,true));}
  const auto* active=expected.history(DownloadHistorySide::Active);std::string rows="l";
  // Preserve unknown root bytes and unknown fields of the SAME actual UID/full
  // hash/add-time. Known reset fields come from actual current backend values;
  // original reset intentionally drops history rows absent from the library.
  for(const auto& record:receipt.records()){
    auto encoded=core::editSavedDownloadHistory("de",core::DownloadHistoryAppend{record});if(!encoded){throw projectionError(encoded.error());}
    const auto& canonical=*encoded->value.source;const auto& row=canonical.find("records")->children.front();std::string extra;
    const auto old=std::find_if(active->records.begin(),active->records.end(),[&](const auto& value){return value.uid==record.uid&&value.hash==record.hash&&value.hashV2==record.hashV2&&value.addTime==record.addTime;});
    if(old!=active->records.end()){
      const auto* oldRows=active->source->find("records");const auto& oldRow=oldRows->children.at(old->sourceIndex);
      for(std::size_t n=0;n+1<oldRow.children.size();n+=2){const auto& key=oldRow.children[n];const auto& value=oldRow.children[n+1];
        if(key.bytes!="u"&&key.bytes!="h"&&key.bytes!="v"&&key.bytes!="n"&&key.bytes!="z"&&key.bytes!="s"&&key.bytes!="a"&&key.bytes!="c"&&key.bytes!="r"&&key.bytes!="t"){
          extra+=active->source->originalBytes().substr(key.begin,value.end-key.begin);
        }
      }
    }
    rows+='d'+extra+canonical.originalBytes().substr(row.begin+1,row.end-row.begin-2)+'e';
    require(rows.size()<=core::Limits{}.inputBytes,ErrorCode::Unsupported,"Reset history output byte bound");
  }
  rows+='e';std::array<std::optional<std::string>,2> after;
  auto next=core::Document::parse(active->source->originalBytes());require(bool(next)&&bool(next->replaceValue("records",rows)),ErrorCode::InvalidFormat,"Reset active projection failed");
  const auto output=next->serialize();require(bool(core::readSavedDownloadHistory(output)),ErrorCode::InvalidFormat,"Reset active projection is ambiguous");if(output!=active->source->originalBytes()){after[0]=output;}
  auto dead=core::editSavedDownloadHistory(expected.history(DownloadHistorySide::Dead)->source->originalBytes(),core::DownloadHistoryClear{});if(!dead){throw projectionError(dead.error());}if(dead->changed){after[1]=dead->value.source->originalBytes();}
  return commit(expected,std::move(after),stop,end,nullptr,nullptr,nullptr,&receipt);
}catch(const Error& error){return std::unexpected(error);}catch(...){return fail(ErrorCode::Io,"History Reset could not be saved; inspect any retained intent");}}
std::expected<DownloadHistoryCommit,Error> DownloadHistoryStore::clear(const DownloadHistorySnapshot& expected,std::stop_token stop,Deadline end,DownloadHistoryObserver observer){try{impl_->fresh(expected);budget(stop,end);std::array<std::optional<std::string>,2> after;for(std::size_t g=0;g<2;++g){const auto value=core::editSavedDownloadHistory(expected.data_->history[g].source->originalBytes(),core::DownloadHistoryClear{});if(!value){throw projectionError(value.error());}if(value->changed){after[g]=value->value.source->originalBytes();}}return commit(expected,std::move(after),stop,end,observer);}catch(const Error& error){return std::unexpected(error);}catch(const std::bad_alloc&){return fail(ErrorCode::Io,"History clear allocation failed");}}
std::expected<DownloadHistoryCommit,Error> DownloadHistoryStore::commit(const DownloadHistorySnapshot& expected,std::array<std::optional<std::string>,2> after,std::stop_token stop,Deadline end,DownloadHistoryObserver observer,const DurableIntakeReceipt* intake,const DurableCompletionReceipt* completion,const HistoryReconstructionReceipt* reconstruction,const DurableRemovalReceipt* removal){bool attempted=false;try{
  impl_->fresh(expected);budget(stop,end);require(impl_->generation<std::numeric_limits<std::uint64_t>::max(),ErrorCode::Conflict,"History generation exhausted");Loaded working;
  // Causal receipt exact-epoch validation and the own write turn are ONE continuous
  // real source section. A cooperative writer cannot enter the old read->write
  // gap and have its epoch drift mistaken for our own ConfigFile increments.
  // The recursive nested writer is valid; this path has no outside observer.
  require(static_cast<unsigned>(bool(intake))+static_cast<unsigned>(bool(completion))+static_cast<unsigned>(bool(reconstruction))+static_cast<unsigned>(bool(removal))<=1,ErrorCode::Conflict,"History commit has conflicting causal receipts");
  const auto receiptCurrent=[&](bool exact){if(intake){take(intake->current(*impl_->lease,exact));}if(completion){take(completion->current(*impl_->lease,exact));}if(reconstruction){take(reconstruction->current(*impl_->lease,exact));}if(removal){take(removal->current(*impl_->lease,exact));}};
  std::optional<ProfileSourceAccess> intakeSource;
  if(intake||completion||reconstruction||removal){require(!observer,ErrorCode::Unsupported,"Causal history has no outside observer");intakeSource.emplace(take(impl_->lease->sourceAccess()));}
  {auto reading=take(impl_->lease->sourceAccess());working=load(*impl_->lease);impl_->roots();if(intake||completion||reconstruction||removal){receiptCurrent(true);}require(equal(impl_->basis->inputs,working.inputs),ErrorCode::Conflict,"History/settings candidates changed since presentation");if(!after[0]&&!after[1]){return DownloadHistoryCommit{expected,false};}}
  require(expected.writable()&&working.review==DownloadHistoryReadOnly::None,ErrorCode::Conflict,"Resolve pending/ambiguous history before editing");
  auto access=take(impl_->lease->sourceAccess(true));require(!observer||access.observationsAllowed(),ErrorCode::Unsupported,"Observed history save requires unnested source boundary");auto verified=load(*impl_->lease);impl_->roots();require(equal(working.inputs,verified.inputs),ErrorCode::Conflict,"History/settings changed before write gate");working=std::move(verified);budget(stop,end);if(intake||completion||reconstruction||removal){receiptCurrent(false);}
  const auto note=intent(working,after);attempted=true;(void)take(ConfigFile::createMissing(*impl_->lease,Names[3],note,core::Limits{}));
  auto next=load(*impl_->lease);for(std::size_t g=0;g<3;++g){for(std::size_t n=0;n<3;++n){require(working.inputs[g][n].matches(next.inputs[g][n]),ErrorCode::Conflict,"History changed during intent publication");}}require(next.inputs[3][0].bytes==note&&!next.inputs[3][1].bytes&&!next.inputs[3][2].bytes,ErrorCode::Conflict,"Owned history intent differs");working=std::move(next);
  auto observe=[&](DownloadHistoryWriteStage stage,std::size_t count){if(observer){impl_->observing=true;try{access.outside([&]{observer(stage,count);});}catch(...){impl_->observing=false;throw;}impl_->observing=false;impl_->roots();auto now=load(*impl_->lease);require(equal(working.inputs,now.inputs),ErrorCode::Conflict,"History source changed across observer");working=std::move(now);}};
  observe(DownloadHistoryWriteStage::IntentDurable,0);std::size_t count=0;
  for(const std::size_t g:{1U,0U}){if(!after[g]){continue;}budget(stop,end);impl_->roots();auto now=load(*impl_->lease);require(equal(working.inputs,now.inputs),ErrorCode::Conflict,"History changed before selected save");working=std::move(now);if(intake||completion||reconstruction||removal){receiptCurrent(false);}
    if(working.files[g]){auto output=core::Document::parse(*after[g]);require(bool(output)&&output->find("records"),ErrorCode::InvalidFormat,"Prepared history records absent");const auto* records=output->find("records");const auto changed=working.files[g]->document().replaceValue("records",output->originalBytes().substr(records->begin,records->end-records->begin));require(bool(changed),ErrorCode::InvalidFormat,"Prepared history replacement failed");take(working.files[g]->save(*impl_->lease));}
    else{(void)take(ConfigFile::createMissing(*impl_->lease,Names[g],*after[g],core::Limits{}));}
    next=load(*impl_->lease);impl_->roots();for(std::size_t other=0;other<4;++other){if(other==g){continue;}for(std::size_t n=0;n<3;++n){require(working.inputs[other][n].matches(next.inputs[other][n]),ErrorCode::Conflict,"Unrelated history/settings/intent changed during save");}}
    require(next.inputs[g][0].bytes==after[g]&&!next.inputs[g][2].bytes,ErrorCode::Conflict,"History postimage not verified");if(working.inputs[g][0].stamp){require(next.inputs[g][1].bytes==working.inputs[g][0].bytes&&next.inputs[g][1].stamp&&detail::sameStateIdentity(*next.inputs[g][1].stamp,*working.inputs[g][0].stamp),ErrorCode::Conflict,"History original preimage backup differs");}
    working=std::move(next);observe(DownloadHistoryWriteStage::FilePublished,++count);
  }
  impl_->roots();next=load(*impl_->lease);require(equal(working.inputs,next.inputs),ErrorCode::Conflict,"History changed before intent completion");
  if(intake||completion||reconstruction||removal){receiptCurrent(false);}
  const auto& marker=next.inputs[3][0];
  require(marker.bytes==note&&marker.stamp&&marker.stillHeld(),ErrorCode::Conflict,"History intent changed before removal");
  const auto directory=impl_->lease->directoryHandle();
  take(detail::removePayloadName(directory,Names[3],*marker.stamp));
  take(detail::syncStateDirectory(directory));
  auto final=load(*impl_->lease);impl_->roots();for(std::size_t g=0;g<3;++g){for(std::size_t n=0;n<3;++n){require(next.inputs[g][n].matches(final.inputs[g][n]),ErrorCode::Conflict,"History changed at final publication");}}require(final.review==DownloadHistoryReadOnly::None,ErrorCode::Conflict,"History remains review-only after publication");impl_->publish(std::move(final));return DownloadHistoryCommit{DownloadHistorySnapshot(impl_->latest),true};
}catch(const Error& error){if(attempted){impl_->poison();}return std::unexpected(error);}catch(const std::bad_alloc&){if(attempted){impl_->poison();}return fail(ErrorCode::Io,"History allocation failed; review any retained intent");}catch(...){if(attempted){impl_->poison();}return fail(ErrorCode::Conflict,"History observer interrupted; preserve intent for review");}}
std::expected<void,Error> DownloadHistoryStore::close(){if(std::this_thread::get_id()!=impl_->worker){return fail(ErrorCode::Unsupported,"History close requires construction worker");}if(impl_->observing){return fail(ErrorCode::Conflict,"History observer cannot close owner");}impl_->basis.reset();impl_->latest.reset();impl_->lease.reset();return {};}
} // namespace biglybtc::storage
