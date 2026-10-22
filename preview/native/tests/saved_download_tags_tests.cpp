#include "biglybtc/saved_download_tags.hpp"
#include "saved_download_tags_view.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
std::size_t assertions = 0, cases = 0;
void check(bool value, const char* message) { ++assertions; if (!value) { throw std::runtime_error(message); } }
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string list(std::initializer_list<std::string> values) {
  std::string result = "l"; for (const auto& value : values) { result += value; } return result + 'e';
}
std::string dictionary(std::initializer_list<std::pair<std::string, std::string>> fields) {
  std::vector<std::pair<std::string, std::string>> sorted(fields);
  std::sort(sorted.begin(), sorted.end()); std::string result = "d";
  for (const auto& [key, value] : sorted) { result += bytes(key) + value; } return result + 'e';
}
constexpr std::string_view Hash20 = "AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQT";
constexpr std::string_view Hash32 = "AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQTCQKRMFYYDENBWHA5DYPQ";
std::string fixture() {
  return dictionary({{"3", dictionary({{"1", dictionary({{"n",bytes("Movies")},
      {"o",list({bytes(Hash20),bytes(Hash32)})},{"p",list({dictionary({{"a","i123e"}}),"de"})}})},
      {"c",dictionary({{"unknown",bytes("keep")}})}})}, {"unknown-root",bytes("keep-root")}});
}
core::SavedDownloadTags parsed(std::string_view raw, core::SavedTagLimits limits = {}) {
  auto result = core::readSavedDownloadTags(raw, limits); check(bool(result), "expected saved tags projection"); return std::move(*result);
}
void error(std::string_view raw, core::SavedTagError expected, core::SavedTagLimits limits = {}) {
  auto result = core::readSavedDownloadTags(raw, limits); check(!result && result.error() == expected, "unexpected saved tags error");
}
struct Area {
  std::filesystem::path root;
  std::optional<storage::ProfileLease> lease;
  Area() {
    std::string pattern = "/tmp/biglybtc-saved-download-tags-XXXXXX";
    const auto* created = ::mkdtemp(pattern.data()); check(created != nullptr, "mkdtemp failed"); root = created;
    check(::chmod(root.c_str(),0700) == 0, "private generated profile mode failed");
    auto held = storage::ProfileLease::acquire(root); check(bool(held), "generated lease failed"); lease.emplace(std::move(*held));
  }
  ~Area() { lease.reset(); std::error_code ignored; std::filesystem::remove_all(root, ignored); }
  void write(const char* name, std::string_view raw) {
    const int descriptor = ::open((root/name).c_str(),O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
    check(descriptor >= 0, "generated fixture creation failed");
    struct Close { int fd; ~Close() { (void)::close(fd); } } close{descriptor};
    std::size_t offset = 0;
    while (offset < raw.size()) {
      const auto count = ::write(descriptor,raw.data()+offset,raw.size()-offset);
      if (count < 0 && errno == EINTR) { continue; }
      check(count > 0, "generated fixture write failed"); offset += static_cast<std::size_t>(count);
    }
  }
};
struct Stamp { dev_t device; ino_t inode; off_t size; mode_t mode; nlink_t links; timespec modified, changed; };
Stamp stamp(const std::filesystem::path& path) {
  struct stat value{}; check(::lstat(path.c_str(),&value) == 0, "fixture stat failed");
  return {value.st_dev,value.st_ino,value.st_size,value.st_mode,value.st_nlink,value.st_mtim,value.st_ctim};
}
bool same(const Stamp& a, const Stamp& b) {
  return a.device==b.device && a.inode==b.inode && a.size==b.size && a.mode==b.mode && a.links==b.links
      && a.modified.tv_sec==b.modified.tv_sec && a.modified.tv_nsec==b.modified.tv_nsec
      && a.changed.tv_sec==b.changed.tv_sec && a.changed.tv_nsec==b.changed.tv_nsec;
}
auto loaded(const Area& area) {
  auto result = storage::readSavedDownloadTagsView(*area.lease); check(bool(result), "expected held tag view"); return std::move(*result);
}
} // namespace

