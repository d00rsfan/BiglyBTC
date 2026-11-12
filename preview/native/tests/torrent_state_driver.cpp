#include "torrent_state.hpp"
#include "state_cache_materialization.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace biglybtc;
using namespace biglybtc::storage;

namespace {
StateSaveStage crashStage = StateSaveStage::TemporarySynced;
bool crash = false;
bool pendingAtStage = false, pendingCreated = false;
struct stat pendingIdentity{};
std::filesystem::path pendingMarker;
constexpr std::string_view PendingBytes = "unknown acquisition fixture; preserve exactly\n";
bool putPending(const std::filesystem::path& marker) {
  std::ofstream file(marker, std::ios::binary); file << PendingBytes; file.flush(); return bool(file);
}
bool sameStamp(const std::filesystem::path& path, const struct stat& before) {
  struct stat after{};
  return ::lstat(path.c_str(), &after) == 0 && before.st_dev == after.st_dev && before.st_ino == after.st_ino
      && before.st_mode == after.st_mode && before.st_nlink == after.st_nlink && before.st_size == after.st_size
      && before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
      && before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
}
template<class F> int deniedStateWrite(const std::filesystem::path& profile, const core::TorrentHash& hash, F&& write) {
  const auto marker=profile/"BiglyBTC.subscription-acquire", primary=profile/"active"/(hash.hex()+".dat"), backup=std::filesystem::path(primary.string()+".bak");
  if (!putPending(marker)) { return 4; }
  struct stat oldMarker{}, oldPrimary{}, oldBackup{};
  if (::lstat(marker.c_str(), &oldMarker) || ::lstat(primary.c_str(), &oldPrimary) || ::lstat(backup.c_str(), &oldBackup)) { return 4; }
  const auto blocked=write();
  return !blocked && blocked.error().code==ErrorCode::Conflict && sameStamp(marker,oldMarker)
      && sameStamp(primary,oldPrimary) && sameStamp(backup,oldBackup) ? 0 : 4;
}
void observer(StateSaveStage stage) {
  if (pendingAtStage && stage == crashStage) { pendingCreated=putPending(pendingMarker) && ::lstat(pendingMarker.c_str(),&pendingIdentity)==0; }
  if (crash && stage == crashStage) { ::_exit(66); }
}
int failed(const Error& value) {
  std::cerr << value.message << '\n';
  return value.code == ErrorCode::Busy ? 3 : 2;
}
}

