#include "../adapters/torrent_intake.hpp"
#include "../adapters/torrent_state.hpp"
#include "../adapters/tag_manager_startup.hpp"
#include "../adapters/download_history_store.hpp"
#include "../adapters/durable_intake_receipt.hpp"
#include "../adapters/file_operation_io.hpp"
#include "../platform/utf8_path.hpp"
#include "biglybtc/file_policy.hpp"

#include <libtorrent/hasher.hpp>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// Owned generated profiles only. Crash rows terminate only their own child process.
// No engine, Java, live profile, payload allocation or network is involved.
namespace {
using namespace biglybtc;
namespace fs = std::filesystem;
using Code = storage::ErrorCode;
using Stage = storage::TorrentIntakeStage;
using platform::pathToUtf8;
using platform::pathFromUtf8;
std::size_t assertions = 0, families = 0;
void check(bool value, const char* message) { ++assertions; if (!value) { throw std::runtime_error(message); } }
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
template<class Function> void run(std::string_view name, Function&& function) {
  try { function(); ++families; }
  catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
}
std::string b(std::string_view text) { return std::to_string(text.size()) + ':' + std::string(text); }
std::string info(bool multi, char pieces = 'h', std::string part = "second.bin") {
  std::string value = "d";
  if (multi) {
    value += "5:filesld6:lengthi1e4:pathl9:first.bineed6:lengthi1e4:pathl3:Sub" + b(part) + "eee";
  } else { value += "6:lengthi1e"; }
  return value + "4:name" + b(multi ? "bundle" : "sample.bin") + "12:piece lengthi16384e6:pieces" + b(std::string(20, pieces)) + 'e';
}
std::string torrent(bool multi = false, char variant = 'h', std::string part = "second.bin") {
  return "d10:attributesd6:future3:raw10:storetypesl1:Ce8:networksl3:Tore14:resumecompletei2ee"
      "4:info" + info(multi, variant, std::move(part)) + "6:opaque3:" + std::string("\0\xffQ", 3)
      + "6:resumed6:future3:raw4:datad6:futurei9e5:validi1eee" + 'e';
}
core::TorrentHash identity(bool multi = false, char variant = 'h') {
  const auto raw = info(multi, variant);
  const auto digest = libtorrent::hasher(raw.data(), static_cast<int>(raw.size())).final();
  auto hash = core::TorrentHash::parse(std::string_view(digest.data(), 20));
  CHECK(hash); return *hash;
}
void write(const fs::path& path, std::string_view bytes, unsigned mode = 0600) {
#ifdef _WIN32
  (void)mode;
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  CHECK(stream);
#else
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode);
  CHECK(fd >= 0);
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { ::close(fd); throw std::runtime_error("generated fixture write failed"); }
    offset += static_cast<std::size_t>(count);
  }
  CHECK(::close(fd) == 0);
