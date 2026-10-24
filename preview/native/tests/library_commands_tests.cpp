#include "library_commands.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <limits>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
using namespace std::string_literals;
using Action = core::DownloadAction;
using Code = storage::ErrorCode;
std::size_t assertions = 0, families = 0;
void require(bool value, std::string_view message) {
  ++assertions; if (!value) { throw std::runtime_error(std::string(message)); }
}
std::string b(std::string_view value) { return std::to_string(value.size())+":"+std::string(value); }
std::string i(std::int64_t value) { return "i"+std::to_string(value)+"e"; }
std::string hashBytes(char value, std::size_t count = 20) { return std::string(count,value); }
core::TorrentHash hash(char value, std::size_t count = 20) {
  auto parsed = core::TorrentHash::parse(hashBytes(value,count)); require(parsed.has_value(),"fixture hash"); return *parsed;
}
std::string entry(char identity, int state = 70, int forced = 0, std::optional<std::int64_t> position = 1,
    std::string tail = {}, std::size_t width = 20) {
  return "d"+b("torrent_hash")+b(hashBytes(identity,width))+b("torrent")+b("/unopened/torrent")
    +b("save_dir")+b("/unopened/payload")+b("state")+i(state)+b("forceStart")+i(forced)
    +(position ? b("position")+i(*position) : "")+b("future")+"li7e3:rawe"+tail+"e";
}
std::string pause(char identity, bool force = false, int owner = 1, std::string tail = {}) {
  return "d"+b("hash")+b(hashBytes(identity))+b("force")+i(force?1:0)+b("owner")+i(owner)+tail+"e";
}
const std::string Opaque = b("opaque")+"d1:xli-7e3:a\0be"s+"e";
std::string inventory(std::string rows, std::optional<std::string> pauses = {}) {
  return "d"+b("downloads")+"l"+rows+"e"+(pauses ? b("pause_data")+*pauses : "")+Opaque+"e";
}
struct Area {
  std::filesystem::path root;
  std::optional<storage::ProfileLease> lease;
  Area() {
    char pattern[] = "/tmp/biglybtc-library-commands-XXXXXX";
    auto* created = ::mkdtemp(pattern); require(created != nullptr,"owned fixture directory"); root = created;
  }
  ~Area() { lease.reset(); std::error_code error; std::filesystem::remove_all(root,error); }
  Area(const Area&) = delete; Area& operator=(const Area&) = delete;
  void write(std::string_view name, std::string_view value) {
    const auto path = root/std::string(name);
    const int descriptor = ::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    require(descriptor >= 0,"fixture write open");
    std::size_t offset = 0;
    while (offset < value.size()) {
      auto count = ::write(descriptor,value.data()+offset,value.size()-offset);
      if (count < 0 && errno == EINTR) { continue; }
      if (count <= 0) { ::close(descriptor); throw std::runtime_error("fixture write"); }
      offset += static_cast<std::size_t>(count);
    }
    require(::close(descriptor)==0,"fixture close");
  }
  std::string read(std::string_view name = "downloads.config") const {
    const auto path = root/std::string(name); const int descriptor = ::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    require(descriptor >= 0,"fixture read open"); std::string result; std::array<char,4096> buffer{};
    for (;;) {
      auto count = ::read(descriptor,buffer.data(),buffer.size());
      if (count < 0 && errno == EINTR) { continue; }
      if (count < 0) { ::close(descriptor); throw std::runtime_error("fixture read"); }
      if (!count) { break; } result.append(buffer.data(),static_cast<std::size_t>(count));
    }
    require(::close(descriptor)==0,"fixture read close"); return result;
  }
  void acquire() { auto owned = storage::ProfileLease::acquire(root); require(owned.has_value(),"fixture lease"); lease.emplace(std::move(*owned)); }
  storage::LibraryCommandResult command(char target, Action action, std::span<const storage::LibraryCompletionPool> pools = {}, std::size_t width = 20) {
    auto result = storage::applyLibraryCommand(*lease,hash(target,width),action,pools);
    require(result.has_value(),result ? "command" : result.error().message); return std::move(*result);
  }
  core::Document document() const { auto value = core::Document::parse(read(),storage::compatibleConfigLimits()); require(value.has_value(),"readback parse"); return std::move(*value); }
  core::LibrarySnapshot saved() const { auto doc = document(); auto value = core::readLibrary(doc); require(value.has_value(),"readback inventory"); return std::move(*value); }
};
core::SavedDownload saved(const Area& area, char target) {
  auto all = area.saved(); const auto wanted = hash(target);
  for (auto& row : all.downloads) { if (row.hash && *row.hash == wanted) { return std::move(row); } }
  throw std::runtime_error("fixture row missing");
}
std::string rawRoot(const Area& area, std::string_view key) {
  auto document = area.document(); const auto* value = document.find(key); require(value != nullptr,"readback root key");
  return document.originalBytes().substr(value->begin,value->end-value->begin);
}
std::string rawRow(const Area& area, char target) {
  auto document = area.document(); auto all = core::readLibrary(document); require(all.has_value(),"row inventory");
  const auto wanted = hash(target);
  for (const auto& row : all->downloads) { if (row.hash && *row.hash == wanted) {
    const auto& node = all->legacyMap ? document.root().children[row.sourceIndex*2+1] : document.find("downloads")->children[row.sourceIndex];
    return document.originalBytes().substr(node.begin,node.end-node.begin);
  } }
  throw std::runtime_error("raw row missing");
}
struct Stamp { dev_t device; ino_t inode; std::string bytes; };
Stamp stamp(const Area& area) { struct stat info{}; require(::lstat((area.root/"downloads.config").c_str(),&info)==0,"fixture stamp"); return {info.st_dev,info.st_ino,area.read()}; }
void unchanged(const Area& area, const Stamp& old) { const auto next = stamp(area); require(next.device==old.device&&next.inode==old.inode&&next.bytes==old.bytes,"refusal/no-op preserves primary identity and bytes"); }
void denied(Area& area, char target, Action action, Code code, std::span<const storage::LibraryCompletionPool> pools = {}) {
  const auto before = stamp(area); auto result = storage::applyLibraryCommand(*area.lease,hash(target),action,pools);
  require(!result&&result.error().code==code,"expected command refusal"); unchanged(area,before);
  require(!std::filesystem::exists(area.root/"downloads.config.saving"),"refusal leaves no saving file");
}
void test(std::string_view name, const std::function<void()>& body) { body(); ++families; std::cout << "case " << name << '\n'; }
}