int main(int argc, char** argv) {
  if (argc < 4) { return 1; }
  const std::string_view identity(argv[2]);
  if (identity.size() != 40 && identity.size() != 64) { return 1; }
  std::string raw;
  for (std::size_t i = 0; i < identity.size(); i += 2) {
    unsigned value = 0;
    const auto decoded = std::from_chars(identity.data() + i, identity.data() + i + 2, value, 16);
    if (decoded.ec != std::errc{} || decoded.ptr != identity.data() + i + 2) { return 1; }
    raw += static_cast<char>(value);
  }
  auto hash = core::TorrentHash::parse(raw);
  if (!hash) { return 1; }
  auto lease = ProfileLease::acquire(argv[1]);
  if (!lease) { return failed(lease.error()); }
  auto store = TorrentStateStore::open(*lease);
  if (!store) { return failed(store.error()); }
  const std::string_view command(argv[3]);
  if(command=="materialize"||command=="materialize-cancel"||command=="materialize-crash"||command=="materialize-conflict"||command=="materialize-pending"){
    std::stop_source stop;bool observed=false;
    const auto result=materializeStateCache(*lease,stop.get_token(),[&](std::size_t done,std::size_t){
      if(done!=1){return;}observed=true;
      if(command=="materialize-crash"){::_exit(66);}
      if(command=="materialize-cancel"){stop.request_stop();}
      if(command=="materialize-pending"){static_cast<void>(putPending(std::filesystem::path(argv[1])/"BiglyBTC.subscription-acquire"));}
      if(command=="materialize-conflict"){std::ofstream file(std::filesystem::path(argv[1])/"active/cache.dat",std::ios::binary|std::ios::app);file<<'!';file.flush();}
    });
    if(!result){
      if(observed&&((command=="materialize-cancel"&&result.error().code==ErrorCode::Cancelled)
          ||((command=="materialize-conflict"||command=="materialize-pending")&&result.error().code==ErrorCode::Conflict))){std::cout<<"PRESERVED_CACHE_ON_INTERRUPTION\n";return 0;}
      return failed(result.error());
    }
    if(command!="materialize"){return 4;}
    std::cout<<"examined="<<result->examined<<" hydrated="<<result->hydrated<<" state_writes="<<result->stateWrites<<" cache_writes="<<result->cacheWrites<<" retained="<<result->retained<<'\n';return 0;
  }
  if (command == "load") {
    auto state = store->load(*lease, *hash);
    if (!state) { return failed(state.error()); }
    std::cout << "LOADED " << state->recoveredBackup() << '\n';
    const auto blocked = state->replaceSection("attributes", "de");
    return !blocked && blocked.error().code == ErrorCode::Conflict ? 0 : 4;
  }
  if (command == "acquisition-blocked-begin") {
    const auto blocked=store->beginUpdate(*lease,*hash);
    return !blocked && blocked.error().code==ErrorCode::Conflict ? 0 : 4;
  }
  if (command == "acquisition-attributes") {
    auto attributes=store->loadAttributes(*lease,*hash);if(!attributes){return failed(attributes.error());}
    return deniedStateWrite(argv[1],*hash,[&]{return store->saveAttributes(*lease,*attributes,"d14:native.fixturei7ee");});
  }
  if(command=="resume-stage-abandon"||command=="resume-stage-save"){
    auto staged=store->stageUpdate(*lease,*hash);if(!staged){return failed(staged.error());}
    if(command=="resume-stage-abandon"){return 0;}
    if(argc!=5){return 1;}
    const auto section=[&](const core::Node* node){return node?core::encodeValue(*node,compatibleConfigLimits()):std::expected<std::string,core::ParseError>{std::string{"de"}};};
    auto r=section(staged->document().find("resume"));if(!r){return 4;}auto resume=core::Document::parse(*r,compatibleConfigLimits());if(!resume){return 4;}
    auto d=section(resume->find("data"));if(!d){return 4;}auto data=core::Document::parse(*d,compatibleConfigLimits());if(!data){return 4;}
    const auto* old=data->find("resume data");if(!old||old->type!=core::Node::Type::Bytes){return 4;}
    std::string pieces(old->bytes);bool complete=true;for(auto& piece:pieces){if(piece==0){piece=2;complete=false;}}
    if(!data->replaceValue("resume data",std::to_string(pieces.size())+":"+pieces)||!data->replaceValue("valid",complete?"i1e":"i0e")||!resume->replaceValue("data",data->serialize())||!staged->replaceSection("resume",resume->serialize())){return 4;}
    const std::string_view stage(argv[4]);crash=stage!="normal";crashStage=stage=="temp"?StateSaveStage::TemporarySynced:stage=="backup"?StateSaveStage::BackupSynced:StateSaveStage::PrimaryRenamed;
    auto saved=store->save(*lease,*staged,observer,"managed-prepare");return saved?0:failed(saved.error());
  }
  auto state = store->beginUpdate(*lease, *hash);
  if (!state) { return failed(state.error()); }
  if (command == "begin") { return 0; }
  if (command == "immutable") {
    const auto blocked = state->replaceSection("info", "de");
    return !blocked && blocked.error().code == ErrorCode::Unsupported ? 0 : 4;
  }
  if (command == "stale") {
    std::ofstream(std::filesystem::path(argv[1]) / "active" / (hash->hex() + ".dat")) << "de";
    const auto blocked = store->save(*lease, *state);
    return !blocked && blocked.error().code == ErrorCode::Conflict ? 0 : 4;
  }
  if (command == "other-profile" && argc == 5) {
    auto other = ProfileLease::acquire(argv[4]);
    if (!other) { return failed(other.error()); }
    auto otherStore = TorrentStateStore::open(*other);
    if (!otherStore) { return failed(otherStore.error()); }
    const auto blocked = otherStore->save(*other, *state);
    return !blocked && blocked.error().code == ErrorCode::UnsafePath ? 0 : 4;
  }
  const bool acquisitionSave=command=="acquisition-save";
  if ((!acquisitionSave && command != "edit") || argc != (acquisitionSave ? 4 : 5)) { return 1; }
  const auto* original = state->document().find("attributes");
  if (!original) { return 4; }
  auto encoded = core::encodeValue(*original, compatibleConfigLimits());
  if (!encoded) { return 4; }
  auto attributes = core::Document::parse(std::move(*encoded), compatibleConfigLimits());
  if (!attributes || !attributes->replaceValue("native.fixture", "i7e")) { return 4; }
  auto edited = state->replaceSection("attributes", attributes->serialize());
  if (!edited) { return failed(edited.error()); }
  if (acquisitionSave) {
    return deniedStateWrite(argv[1],*hash,[&]{return store->save(*lease,*state);});
  }
  const std::string_view stage(argv[4]);
  pendingAtStage=stage.starts_with("acquisition-");
  pendingMarker=std::filesystem::path(argv[1])/"BiglyBTC.subscription-acquire";
  const auto phase=pendingAtStage ? stage.substr(12) : stage;
  crash = !pendingAtStage && stage != "normal";
  crashStage = phase == "temp" ? StateSaveStage::TemporarySynced
      : phase == "backup" ? StateSaveStage::BackupSynced : StateSaveStage::PrimaryRenamed;
  const auto saved = store->save(*lease, *state, observer);
  if (pendingAtStage) { return pendingCreated && sameStamp(pendingMarker,pendingIdentity) && !saved && saved.error().code==ErrorCode::Conflict ? 0 : 4; }
  return saved ? 0 : failed(saved.error());
}