#endif
}
std::string read(const fs::path& path) {
#ifdef _WIN32
  std::ifstream stream(path, std::ios::binary);
  CHECK(stream);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
#else
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  CHECK(fd >= 0);
  std::array<char, 8192> buffer{};
  std::string result;
  for (;;) {
    const auto count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    if (count < 0) { ::close(fd); throw std::runtime_error("generated fixture read failed"); }
    if (count == 0) { break; }
    result.append(buffer.data(), static_cast<std::size_t>(count));
  }
  CHECK(::close(fd) == 0); return result;
#endif
}
core::Document document(const fs::path& path) {
  auto result = core::Document::parse(read(path), storage::compatibleConfigLimits());
  CHECK(result); return std::move(*result);
}
std::string raw(const core::Document& source, const core::Node* value) {
  CHECK(value); return source.originalBytes().substr(value->begin, value->end - value->begin);
}
struct Area {
  fs::path root, profile, metadata, destination;
  std::optional<storage::ProfileLease> lease;
  explicit Area(bool multi = false) {
#ifdef _WIN32
    static unsigned sequence = 0;
    do {
      root = fs::temp_directory_path() /
          pathFromUtf8("BiglyBTC-intake-тест-" + std::to_string(::GetCurrentProcessId()) + '-' + std::to_string(++sequence));
    } while (!fs::create_directory(root));
    root = fs::canonical(root).make_preferred();
#else
    const auto parent = fs::canonical(fs::temp_directory_path());
    auto name = (parent / "biglybtc-torrent-intake-XXXXXX").native();
    const auto* created = ::mkdtemp(name.data()); CHECK(created);
    root = created;
#endif
    profile = root / "profile"; metadata = root / "input.torrent"; destination = root / "new" / "payload";
    CHECK(fs::create_directory(profile));
    write(metadata, torrent(multi)); acquire();
  }
  ~Area() { lease.reset(); std::error_code ignored; fs::remove_all(root, ignored); }
  Area(const Area&) = delete;
  Area& operator=(const Area&) = delete;
  void acquire() { auto result = storage::ProfileLease::acquire(profile); CHECK(result); lease.emplace(std::move(*result)); }
  void reopen() { lease.reset(); acquire(); }
  fs::path state(bool multi = false, char variant = 'h') const { return profile / "active" / (identity(multi, variant).hex() + ".dat"); }
  fs::path archive(bool multi = false, char variant = 'h') const { return profile / "torrents" / (identity(multi, variant).hex() + ".torrent"); }
  void noPayload() const { CHECK(!fs::exists(root / "new")); }
  void noMutation() const {
    noPayload();
    CHECK(!fs::exists(profile / "active") && !fs::exists(profile / "torrents"));
    CHECK(!fs::exists(profile / "BiglyBTC.torrent-import") && !fs::exists(profile / "BiglyBTC.torrent-import.saving"));
  }
  core::LibrarySnapshot saved() const {
    auto source = document(profile / "downloads.config"); auto saved = core::readLibrary(source);
    CHECK(saved && saved->issues.empty() && !saved->legacyMap); return std::move(*saved);
  }
};
Stage crashStage = Stage::JournalSynced;
void crashAt(Stage stage) { if (stage == crashStage) { std::_Exit(37); } }
void importAtPrefix(storage::ProfileLease& lease, const fs::path& input, const fs::path& destination,
    bool stopped, bool swarmTags) {
  if (!swarmTags) {
    const auto result = storage::importTorrent(lease, input, destination, stopped, crashAt);
    if (!result) { std::cerr << result.error().message << '\n'; }
    return;
  }
  const auto source = storage::inspectTorrentIntake(input);
  const auto mode = storage::TagManagerStartupMode::capture(lease);
  CHECK(source && mode && mode->enabled());
  const std::array<compat::FilePolicy,1> chosen{{{false,0}}};
  const auto result = storage::importTorrentWithFileOptions(lease, *source, destination, true, chosen, crashAt,
      false, {}, {}, false, false, false, {}, {}, false, {}, core::ExistingVerificationGoal::FullTorrent,
      core::TorrentAllocationMode::None, storage::TorrentIntakeNetwork::Public, nullptr, &*mode);
  if (!result) { std::cerr << result.error().message << '\n'; }
}

void interruptImport(Area& area, Stage stage, bool stopped = false, bool swarmTags = false) {
#ifdef _WIN32
  std::wstring executable(32768, L'\0');
  const auto length = ::GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  CHECK(length > 0 && length < executable.size());
  executable.resize(length);
  auto command = L"\"" + executable + L"\" --intake-crash " + std::to_wstring(static_cast<unsigned>(stage))
      + (stopped ? L" 1" : L" 0") + (swarmTags ? L" 1" : L" 0") + L" \"" + area.root.native() + L"\"";
  area.lease.reset();
  STARTUPINFOW startup{}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION child{};
  CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
      nullptr, nullptr, &startup, &child));
  ::CloseHandle(child.hThread);
  const auto waited = ::WaitForSingleObject(child.hProcess, 45000);
  if (waited == WAIT_TIMEOUT) {
    ::TerminateProcess(child.hProcess, 120);
    ::WaitForSingleObject(child.hProcess, 5000);
  }
  DWORD status = 0;
  const bool done = waited == WAIT_OBJECT_0 && ::GetExitCodeProcess(child.hProcess, &status);
  ::CloseHandle(child.hProcess);
  CHECK(done && status == 37);
#else
  crashStage = stage;
  const auto child = ::fork(); CHECK(child >= 0);
  if (child == 0) {
    importAtPrefix(*area.lease, area.metadata, area.destination, stopped, swarmTags);
    std::_Exit(90);
  }
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) { throw std::runtime_error("owned child wait failed"); }
  }
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 37);
  area.lease.reset();
#endif
  area.acquire();
}

storage::detail::StateStamp fixtureStamp(const fs::path& path) {
  auto parent = storage::fileop::directory(path.parent_path());
  const auto stamp = storage::detail::stateStampAt(parent.get(), path.filename());
  CHECK(stamp && *stamp);
  return **stamp;
}