int main() {
  try {
    test("start-stop-force-with-opaque-preservation",[] {
      Area area; const auto other = entry('b',75,0,2); area.write("downloads.config",inventory(entry('a')+other)); area.acquire();
      const auto opaque = rawRoot(area,"opaque"); const auto unaffected = rawRow(area,'b');
      auto start = area.command('a',Action::Start); require(start.changed&&start.before.originalState==70&&start.after.originalState==0,"Start saves Waiting");
      require(!start.after.forceStart&&!start.pauseAfter,"Start does not force or pause");
      auto forced = area.command('a',Action::ForceStart); require(forced.after.forceStart&&forced.after.originalState==0,"ForceStart enables");
      const auto beforeNoop = stamp(area); auto repeated = area.command('a',Action::ForceStart); require(!repeated.changed,"ForceStart is not toggle"); unchanged(area,beforeNoop);
      auto stop = area.command('a',Action::Stop); require(stop.after.originalState==70&&!stop.after.forceStart,"Stop clears active force");
      require(rawRoot(area,"opaque")==opaque&&rawRow(area,'b')==unaffected,"unknown subtree and unselected row bytes exact");
      auto target = core::Document::parse(rawRow(area,'a')); require(target.has_value(),"changed row parse");
      const auto* future = target->find("future"); require(future&&target->originalBytes().substr(future->begin,future->end-future->begin)=="li7e3:rawe","changed row unknown value exact");
      require(!std::filesystem::exists(area.root/"active"),"no torrent/cache/payload activation");
    });
    test("force-pause-resume",[] {
      Area area; area.write("downloads.config",inventory(entry('a',0,1))); area.acquire();
      auto paused = area.command('a',Action::Pause); require(paused.after.originalState==70&&!paused.after.forceStart,"active pause stops/clears force");
      require(paused.pauseAfter&&paused.pauseAfter->forceStart&&paused.pauseAfter->owner==1,"pause captures force/general owner");
      auto resumed = area.command('a',Action::Resume); require(resumed.after.forceStart&&resumed.after.originalState==0&&!resumed.pauseAfter,"forced resume restores Waiting/force");
      require(rawRoot(area,"pause_data")=="le","empty pause list no stale owner");
    });
    test("stopped-pause-and-idempotence",[] {
      Area area; const auto owned = pause('a',false,29,b("vendor")+b("raw")); area.write("downloads.config",inventory(entry('a'),"l"+owned+"e")); area.acquire();
      const auto before = stamp(area); auto repeated = area.command('a',Action::Pause);
      require(!repeated.changed&&repeated.pauseAfter&&repeated.pauseAfter->owner==29,"already paused keeps owner"); unchanged(area,before);
      auto resume = area.command('a',Action::Resume); require(resume.after.originalState==75&&!resume.after.forceStart,"ordinary Resume queues");
    });
    test("start-versus-queue-and-forced-resumption",[] {
      for (auto action : {Action::Start,Action::Queue}) { for (bool forced : {false,true}) {
        Area area; area.write("downloads.config",inventory(entry('a'),"l"+pause('a',forced,3)+"e")); area.acquire();
        auto result = area.command('a',action); const auto expected = forced||action==Action::Start ? 0 : 75;
        require(result.after.originalState==expected&&result.after.forceStart==forced&&!result.pauseAfter,"Start/Queue respects saved pause force");
      } }
    });
    test("resume-absent-and-start-queue-active-are-noops",[] {
      Area area; area.write("downloads.config",inventory(entry('a',0,1))); area.acquire();
      const auto before = stamp(area);
      for (auto action : {Action::Resume,Action::Start,Action::Queue}) { require(!area.command('a',action).changed,"inapplicable action no-op"); unchanged(area,before); }
      require(!std::filesystem::exists(area.root/"downloads.config.bak"),"no-op creates no backup");
    });
    test("stop-paused-preserves-other-pause-bytes",[] {
      Area area; const auto other = pause('b',true,71,b("unknown")+"li8ee");
      area.write("downloads.config",inventory(entry('a')+entry('b',70,0,2),"l"+pause('a',true,6)+other+b(hashBytes('c'))+"e")); area.acquire();
      auto stopped = area.command('a',Action::Stop); require(stopped.changed&&!stopped.pauseAfter&&stopped.after.originalState==70,"Stop clears manual pause");
      require(rawRoot(area,"pause_data")=="l"+other+b(hashBytes('c'))+"e","unrelated map/legacy pauses retain raw bytes");
      Area transitional; transitional.write("downloads.config",inventory(entry('a',0,1),"l"+pause('a',true)+"e")); transitional.acquire();
      auto detached = transitional.command('a',Action::Stop); require(detached.after.originalState==0&&detached.after.forceStart&&!detached.pauseAfter,"paused stop is only pause-owner removal");
    });
    test("stopped-force-controller-conditional",[] {
      Area area; area.write("downloads.config",inventory(entry('a',70,1),"l"+pause('a',true)+"e")); area.acquire();
      auto result = area.command('a',Action::Resume); require(result.after.originalState==70&&result.after.forceStart&&!result.pauseAfter,"already-true force does not fabricate controller state change");
    });
    test("legacy-fields-and-root-metadata",[] {
      Area area; const auto row = "d"+b("torrent_hash")+b(hashBytes('a'))+b("torrent")+b("/old/torrent")+b("path")+b("/old/data")
          +b("stopped")+i(1)+b("startStopLocked")+i(0)+b("position")+i(1)+"e";
      area.write("downloads.config","d"+b("old-row")+row+b("meta")+"li-4e3:keyee"); area.acquire();
      auto result = area.command('a',Action::Queue); require(result.before.legacyKey=="old-row"&&result.after.originalState==75,"legacy root Queue");
      require(rawRoot(area,"meta")=="li-4e3:keye","legacy root metadata bytes");
      auto doc = area.document(); const auto* edited = doc.find("old-row"); require(edited&&edited->find("stopped")->integer==0&&edited->find("state")->integer==75,"legacy stopped synchronization");
      result = area.command('a',Action::ForceStart); doc = area.document(); edited = doc.find("old-row");
      require(edited->find("startStopLocked")->integer==1&&edited->find("forceStart")->integer==1,"legacy force synchronization");
      auto paused = area.command('a',Action::Pause); require(paused.pauseAfter&&area.saved().legacyMap,"legacy pause list supported");
    });
    test("moves-keep-completion-pools-separate",[] {
      Area area; area.write("downloads.config",inventory(entry('a',70,0,1)+entry('b',70,0,2)+entry('c',70,0,3)+entry('d',70,0,1)+entry('e',70,0,2))); area.acquire();
      const std::array<storage::LibraryCompletionPool,5> pools{{{hash('a'),false},{hash('b'),false},{hash('c'),false},{hash('d'),true},{hash('e'),true}}};
      const auto complete = rawRow(area,'d')+rawRow(area,'e');
      auto top = area.command('c',Action::MoveTop,pools); require(top.positions.size()==3&&top.before.position==3&&top.after.position==1,"Top shifts own pool");
      require(saved(area,'a').position==2&&saved(area,'b').position==3&&saved(area,'c').position==1,"Top exact positions");
      auto down = area.command('c',Action::MoveDown,pools); require(down.positions.size()==2&&saved(area,'c').position==2&&saved(area,'a').position==1,"Down swaps neighboring rank");
      auto up = area.command('c',Action::MoveUp,pools); require(up.positions.size()==2&&saved(area,'c').position==1,"Up reverses neighbor move");
      area.command('c',Action::MoveBottom,pools); require(saved(area,'a').position==1&&saved(area,'b').position==2&&saved(area,'c').position==3,"Bottom shifts own pool");
      require(rawRow(area,'d')+rawRow(area,'e')==complete,"other completion pool byte-exact");
    });
    test("move-pools-required-exact-and-unique",[] {
      Area area; area.write("downloads.config",inventory(entry('a')+entry('b',70,0,2))); area.acquire();
      denied(area,'a',Action::MoveDown,Code::Unsupported);
      const std::array<storage::LibraryCompletionPool,1> partial{{{hash('a'),false}}}; denied(area,'a',Action::MoveDown,Code::Unsupported,partial);
      const std::array<storage::LibraryCompletionPool,2> duplicate{{{hash('a'),false},{hash('a'),false}}}; denied(area,'a',Action::MoveDown,Code::Conflict,duplicate);
      const std::array<storage::LibraryCompletionPool,2> stale{{{hash('a'),false},{hash('z'),false}}}; denied(area,'a',Action::MoveDown,Code::Unsupported,stale);
    });
    test("malformed-positions-never-guessed-normalized-or-overflowed",[] {
      for (const auto& position : {std::optional<std::int64_t>{},std::optional<std::int64_t>{-1},std::optional<std::int64_t>{0},
          std::optional<std::int64_t>{1},std::optional<std::int64_t>{std::numeric_limits<std::int64_t>::max()}}) {
        Area area; area.write("downloads.config",inventory(entry('a')+entry('b',70,0,position))); area.acquire();
        const std::array<storage::LibraryCompletionPool,2> pools{{{hash('a'),false},{hash('b'),false}}}; denied(area,'a',Action::MoveDown,Code::Unsupported,pools);
      }
    });
    test("move-boundary-noop-and-legacy-pool",[] {
      Area area; area.write("downloads.config","d"+b("a")+entry('a')+b("b")+entry('b',70,0,2)+Opaque+"e"); area.acquire();
      const std::array<storage::LibraryCompletionPool,2> pools{{{hash('a'),false},{hash('b'),false}}}; const auto before = stamp(area);
      require(!area.command('a',Action::MoveTop,pools).changed&&!area.command('a',Action::MoveUp,pools).changed,"boundary move no-op"); unchanged(area,before);
      area.command('a',Action::MoveBottom,pools); require(saved(area,'a').position==2&&saved(area,'b').position==1&&area.saved().legacyMap,"legacy rows reorder without format guessing");
    });
    test("duplicate-stale-and-malformed-identities-refuse",[] {
      for (const auto& rows : {entry('a')+entry('a',75),entry('a')+"i7e",entry('a')+"d7:torrent3:bade"}) {
        Area area; area.write("downloads.config",inventory(rows)); area.acquire();
        denied(area,'a',Action::Start,rows==entry('a')+entry('a',75)?Code::Conflict:Code::InvalidFormat);
      }
      Area area; area.write("downloads.config",inventory(entry('a'))); area.acquire(); denied(area,'z',Action::Stop,Code::Conflict);
    });
    test("malformed-or-duplicate-pause-data-refuses",[] {
      for (const auto& pauses : {"i7e"s,"li2ee"s,"ld4:hash"+b(hashBytes('a'))+"ee","l"+pause('a')+pause('a')+"e"}) {
        Area area; area.write("downloads.config",inventory(entry('a'),pauses)); area.acquire();
        denied(area,'a',Action::Start,pauses=="l"+pause('a')+pause('a')+"e"?Code::Conflict:Code::InvalidFormat);
      }
    });
    test("invalid-action-and-lease-refuse",[] {
      Area area; area.write("downloads.config",inventory(entry('a'))); area.acquire();
      denied(area,'a',static_cast<Action>(999),Code::Unsupported);
      auto other = storage::ProfileLease::acquire(area.root); require(!other&&other.error().code==Code::Busy,"same real profile lease is exclusive");
      auto held = std::move(*area.lease); const auto before = stamp(area);
      auto result = storage::applyLibraryCommand(*area.lease,hash('a'),Action::Start); require(!result&&result.error().code==Code::UnsafePath,"moved lease invalid"); unchanged(area,before);
    });
    test("saving-recovery-refuses-backup-recovery-uses-existing-cas-policy",[] {
      { Area area; const auto bytes = inventory(entry('a')); area.write("downloads.config.saving",bytes); area.acquire();
        auto result = storage::applyLibraryCommand(*area.lease,hash('a'),Action::Start);
        require(!result&&result.error().code==Code::Conflict,"Saving source cannot be promoted by command");
        require(area.read("downloads.config.saving")==bytes&&!std::filesystem::exists(area.root/"downloads.config"),"Saving retained without primary creation"); }
      { Area area; const auto original = inventory(entry('a')); area.write("downloads.config","broken"); area.write("downloads.config.bak",original); area.acquire();
        auto result = area.command('a',Action::Start); require(result.source==storage::RecoverySource::Backup&&saved(area,'a').originalState==0,"existing backup recovery save policy");
        require(area.read("downloads.config.bak")==original,"backup source retained"); }
    });
    test("existing-saving-path-conflict-does-not-overwrite",[] {
      Area area; area.write("downloads.config",inventory(entry('a'))); area.write("downloads.config.saving","foreign"); area.acquire(); const auto before = stamp(area);
      auto result = storage::applyLibraryCommand(*area.lease,hash('a'),Action::Start); require(!result,"existing saving blocks exclusive write");
      unchanged(area,before); require(area.read("downloads.config.saving")=="foreign"&&!std::filesystem::exists(area.root/"downloads.config.bak"),"foreign saving/backup untouched");
    });
    test("v2-hash-and-unrelated-binary-values",[] {
      Area area; area.write("downloads.config",inventory(entry('v',70,0,1,{},32))); area.acquire(); const auto opaque = rawRoot(area,"opaque");
      auto result = area.command('v',Action::Start,{},32); require(result.after.hash&&result.after.hash->bytes().size()==32&&result.after.originalState==0,"full v2 hash matched");
      require(rawRoot(area,"opaque")==opaque,"binary opaque subtree exact");
    });
    test("legacy-advertised-malformed-row-cannot-hide-target",[] {
      Area area; area.write("downloads.config","d"+b("valid")+entry('a')+b("bad")+"d"+b("torrent_hash")+b(hashBytes('a'))+"e"+"e"); area.acquire();
      denied(area,'a',Action::Stop,Code::InvalidFormat);
    });
    std::cout << "PASS " << families << " saved-library command families; " << assertions << " assertions; generated profiles only; no engine actions or Java-runtime equality credit\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