int main() {
  try {
    const auto run = [](const char* name, auto body) { body(); ++cases; std::cout << "case " << name << '\n'; };
    run("exact-manual-name-uid-and-two-normal-hash-sizes", [] {
      const auto raw = fixture(); const auto value = parsed(raw);
      check(value.manualTypePresent && value.tags.size()==1 && value.issues.empty(), "manual tag selection failed");
      const auto& tag=value.tags.front();check(tag.id==1 && tag.uid==12884901889LL && tag.rawName=="Movies", "source name/id/uid mismatch");
      check(tag.membersPresent && !tag.membersMalformed && tag.members.size()==2 && tag.uniqueHashes.size()==2, "normal membership shape mismatch");
      check(tag.uniqueHashes[0].hex()=="000102030405060708090A0B0C0D0E0F10111213", "20-byte declared hash mismatch");
      check(tag.uniqueHashes[1].hex()=="000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F", "32-byte declared hash mismatch");
      check(value.source->originalBytes()==raw && value.source->serialize()==raw, "unknown raw source changed");
    });
    run("absence-empty-and-download-state-swarm-data-stay-distinct", [] {
      auto absent=parsed("de");check(!absent.manualTypePresent && absent.tags.empty(), "missing manual root invented tags");
      auto empty=parsed(dictionary({{"3","de"}}));check(empty.manualTypePresent && empty.tags.empty(), "empty manual root lost presence");
      auto swarm=parsed(dictionary({{"attributes",dictionary({{"stag",list({bytes("not-manual-membership")})}})}}));
      check(!swarm.manualTypePresent && swarm.tags.empty(), "download-state swarm metadata became manual membership");
    });
    run("invalid-roots-and-config-structure-rejected", [] {
      error("not bencode",core::SavedTagError::Format);error("le",core::SavedTagError::Root);
      error(dictionary({{"3","le"}}),core::SavedTagError::ManualType);
      error("d1:3de1:3dee",core::SavedTagError::Format);
    });
    run("missing-empty-localization-and-binary-names-retain-provenance", [] {
      const auto value=parsed(dictionary({{"3",dictionary({
          {"0",dictionary({{"o","le"}})}, {"1",dictionary({{"n",bytes("")}})},
          {"2",dictionary({{"n",bytes("tag.example")}})}, {"3",dictionary({{"n",bytes(std::string("a\0b",3))}})},
          {"4",dictionary({{"n","i1e"}})}})}}));
      check(!value.tags[0].rawName && value.tags[0].uid==12884901888LL, "missing name became invented text");
      check(value.tags[1].rawName && value.tags[1].rawName->empty(), "known empty name lost");
      check(value.tags[2].nameMayRequireLocalization, "localization key mistaken for final translated name");
      check(value.tags[3].rawName==std::string("a\0b",3) && !value.tags[4].rawName, "binary/wrong-type names coerced");
      check(!value.tags[0].membersMalformed && value.tags[0].membersPresent && value.tags[0].members.empty(), "known empty list lost");
      check(!value.tags[1].membersPresent, "absent membership became known empty list");
    });
    run("invalid-id-alias-and-record-types-never-hide-ambiguity", [] {
      const auto value=parsed(dictionary({{"3",dictionary({{"01","de"},{"1","de"},{"2147483647","de"},
          {"2147483648","de"},{"2bad","de"},{"3","i1e"},{"metadata","de"}})}}));
      check(value.tags.size()==6, "numeric-leading malformed rows lost or metadata selected");
      check(value.tags[0].duplicateId && value.tags[1].duplicateId, "leading-zero UID alias hidden");
      check(value.tags[2].uid==15032385535LL, "maximum normal int tag ID incorrect");
      check(!value.tags[3].id && !value.tags[4].id, "invalid integer IDs silently narrowed");
      check(!value.issues.empty() && !value.tags[5].uid && !value.tags[5].rawName && !value.tags[5].membersPresent, "malformed record invented fields");
    });
    run("malformed-members-retained-without-lenient-decoding-claims", [] {
      std::string lower(Hash20);std::transform(lower.begin(),lower.end(),lower.begin(),[](char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;});
      std::string badTail(Hash32);badTail.back()='R';
      auto value=parsed(dictionary({{"3",dictionary({{"1",dictionary({{"o",list({bytes(Hash20),bytes(Hash20),bytes(lower),bytes(badTail),"i1e"})}})}})}}));
      const auto& tag=value.tags[0];check(tag.members.size()==5 && tag.uniqueHashes.size()==1, "duplicates inflated declared membership");
      check(tag.members[0].duplicate && tag.members[1].duplicate, "duplicate source records not retained");
      check(tag.members[2].rawId==lower && !tag.members[2].hash, "noncanonical member was fabricated as normal identity");
      check(!tag.members[3].hash && !tag.members[4].rawId && !tag.members[4].hash, "bad tail/type became a hash");
      const auto wrong=parsed(dictionary({{"3",dictionary({{"1",dictionary({{"o","i1e"}})}})}}));
      check(wrong.tags[0].membersPresent && wrong.tags[0].membersMalformed, "wrong-list type indistinguishable from absence");
    });
    run("projection-owns-raw-bytes-and-primitive-names", [] {
      std::string raw=fixture();const auto value=parsed(raw);const auto source=raw;raw.assign("de");
      check(value.source->originalBytes()==source && value.tags[0].rawName=="Movies" && value.tags[0].members[0].rawId==Hash20,
          "caller mutation changed owned projection");
    });
    run("all-projection-caps-are-lower-only-and-atomic", [] {
      const auto raw=fixture();core::SavedTagLimits limits;limits.tags=1;limits.members=2;
      check(parsed(raw,limits).tags.size()==1, "exact selected row/member bounds rejected");
      limits.tags=0;error(raw,core::SavedTagError::Limit,limits);limits={};limits.members=1;error(raw,core::SavedTagError::Limit,limits);
      limits={};limits.fieldBytes=31;error(raw,core::SavedTagError::Limit,limits);
      limits={};limits.textBytes=1;error(raw,core::SavedTagError::Limit,limits);
      limits={};limits.tags=4097;error("de",core::SavedTagError::Limit,limits);
      limits={};limits.document.inputBytes=4*1024*1024+1;error("de",core::SavedTagError::Limit,limits);
      limits={};limits.document.nodes=100001;error("de",core::SavedTagError::Limit,limits);
      limits={};limits.document.depth=65;error("de",core::SavedTagError::Limit,limits);
      limits={};limits.document.nodes=1;error(raw,core::SavedTagError::Limit,limits);
      limits={};limits.document.depth=0;error(raw,core::SavedTagError::Limit,limits);
      limits={};limits.issues=0;error(dictionary({{"3",dictionary({{"1","i1e"}})}}),core::SavedTagError::Limit,limits);
    });
    run("held-primary-is-read-only-and-never-follows-active-state", [] {
      Area area;const auto raw=fixture();area.write("tag.config",raw);area.write("active", "not-a-directory-and-not-read");
      const auto original=stamp(area.root/"tag.config");const auto state=stamp(area.root/"active");
      const auto value=loaded(area);check(value.snapshot && value.recoverySource==storage::RecoverySource::Primary, "primary provenance lost");
      check(value.snapshot->source->originalBytes()==raw && value.snapshot->tags.size()==1, "primary projection incorrect");
      check(same(original,stamp(area.root/"tag.config")) && same(state,stamp(area.root/"active")), "reader mutated fixture metadata");
    });
    run("true-missing-and-valid-backup-saving-provenance", [] {
      {Area area;const auto value=loaded(area);check(value.missing() && !value.recoverySource, "true missing lost");}
      for(const auto* candidate:{"tag.config.bak","tag.config.saving"}) {
        Area area;area.write(candidate,fixture());const auto original=stamp(area.root/candidate);const auto value=loaded(area);
        check(value.snapshot.has_value() && value.recoverySource.has_value(), "recovery candidate missing");
        check(value.recoverySource==(std::string_view(candidate).ends_with(".bak")?storage::RecoverySource::Backup:storage::RecoverySource::Saving), "recovery precedence/source wrong");
        check(!std::filesystem::exists(area.root/"tag.config") && same(original,stamp(area.root/candidate)), "recovery promoted or modified candidate");
      }
    });
    run("malformed-backup-is-not-missing-and-symlink-is-not-followed", [] {
      {Area area;area.write("tag.config.bak","malformed");auto value=storage::readSavedDownloadTagsView(*area.lease);
       check(!value && value.error().code==storage::ErrorCode::InvalidFormat, "malformed backup hidden as missing");}
      {Area area;area.write("target",fixture());check(::symlink("target",(area.root/"tag.config").c_str())==0,"owned symlink setup failed");
       const auto target=stamp(area.root/"target");auto value=storage::readSavedDownloadTagsView(*area.lease);
       check(!value && value.error().code==storage::ErrorCode::UnsafePath, "symlink configuration accepted");
       check(same(target,stamp(area.root/"target")), "symlink target mutated");}
    });
    run("prestop-and-moved-lease-refuse-without-new-files", [] {
      Area area;std::stop_source stop;stop.request_stop();auto cancelled=storage::readSavedDownloadTagsView(*area.lease,stop.get_token());
      check(!cancelled && cancelled.error().code==storage::ErrorCode::Cancelled, "prestop not honored");
      auto held=std::move(*area.lease);auto invalid=storage::readSavedDownloadTagsView(*area.lease);
      check(!invalid && invalid.error().code==storage::ErrorCode::UnsafePath && held.valid(), "moved lease reused or reacquired");
      check(!std::filesystem::exists(area.root/"tag.config") && !std::filesystem::exists(area.root/"tag.config.saving"), "reader wrote configuration");
    });
    run("saved-statistics-visibility-is-full-long-exact1-with-default-provenance", [] {
      const auto raw=dictionary({{"3",dictionary({
        {"1",dictionary({{"o","le"}})}, {"2",dictionary({{"c",dictionary({{"vis","i1e"}})}})},
        {"3",dictionary({{"c",dictionary({{"vis","i0e"}})}})}, {"4",dictionary({{"c",dictionary({{"vis","i4294967297e"}})}})},
        {"5",dictionary({{"c",dictionary({{"vis","i-1e"}})}})}, {"6",dictionary({{"c",dictionary({{"vis",bytes("yes")}})}})},
        {"7",dictionary({{"c","le"}})}, {"8",dictionary({{"c","de"}})}})}});
      const auto value=parsed(raw);check(value.tags.size()==8,"visibility fixture lost tags");
      check(value.tags[0].visible && value.tags[0].visibilityDefaulted && !value.tags[0].rawVisibility,"missing visibility default lost");
      check(value.tags[1].visible && !value.tags[1].visibilityDefaulted && value.tags[1].rawVisibility==1,"explicit visible1 lost");
      for(const auto i:{2,3,4}) {check(!value.tags[static_cast<std::size_t>(i)].visible && !value.tags[static_cast<std::size_t>(i)].visibilityDefaulted,"non1 full Long became true");}
      check(value.tags[3].rawVisibility==4294967297LL,"visibility was narrowed to low32");
      for(const auto i:{5,6}) {check(value.tags[static_cast<std::size_t>(i)].visible && value.tags[static_cast<std::size_t>(i)].visibilityMalformed && value.tags[static_cast<std::size_t>(i)].visibilityDefaulted,"malformed visibility lost source default or diagnostic");}
      check(value.tags[7].visible && !value.tags[7].visibilityMalformed && value.issues.size()==2,"empty config/default diagnostic mismatch");
      check(value.source->originalBytes()==raw && value.source->serialize()==raw,"visibility extraction changed raw config");
    });
    std::cout << "PASS " << cases << " saved manual download-tag cases; " << assertions
              << " assertions; persisted declared data only, no live membership/rates or Java equality credit\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& failure) { std::cerr << "FAIL " << failure.what() << '\n'; return EXIT_FAILURE; }
}