void checkPrivateArtifact(const fs::path& path) {
  auto parent = storage::fileop::directory(path.parent_path());
  auto file = storage::detail::openStateWitness(parent.get(), path.filename());
  CHECK(file && storage::fileop::privateEntry(file->get()));
  CHECK(fixtureStamp(path).links == 1);
}
fs::path editPath;
void editInventoryAtJournal(Stage stage) {
  if (stage == Stage::JournalSynced) { write(editPath, "d9:downloadsle6:futurei55ee"); throw std::runtime_error("intentional interruption"); }
}
void editStateAtPublished(Stage stage) {
  if (stage == Stage::StatePublished) { write(editPath, "d6:foreigni1ee"); throw std::runtime_error("intentional interruption"); }
}
} // namespace

namespace {
fs::path acquisitionGuardPath;
Stage acquisitionGuardStage=Stage::JournalSynced;
void introduceAcquisitionGuard(Stage stage){
  if(stage==acquisitionGuardStage){write(acquisitionGuardPath,"unrecognized-acquisition-intent");}
}
int acquisitionPendingGuardRows(){
  try{
    std::size_t cases=0;
    for(const auto* marker:{"BiglyBTC.subscription-acquire","BiglyBTC.subscription-acquire.saving","BiglyBTC.subscription-acquire.bak"}){
      Area area;const auto prepared=storage::inspectTorrentIntake(area.metadata);CHECK(prepared);
      const auto inventory=area.profile/"downloads.config";const auto before=fs::exists(inventory)?std::optional{read(inventory)}:std::nullopt;
      const auto path=area.profile/marker;write(path,"unrecognized-acquisition-intent");
      CHECK(storage::hasPendingTorrentIntake(*area.lease));
      const auto plain=storage::importTorrent(*area.lease,area.metadata,area.destination,true);CHECK(!plain&&plain.error().code==Code::Conflict);
      const std::array<compat::FilePolicy,1> choices{{{false,0}}};
      const auto selected=storage::importTorrentWithFileOptions(*area.lease,*prepared,area.destination,true,choices);CHECK(!selected&&selected.error().code==Code::Conflict);
      const auto alias=storage::importHybridAlias(*area.lease,identity(),storage::OriginalSelectedHash::V2,std::span<const storage::OriginalTorrentIdentity>{});CHECK(!alias&&alias.error().code==Code::Conflict);
      const auto recovered=storage::recoverTorrentIntake(*area.lease);CHECK(!recovered&&recovered.error().code==Code::Conflict);
      CHECK(read(path)=="unrecognized-acquisition-intent"&&!fs::exists(area.profile/"BiglyBTC.torrent-import"));
      CHECK((fs::exists(inventory)?std::optional{read(inventory)}:std::nullopt)==before);
      CHECK(!fs::exists(area.profile/"active")&&!fs::exists(area.profile/"torrents"));area.noPayload();++cases;
    }
    for(const auto stage:{Stage::JournalSynced,Stage::StatePublished,Stage::ArchivePublished,Stage::InventoryPublished}){
      Area area;acquisitionGuardPath=area.profile/"BiglyBTC.subscription-acquire";acquisitionGuardStage=stage;
      const auto imported=storage::importTorrent(*area.lease,area.metadata,area.destination,true,introduceAcquisitionGuard);
      CHECK(!imported&&imported.error().code==Code::Conflict);
      CHECK(read(acquisitionGuardPath)=="unrecognized-acquisition-intent"&&fs::exists(area.profile/"BiglyBTC.torrent-import"));
      const auto journal=read(area.profile/"BiglyBTC.torrent-import");
      CHECK(!storage::recoverTorrentIntake(*area.lease)&&read(area.profile/"BiglyBTC.torrent-import")==journal);
      if(stage==Stage::JournalSynced){CHECK(!fs::exists(area.state())&&!fs::exists(area.archive())&&!fs::exists(area.profile/"downloads.config"));}
      if(stage==Stage::StatePublished){CHECK(fs::exists(area.state())&&!fs::exists(area.archive())&&!fs::exists(area.profile/"downloads.config"));}
      if(stage==Stage::ArchivePublished){CHECK(fs::exists(area.state())&&fs::exists(area.archive())&&!fs::exists(area.profile/"downloads.config"));}
      if(stage==Stage::InventoryPublished){CHECK(area.saved().downloads.size()==1&&fs::exists(area.state())&&fs::exists(area.archive()));}
      area.noPayload();++cases;
    }
    {
      Area area;const auto interrupted=+[](Stage stage){if(stage==Stage::JournalSynced){throw std::runtime_error("Owned marked-journal negative fixture");}};
      CHECK(!storage::importTorrent(*area.lease,area.metadata,area.destination,true,interrupted));
      auto journal=document(area.profile/"BiglyBTC.torrent-import");CHECK(journal.replaceValue("subscriptionAcquisition","de"));
      const auto bytes=journal.serialize();write(area.profile/"BiglyBTC.torrent-import",bytes);
      const auto recovered=storage::recoverTorrentIntake(*area.lease);CHECK(!recovered&&recovered.error().code==Code::Conflict);
      CHECK(read(area.profile/"BiglyBTC.torrent-import")==bytes&&!fs::exists(area.state())&&!fs::exists(area.archive()));
      area.noPayload();++cases;
    }
    CHECK(cases==8);std::cout<<"PASS_ACQUISITION_PENDING_INTAKE_GUARDS cases="<<cases<<'\n';return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
} // namespace
int runIntakeTests(int argc,char** argv) {
  if(argc==2&&std::string_view(argv[1])=="--acquisition-pending-guards"){return acquisitionPendingGuardRows();}
  if(argc==6&&std::string_view(argv[1])=="--prepare-save-name"){
    try{
      const auto request=document(argv[5]);const auto* node=request.find("saveName");
      CHECK(request.root().type==core::Node::Type::Dictionary&&request.root().children.size()==(node?2U:0U));
      std::optional<std::string> name;if(node){CHECK(node->type==core::Node::Type::Bytes);name=node->bytes;}
      auto lease=storage::ProfileLease::acquire(argv[3]);CHECK(lease);const auto source=storage::inspectTorrentIntake(argv[2]);CHECK(source);
      std::vector<compat::FilePolicy> policy;for(const auto& file:source->layout().files){policy.push_back({file.padding,0});}
      const auto result=storage::importTorrentWithFileOptions(*lease,*source,argv[4],true,policy,nullptr,false,{}, {},false,false,false,std::nullopt,std::move(name));CHECK(result);
      std::cout<<result->hex()<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
  }
  if(argc==5&&std::string_view(argv[1])=="--project-trackers"){
    try{
      const auto request=document(argv[3]);const auto* node=request.find("trackers");
      CHECK(request.root().type==core::Node::Type::Dictionary&&request.root().children.size()==(node?2U:0U));
      std::optional<core::TorrentIntakeTrackerTiers> tiers;
      if(node){tiers=core::decodeTorrentIntakeTrackerTiers(*node);CHECK(tiers);}
      const auto source=read(argv[2]);const auto projected=storage::rewriteTorrentIntakeTrackers(source,tiers);
      CHECK(projected);CHECK(!fs::exists(argv[4]));write(argv[4],*projected);
      std::cout<<"PASS production tracker projection; retained source input; no engine or network\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
  }
  if(argc!=1){std::cerr<<"Expected no arguments, --project-trackers INPUT REQUEST OUTPUT, or --prepare-save-name METADATA PROFILE DESTINATION REQUEST\n";return 2;}
  try {
    run("single and multifile import creates only compatible metadata", [] {
      for (const bool multi : {false, true}) {
        Area area(multi);
        const auto original = read(area.metadata);
        const auto inputBefore = fixtureStamp(area.metadata);
        const auto result = storage::importTorrent(*area.lease, area.metadata, area.destination, multi);
        CHECK(result && *result == identity(multi));
        const auto saved = area.saved();
        CHECK(saved.downloads.size() == 1);
        const auto& entry = saved.downloads.front();
        // Original legacy encoding:0 means skipped; -1 decodes to selected
        // normal priority0. Check the published row through the actual shared
        // decoder for BOTH a one-file and a multi-file import.
        const auto inventory = document(area.profile / "downloads.config");
        const auto* rows = inventory.find("downloads"); CHECK(rows && rows->children.size() == 1);
        const auto& imported = rows->children.front();
        const std::size_t files = multi ? 2 : 1;
        const auto policies = compat::originalFilePolicy(imported, files);
        CHECK(policies && policies->size() == files);
        for (const auto& policy : *policies) { CHECK(!policy.skipped && policy.priority == 0); }
        const auto* priorities = imported.find("file_priorities");
        CHECK(priorities && priorities->type == core::Node::Type::List && priorities->children.size() == files);
        for (const auto& priority : priorities->children) {
          CHECK(priority.type == core::Node::Type::Integer && priority.integer == -1);
        }
        CHECK(entry.hash && *entry.hash == *result && pathFromUtf8(entry.saveDirectory) == area.destination && entry.saveFile == (multi ? "bundle" : "sample.bin"));
        CHECK(pathFromUtf8(entry.torrentFile) == area.archive(multi) && entry.persistent && !entry.forceStart);
        CHECK(entry.originalState == (multi ? 70 : 0) && entry.downloaded == 0 && entry.completedBytes == 0);
        CHECK(read(area.archive(multi)) == original && read(area.metadata) == original);
        CHECK(storage::detail::sameStateIdentity(inputBefore, fixtureStamp(area.metadata)));
        checkPrivateArtifact(area.archive(multi));
        checkPrivateArtifact(area.state(multi));
        const auto state = document(area.state(multi));
        const auto source = document(area.metadata);
        CHECK(raw(state, state.find("info")) == raw(source, source.find("info")));
        CHECK(raw(state, state.find("opaque")) == raw(source, source.find("opaque")));
        const auto* attributes = state.find("attributes");
        CHECK(attributes && attributes->find("future")->bytes == "raw");
        CHECK(attributes->find("networks")->children[0].bytes == "Public");
        CHECK(attributes->find("storetypes")->children.size() == (multi ? 2 : 1));
        for (const auto& type : attributes->find("storetypes")->children) { CHECK(type.bytes == "L"); }
        CHECK(attributes->find("resumecomplete")->integer == 1 && attributes->find("filelinks2")->children.empty());
        CHECK(state.find("resume")->find("future")->bytes == "raw");
        CHECK(state.find("resume")->find("data")->find("future")->integer == 9);
        CHECK(state.find("resume")->find("data")->find("valid")->integer == 0);
        auto store = storage::TorrentStateStore::open(*area.lease); CHECK(store);
        auto loaded = store->load(*area.lease, *result); CHECK(loaded && !loaded->recoveredBackup());
        CHECK(!fs::exists(area.profile / "downloads.config.bak"));
        CHECK(!fs::exists(area.profile / "BiglyBTC.torrent-import")); area.noPayload();
      }
    });
    run("history receipt binds the saved UTF-8 location and exact archive identity", [] {
      for(const bool replaceArchive:{false,true}){
        Area area;
        const auto destination=area.root/fs::path(u8"payload-\u03bb");
        storage::IntakeHistoryOutcome outcome;
        const auto imported=storage::importTorrent(*area.lease,area.metadata,destination,true,nullptr,
            false,storage::TorrentIntakeNetwork::Public,&outcome);
        CHECK(imported&&outcome.receipt&&!outcome.warning);
        auto history=storage::DownloadHistoryStore::open(*area.lease);CHECK(history);
        const auto before=(*history)->snapshot();CHECK(before);
        if(replaceArchive){
          const auto saved=read(area.archive());
          fs::rename(area.archive(),area.profile/"old-archive");
          write(area.archive(),saved);
        }
        const auto appended=(*history)->appendIntake(*before,std::move(*outcome.receipt));
        if(replaceArchive){
          CHECK(!appended&&appended.error().code==Code::Conflict);
          CHECK(!fs::exists(area.profile/"dlhistorya.config"));
        }else{
          CHECK(appended&&appended->changed);
          const auto* active=appended->snapshot.history(storage::DownloadHistorySide::Active);
          CHECK(active&&active->records.size()==1);
          CHECK(active->records.front().saveLocation==(destination/"sample.bin"));
        }
        CHECK(!fs::exists(destination));
      }
    });
#ifdef _WIN32
    run("Unicode indexed paths and case-insensitive aliases", [] {
      Area area(true);
      const auto input = storage::inspectTorrentIntake(area.metadata);
      CHECK(input);
      const std::array<compat::FilePolicy, 2> choices{{{false, 0}, {false, 0}}};
      const auto leaf = area.destination / pathFromUtf8("Данные.bin");
      const auto targets = [&](const fs::path& second) {
        return core::TorrentIntakeFileDestinations{{0, pathToUtf8(leaf)}, {1, pathToUtf8(second)}};
      };
      const auto rejected = storage::importTorrentWithFileOptions(*area.lease, *input, area.destination,
          true, choices, nullptr, false, {}, {}, false, false, false, {}, {}, false,
          targets(area.destination / pathFromUtf8("ДАННЫЕ.BIN")));
      CHECK(!rejected && rejected.error().code == Code::Conflict);
      area.noMutation();
      const auto accepted = storage::importTorrentWithFileOptions(*area.lease, *input, area.destination,
          true, choices, nullptr, false, {}, {}, false, false, false, {}, {}, false,
          targets(area.destination / pathFromUtf8("Второй.bin")));
      CHECK(accepted && *accepted == identity(true));
      CHECK(read(area.state(true)).find(pathToUtf8(leaf)) != std::string::npos);
      area.noPayload();
    });
#endif
    run("append preserves original opaque rows roots pauses modes and backup", [] {
      Area area;
      const auto oldRow = "d12:torrent_hash20:" + std::string(20, 'x') + "7:torrent9:/unopened8:save_dir9:/unopened5:statei70e8:positioni3e6:futureli7e3:rawee";
      const std::string before = "d9:downloadsl" + oldRow + "e6:opaque3:" + std::string("\0\xffX", 3) + "10:pause_datalee";
      write(area.profile / "downloads.config", before, 0640);
      const auto old = document(area.profile / "downloads.config");
      CHECK(storage::importTorrent(*area.lease, area.metadata, area.destination, false));
      const auto after = document(area.profile / "downloads.config");
      CHECK(raw(after, &after.find("downloads")->children.front()) == raw(old, &old.find("downloads")->children.front()));
      CHECK(raw(after, after.find("opaque")) == raw(old, old.find("opaque")));
      CHECK(raw(after, after.find("pause_data")) == raw(old, old.find("pause_data")));
      CHECK(read(area.profile / "downloads.config.bak") == before);
#ifndef _WIN32
      CHECK((fixtureStamp(area.profile / "downloads.config").mode & 0777) == 0640);
#endif
      CHECK(area.saved().downloads.back().position == 4); area.noPayload();
    });
    run("duplicate imports are unchanged conflicts", [] {
      Area area;
      CHECK(storage::importTorrent(*area.lease, area.metadata, area.destination, false));
      const auto before = read(area.profile / "downloads.config");
      const auto state = read(area.state());
      const auto again = storage::importTorrent(*area.lease, area.metadata, area.destination, true);
      CHECK(!again && again.error().code == Code::Conflict);
      CHECK(read(area.profile / "downloads.config") == before && read(area.state()) == state);
      CHECK(!fs::exists(area.profile / "BiglyBTC.torrent-import")); area.noPayload();
    });
    run("all four durable interruption stages recover on a fresh lease", [] {
      for (const auto stage : {Stage::JournalSynced, Stage::StatePublished, Stage::ArchivePublished, Stage::InventoryPublished}) {
        Area area;
        const std::string before = "d9:downloadslee";
        write(area.profile / "downloads.config", before);
        interruptImport(area, stage);
        CHECK(fs::exists(area.profile / "BiglyBTC.torrent-import"));
        if (stage != Stage::InventoryPublished) { CHECK(read(area.profile / "downloads.config") == before); }
        area.reopen();
        const auto recovered = storage::recoverTorrentIntake(*area.lease);
        CHECK(recovered && recovered->has_value() && **recovered == identity());
        CHECK(area.saved().downloads.size() == 1);
        CHECK(read(area.profile / "downloads.config.bak") == before);
        CHECK(read(area.archive()) == read(area.metadata));
        CHECK(!fs::exists(area.profile / "BiglyBTC.torrent-import"));
        const auto idle = storage::recoverTorrentIntake(*area.lease); CHECK(idle && !idle->has_value()); area.noPayload();
      }
    });
    run("concurrent inventory change blocks recovery without overwriting", [] {
      Area area;
      write(area.profile / "downloads.config", "d9:downloadslee");
      editPath = area.profile / "downloads.config";
      CHECK(!storage::importTorrent(*area.lease, area.metadata, area.destination, false, editInventoryAtJournal));
      const auto foreign = read(editPath);
      area.reopen(); const auto result = storage::recoverTorrentIntake(*area.lease);
      CHECK(!result && result.error().code == Code::Conflict && read(editPath) == foreign);
      CHECK(fs::exists(area.profile / "BiglyBTC.torrent-import") && !fs::exists(area.profile / "active")); area.noPayload();
    });
    run("changed published state never admits a library row", [] {
      Area area;
      write(area.profile / "downloads.config", "d9:downloadslee"); editPath = area.state();
      CHECK(!storage::importTorrent(*area.lease, area.metadata, area.destination, false, editStateAtPublished));
      const auto foreign = read(editPath);
      const auto result = storage::recoverTorrentIntake(*area.lease);
      CHECK(!result && result.error().code == Code::Conflict);
      CHECK(read(editPath) == foreign && area.saved().downloads.empty()); area.noPayload();
    });
    run("invalid metadata paths hash overrides and ambiguous info reject before journal", [] {
      for (int kind = 0; kind < 4; ++kind) {
        Area area;
        if (kind == 0) { write(area.metadata, "d4:infodee"); }
        if (kind == 1) { write(area.metadata, torrent(true, 'h', "..")); }
        if (kind == 2) {
          auto data = torrent(); const auto at = data.find("d6:length"); CHECK(at != std::string::npos);
          data.insert(at + 1, "13:hash-override20:" + std::string(20, 'x')); write(area.metadata, data);
        }
        if (kind == 3) {
          auto data = torrent(); const auto at = data.find("6:lengthi1e"); CHECK(at != std::string::npos);
          data.insert(at, "6:lengthi2e"); write(area.metadata, data);
        }
        CHECK(!storage::importTorrent(*area.lease, area.metadata, area.destination, false)); area.noMutation();
      }
    });
    run("unsafe metadata and destination are refused", [] {
      Area area;
      const auto target = read(area.metadata);
      const auto link = area.root / "linked.torrent";
#ifdef _WIN32
      fs::create_hard_link(area.metadata, link);
#else
      CHECK(::symlink(area.metadata.c_str(), link.c_str()) == 0);
#endif
      CHECK(!storage::importTorrent(*area.lease, link, area.destination, false)); area.noMutation();
      fs::remove(link);
#ifdef _WIN32
      write(area.root / "dest-link", "not a directory");
#else
      CHECK(::symlink(area.root.c_str(), (area.root / "dest-link").c_str()) == 0);
#endif
      CHECK(!storage::importTorrent(*area.lease, area.metadata, area.root / "dest-link" / "payload", false)); area.noMutation();
      CHECK(read(area.metadata) == target);
    });
    run("existing state candidates remain untouched", [] {
      for (int kind = 0; kind < 4; ++kind) {
#ifdef _WIN32
        if (kind == 2) { continue; } // The FIFO row exercises POSIX only.
#endif
        Area area; CHECK(fs::create_directory(area.profile / "active"));
        const auto candidate = area.state();
        if (kind == 0) { write(candidate, "foreign"); }
#ifdef _WIN32
        if (kind == 1) {
          const auto foreign = area.root / "foreign-state";
          write(foreign, "linked state");
          fs::create_hard_link(foreign, candidate);
        }
#else
        if (kind == 1) { CHECK(::symlink(area.metadata.c_str(), candidate.c_str()) == 0); }
        if (kind == 2) { CHECK(::mkfifo(candidate.c_str(), 0600) == 0); }
#endif
        if (kind == 3) { CHECK(fs::create_directory(candidate)); }
        const auto before = fixtureStamp(candidate);
        const auto result = storage::importTorrent(*area.lease, area.metadata, area.destination, false);
        CHECK(!result && result.error().code == Code::Conflict);
        CHECK(storage::detail::sameStateSnapshot(before, fixtureStamp(candidate)));
        CHECK(!fs::exists(area.profile / "downloads.config") && !fs::exists(area.profile / "BiglyBTC.torrent-import"));
        if (kind == 0) { CHECK(read(candidate) == "foreign"); } area.noPayload();
      }
    });
    run("recovery candidates and malformed library are not promoted", [] {
      for (int kind = 0; kind < 3; ++kind) {
        Area area;
        const auto candidate = area.profile / (kind == 0 ? "downloads.config.bak" : kind == 1 ? "downloads.config.saving" : "downloads.config");
        write(candidate, kind == 2 ? "d9:downloadsi1ee" : "d9:downloadslee");
        const auto before = read(candidate);
        CHECK(!storage::importTorrent(*area.lease, area.metadata, area.destination, false));
        CHECK(read(candidate) == before); area.noMutation();
      }
    });
    run("no transaction and invalid lease are bounded", [] {
      Area area;
      const auto idle = storage::recoverTorrentIntake(*area.lease);
      CHECK(idle && !idle->has_value());
      auto moved = std::move(*area.lease);
      const auto result = storage::importTorrent(*area.lease, area.metadata, area.destination, false);
      CHECK(!result && result.error().code == Code::UnsafePath); area.noMutation();
      (void)moved;
    });
    run("force start is part of the same journaled row", [] {
      Area area;
      const auto imported = storage::importTorrent(*area.lease, area.metadata, area.destination, true, nullptr, true);
      CHECK(imported && *imported == identity());
      const auto saved = area.saved();
      CHECK(saved.downloads.size() == 1 && saved.downloads.front().forceStart);
      CHECK(saved.downloads.front().originalState == 0 && saved.downloads.front().startup == core::StartupState::Waiting);
      area.noPayload();
    });
    run("old version-one journal defaults missing force to false and rejects unknown extras", [] {
      for (int kind = 0; kind < 3; ++kind) {
        Area area;
        interruptImport(area, Stage::JournalSynced);
        const auto path = area.profile / "BiglyBTC.torrent-import";
        auto journal = read(path);
        const std::string field = "10:forceStarti0e";
        const auto position = journal.find(field); CHECK(position != std::string::npos);
        if (kind < 2) { journal.erase(position, field.size()); }
        if (kind == 1) { journal.insert(journal.size() - 1, "6:futurei1e"); }
        if (kind == 2) { journal.replace(position, field.size(), "10:forceStarti2e"); }
        write(path, journal);
        area.reopen(); const auto result = storage::recoverTorrentIntake(*area.lease);
        if (kind != 0) {
          CHECK(!result && read(path) == journal && !fs::exists(area.profile / "downloads.config"));
        } else {
          CHECK(result && result->has_value());
          CHECK(area.saved().downloads.size() == 1 && !area.saved().downloads.front().forceStart);
        }
        area.noPayload();
      }
    });
    run("reviewed Swarm Tags retain the accepted metadata across all interruption stages", [] {
      for (const auto stage : {Stage::JournalSynced,Stage::StatePublished,Stage::ArchivePublished,Stage::InventoryPublished}) {
        Area area;
        write(area.profile/"biglybt.config","d6:locale5:en_GB17:tagmanager.enablei1ee");
        auto input = document(area.metadata);
        CHECK(input.replaceValue("azureus_private_properties","d9:tag_cachel"
            + b("First") + b("first") + b("_internal_") + b("Café") + "ee"));
        write(area.metadata,input.serialize());
        interruptImport(area, stage, true, true);
        const auto journal = document(area.profile/"BiglyBTC.torrent-import");
        CHECK(journal.find("cachedSwarmTags") && journal.find("cachedSwarmTags")->integer==1);
        write(area.profile/"biglybt.config","d6:locale5:en_GB17:tagmanager.enablei0ee");
        area.reopen();
        const auto result = storage::recoverTorrentIntake(*area.lease);
        CHECK(result && result->has_value() && **result==identity());
        const auto saved = document(area.state());
        const auto* list = saved.find("attributes")->find("stag");
        CHECK(list && list->type==core::Node::Type::List && list->children.size()==2);
        CHECK(list->children[0].bytes=="First" && list->children[1].bytes=="Café");
        CHECK(read(area.archive())==read(area.metadata) && area.saved().downloads.size()==1);
        const auto once = saved.originalBytes();
        const auto idle = storage::recoverTorrentIntake(*area.lease);
        CHECK(idle && !idle->has_value() && read(area.state())==once);
        area.noPayload();
      }
    });
    run("original added time is frozen once in accepted journal and recovery", [] {
      Area area; interruptImport(area, Stage::JournalSynced, true);
      const auto journal=document(area.profile/"BiglyBTC.torrent-import");const auto* frozen=journal.find("addedTime");CHECK(frozen&&frozen->type==core::Node::Type::Integer&&frozen->integer>0);const auto added=frozen->integer;
      area.reopen();const auto recovered=storage::recoverTorrentIntake(*area.lease);CHECK(recovered&&recovered->has_value());
      const auto state=document(area.state());const auto* parameters=state.find("attributes")->find("parameters");CHECK(parameters&&parameters->find("stats.download.added.time")->integer==added);
      const auto stable=read(area.state());CHECK(storage::recoverTorrentIntake(*area.lease));CHECK(read(area.state())==stable);area.noPayload();
    });
    std::cout << families << " torrent-intake families, " << assertions << " assertions passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  if (argc == 6 && std::wstring_view(argv[1]) == L"--intake-crash") {
    try {
      const auto rawStage = std::wstring_view(argv[2]);
      if (rawStage.size() != 1 || rawStage[0] < L'0' || rawStage[0] > L'3') { return 123; }
      crashStage = static_cast<Stage>(rawStage[0] - L'0');
      const fs::path root(argv[5]);
      auto lease = storage::ProfileLease::acquire(root / "profile");
      CHECK(lease);
      importAtPrefix(*lease, root / "input.torrent", root / "new" / "payload",
          std::wstring_view(argv[3]) == L"1", std::wstring_view(argv[4]) == L"1");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 90;
  }
  std::vector<std::string> args;
  for (int n = 0; n < argc; ++n) { args.push_back(pathToUtf8(fs::path(argv[n]))); }
  std::vector<char*> pointers;
  for (auto& arg : args) { pointers.push_back(arg.data()); }
  return runIntakeTests(argc, pointers.data());
}
#else
int main(int argc, char** argv) { return runIntakeTests(argc, argv); }
#endif
