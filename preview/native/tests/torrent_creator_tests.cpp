#include "../engine/torrent_creator.hpp"
#include "../engine/original_layout_projection.hpp"
#include "original_torrent_identity_lt.hpp"

#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_info.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// --wrap=fsync is test-only. It injects one post-link directory-sync failure;
// no production test hooks, filesystem permission assumptions or engine exist.
static bool failDirectorySync = false;
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd) {
  struct stat value{};
  if (failDirectorySync && ::fstat(fd, &value) == 0 && S_ISDIR(value.st_mode)) {
    failDirectorySync = false; errno = EIO; return -1;
  }
  return __real_fsync(fd);
}
namespace {
namespace fs = std::filesystem;
namespace lt = libtorrent;
using namespace biglybtc::engine;
std::size_t checks = 0, families = 0;
void check(bool value, const char* text) { ++checks; if (!value) { throw std::runtime_error(text); } }
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
template<class F> void run(const char* name, F&& action) {
  try { action(); ++families; }
  catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
}
void write(const fs::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  CHECK(output.good()); output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close(); CHECK(output.good());
}
std::string read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary); CHECK(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::string data(std::size_t size, unsigned seed = 7) {
  std::string value(size, '\0');
  for (std::size_t i = 0; i < size; ++i) { value[i] = static_cast<char>((i * 37u + seed) % 251u); }
  return value;
}
struct Area {
  fs::path root;
  Area() {
    char path[] = "/tmp/biglybtc-creator-XXXXXX";
    const auto* created = ::mkdtemp(path); CHECK(created); root = created;
    CHECK(::chmod(root.c_str(), 0700) == 0);
  }
  ~Area() { std::error_code ignored; fs::remove_all(root, ignored); }
  fs::path source() const { return root / "sample.bin"; }
  fs::path output() const { return root / "created.torrent"; }
  CreateOptions options() const { CreateOptions value; value.source = source(); value.output = output(); return value; }
  void cleanOutput() const {
    CHECK(!fs::exists(output()));
    for (const auto& entry : fs::directory_iterator(root)) {
      CHECK(!entry.path().filename().native().starts_with(".biglybtc-create-"));
    }
  }
};
lt::torrent_info load(const fs::path& path) {
  lt::error_code error; lt::torrent_info value(path.native(), error); CHECK(!error); return value;
}
void verifyPieces(const lt::torrent_info& info, const std::string& stream) {
  CHECK(info.total_size() == static_cast<std::int64_t>(stream.size()));
  const auto size = static_cast<std::size_t>(info.piece_length());
  const auto pieces = (stream.size() + size - 1) / size;
  CHECK(info.num_pieces() == static_cast<int>(pieces));
  for (std::size_t piece = 0; piece < pieces; ++piece) {
    const auto offset = piece * size;
    const auto count = std::min(size, stream.size() - offset);
    const auto expected = lt::hasher(stream.data() + offset, static_cast<int>(count)).final();
    CHECK(info.hash_for_piece(lt::piece_index_t{static_cast<int>(piece)}) == expected);
  }
}
void expect(const std::expected<CreateResult, CreateError>& result, CreateErrorCode code) {
  CHECK(!result && result.error().code == code);
}
} // namespace
int main() {
  try {
    run("original source, webseed and DHT metadata remain in their exact dictionaries", [] {
      Area area;write(area.source(),"a");auto options=area.options();options.sourceTag="owned π";
      options.urlSeeds={"https://seeds.invalid/payload"};options.httpSeeds={"http://seeds.invalid/script"};options.dhtBackup=false;options.extraHashes=true;
      const auto result=createTorrent(options);CHECK(result);auto document=biglybtc::core::Document::parse(read(area.output()));CHECK(document);
      const auto* info=document->find("info");CHECK(info&&info->find("source")&&info->find("sha1")&&info->find("ed2k"));
      CHECK(info->find("source")->bytes==options.sourceTag);
      CHECK(info->find("sha1")->bytes==lt::hasher("a",1).final().to_string());
      const std::string expected="\xbd\xe5\x2c\xb3\x1d\xe3\x3e\x46\x24\x5e\x05\xfb\xdb\xd6\xfb\x24";
      CHECK(info->find("ed2k")->bytes==expected);
      CHECK(document->find("url-list")->children[0].bytes==options.urlSeeds[0]);
      CHECK(document->find("httpseeds")->children[0].bytes==options.httpSeeds[0]);
      CHECK(document->find("azureus_properties")->find("dht_backup_enable")->integer==0);
      auto without=options;without.output=area.root/"without.torrent";without.urlSeeds.clear();without.httpSeeds.clear();without.dhtBackup=true;
      const auto outside=createTorrent(without);CHECK(outside&&outside->selectedHash==result->selectedHash);
      without.output=area.root/"tag.torrent";without.sourceTag="another";const auto changed=createTorrent(without);CHECK(changed&&changed->selectedHash!=result->selectedHash);
      without.output=area.root/"invalid.torrent";without.format=CreateFormat::V2;expect(createTorrent(without),CreateErrorCode::Options);CHECK(!fs::exists(without.output));
      without.format=CreateFormat::V1;without.httpSeeds={"file:///no"};expect(createTorrent(without),CreateErrorCode::Options);CHECK(!fs::exists(without.output));
    });
    run("original extra hashes include actual empty leaves and hybrid virtual padding", [] {
      for(const auto format:{CreateFormat::V1,CreateFormat::Hybrid}) {
        Area area;const auto source=area.root/"bundle";fs::create_directory(source);write(source/"a","a");write(source/"b","");write(source/"c","b");
        auto options=area.options();options.source=source;options.format=format;options.pieceSize=32768;options.extraHashes=true;
        const auto result=createTorrent(options);CHECK(result);auto document=biglybtc::core::Document::parse(read(area.output()));CHECK(document);
        const auto* info=document->find("info");CHECK(info&&info->find("sha1")&&info->find("ed2k"));
        std::string stream="a";if(format==CreateFormat::Hybrid) stream.resize(32768,'\0');stream+='b';
        CHECK(info->find("sha1")->bytes==lt::hasher(stream.data(),static_cast<int>(stream.size())).final().to_string());
        const auto* files=info->find("files");CHECK(files);std::size_t real=0;
        for(const auto& file:files->children){const auto* attr=file.find("attr");if(attr&&attr->bytes.find('p')!=std::string::npos) continue;
          CHECK(file.find("sha1")&&file.find("ed2k"));if(file.find("length")->integer==0){CHECK(file.find("sha1")->bytes==lt::hasher().final().to_string());CHECK(file.find("ed2k")->bytes==std::string("\x31\xd6\xcf\xe0\xd1\x6a\xe9\x31\xb7\x3c\x59\xd7\xe0\xc0\x89\xc0",16));}++real;
        }CHECK(real==3);
      }
    });
    run("explicit v2 and hybrid retain actual full hashes and authenticated tail projection", [] {
      for(const auto format:{CreateFormat::V2,CreateFormat::Hybrid}) {
        Area area;const auto bytes=data(77003);write(area.source(),bytes);auto options=area.options();options.format=format;
        options.privateTorrent=true;options.pieceSize=32768;const auto result=createTorrent(options);CHECK(result);
        const auto info=load(area.output());const auto raw=read(area.output());auto document=biglybtc::core::Document::parse(raw);CHECK(document);
        const auto layout=biglybtc::compat::originalLayout(*document);CHECK(layout);
        const auto projection=biglybtc::engine::detail::projectOriginalLayout(*layout,info);CHECK(projection);
        CHECK(result->format==format&&result->fullInfoHashV2&&result->fullInfoHashV2->bytes().size()==32);
        CHECK(result->totalBytes==bytes.size()&&result->fileCount==1&&result->pieceCount==3);
        CHECK(layout->bytes==77003&&projection->trailingPad.has_value()==(format==CreateFormat::V2));
        const auto subject=biglybtc::storage::originalTorrentInfoSubject(*document);CHECK(subject);
        CHECK(info.info_hashes().v2==lt::hasher256(subject->data(),static_cast<int>(subject->size())).final());
        CHECK(result->selectedHash.bytes().size()==20&&read(area.source())==bytes);
        if(format==CreateFormat::V2) {CHECK(result->infoHashV1.bytes().empty()&&!result->fullInfoHashV1);CHECK(std::equal(result->selectedHash.bytes().begin(),result->selectedHash.bytes().end(),result->fullInfoHashV2->bytes().begin()));}
        else {CHECK(result->fullInfoHashV1&&result->infoHashV1==result->selectedHash);verifyPieces(info,bytes);}
      }
    });
    run("new formats retain mixed real empty files virtual pad slots and physical bytes", [] {
      for(const auto format:{CreateFormat::V2,CreateFormat::Hybrid}) {
        Area area;const auto source=area.root/"bundle-π";fs::create_directories(source/"a-dir");
        const auto a=data(17001,3),z=data(50003,9),tail=data(101,1);
        write(source/"01-first.bin",a);write(source/"a-dir/02-שب.bin",z);write(source/"a-dir/03-empty.bin","");write(source/"z-last.bin",tail);
        auto options=area.options();options.source=source;options.format=format;const auto result=createTorrent(options);CHECK(result&&result->fileCount==4);
        const auto info=load(area.output());auto document=biglybtc::core::Document::parse(read(area.output()));CHECK(document);
        const auto layout=biglybtc::compat::originalLayout(*document);CHECK(layout);
        const auto projection=biglybtc::engine::detail::projectOriginalLayout(*layout,info);CHECK(projection);
        CHECK(layout->files.size()==6&&layout->files[4].bytes==0&&!layout->files[4].padding);
        CHECK(layout->files[1].padding&&layout->files[3].padding&&projection->trailingPad.has_value()==(format==CreateFormat::V2));
        CHECK(read(source/"01-first.bin")==a&&read(source/"a-dir/02-שب.bin")==z&&read(source/"z-last.bin")==tail&&fs::file_size(source/"a-dir/03-empty.bin")==0);
        if(format==CreateFormat::Hybrid){verifyPieces(info,a+std::string(32768-a.size(),'\0')+z+std::string(65536-z.size(),'\0')+tail);}
      }
    });
    run("format constraints and divergent UTF16 ordering refuse before publication", [] {
      Area area;write(area.source(),data(70003));auto options=area.options();options.format=CreateFormat::V2;options.pieceSize=49152;
      expect(createTorrent(options),CreateErrorCode::Options);area.cleanOutput();options.format=static_cast<CreateFormat>(99);options.pieceSize=0;
      expect(createTorrent(options),CreateErrorCode::Options);area.cleanOutput();
      const auto source=area.root/"names";fs::create_directory(source);write(source/std::string("\xf0\x90\x80\x80-a"),"a");write(source/std::string("\xee\x80\x80-b"),"b");
      for(const auto format:{CreateFormat::V2,CreateFormat::Hybrid}){options=area.options();options.source=source;options.format=format;expect(createTorrent(options),CreateErrorCode::Generator);area.cleanOutput();}
      CHECK(fs::file_size(source/std::string("\xf0\x90\x80\x80-a"))==1&&fs::file_size(source/std::string("\xee\x80\x80-b"))==1);
    });
    run("v2 cancellation preserves payload and existing output rules", [] {
      Area area;const auto bytes=data(100003);write(area.source(),bytes);auto options=area.options();options.format=CreateFormat::V2;std::stop_source stop;
      const auto cancelled=createTorrent(options,[&](const CreateProgress& progress){if(progress.stage==CreateStage::Hashing&&progress.completedBytes>0)stop.request_stop();},stop.get_token());
      expect(cancelled,CreateErrorCode::Cancelled);area.cleanOutput();CHECK(read(area.source())==bytes);
      write(area.output(),"existing");expect(createTorrent(options),CreateErrorCode::Conflict);CHECK(read(area.output())=="existing");
    });
    run("single actual bytes metadata and deterministic repeated generation", [] {
      Area area; const auto bytes = data(190123); write(area.source(), bytes);
      struct stat before{}; CHECK(::stat(area.source().c_str(), &before) == 0);
      auto options = area.options(); options.privateTorrent = true; options.comment = "Generated – π";
      options.trackerTiers = {{"http://127.0.0.1:19123/announce", "udp://127.0.0.1:19124/announce"},
                             {"https://127.0.0.1:19125/announce"}};
      std::vector<CreateProgress> observed;
      const auto result = createTorrent(options, [&](const CreateProgress& value) { observed.push_back(value); });
      CHECK(result && result->publication == CreatePublication::Durable && !result->publicationError);
      CHECK(result->totalBytes == bytes.size() && result->fileCount == 1 && result->pieceSize == 32768 && result->pieceCount == 6);
      const auto info = load(area.output()); verifyPieces(info, bytes);
      CHECK(info.info_hashes().has_v1() && !info.info_hashes().has_v2() && info.priv());
      CHECK(info.comment() == options.comment && info.name() == "sample.bin");
      CHECK(info.trackers().size() == 3 && info.trackers()[0].tier == 0 && info.trackers()[1].tier == 0 && info.trackers()[2].tier == 1);
      CHECK(info.info_hashes().v1.to_string() == std::string(reinterpret_cast<const char*>(result->infoHashV1.bytes().data()), 20));
      std::uint64_t last = 0;
      for (const auto& value : observed) {
        CHECK(value.completedBytes >= last && value.completedBytes <= value.totalBytes);
        if (value.stage != CreateStage::Scanning) { CHECK(value.totalBytes == bytes.size() && value.files == 1); }
        CHECK(value.completedPieces <= value.totalPieces); last = value.completedBytes;
      }
      CHECK(!observed.empty() && observed.front().stage == CreateStage::Scanning && observed.back().stage == CreateStage::Publishing);
      CHECK(observed.back().completedBytes == bytes.size() && observed.back().completedPieces == result->pieceCount);
      auto second = options; second.output = area.root / "again.torrent";
      const auto repeated = createTorrent(second); CHECK(repeated && repeated->infoHashV1 == result->infoHashV1);
      CHECK(read(second.output) == read(area.output()) && read(area.source()) == bytes);
      struct stat after{}, output{}; CHECK(::stat(area.source().c_str(), &after) == 0 && ::stat(area.output().c_str(), &output) == 0);
      CHECK(before.st_ino == after.st_ino && before.st_size == after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec
          && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec && before.st_ctim.tv_sec == after.st_ctim.tv_sec
          && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec);
      CHECK((output.st_mode & 0777) == 0600 && output.st_nlink == 1);
    });
    run("sorted multi-file UTF8 paths cross-file pieces and retained empty files", [] {
      Area area; const auto source = area.root / "bundle-π"; fs::create_directory(source);
      fs::create_directory(source / "b-dir"); fs::create_directory(source / "empty-dir");
      const auto first = data(17003, 3), second = data(50007, 9), third = data(19, 1);
      write(source / "z-last.bin", third); write(source / "b-dir" / "02-שب.bin", second);
      write(source / "b-dir" / "01-empty.bin", ""); write(source / "a-first.bin", first);
      auto options = area.options(); options.source = source; options.pieceSize = 49152;
      const auto result = createTorrent(options); CHECK(result && result->fileCount == 4 && result->pieceSize == 49152);
      const auto info = load(area.output()); verifyPieces(info, first + second + third);
      CHECK(info.name() == "bundle-π" && info.num_files() == 4);
      const std::array<std::string, 4> paths{"bundle-π/a-first.bin", "bundle-π/b-dir/01-empty.bin", "bundle-π/b-dir/02-שب.bin", "bundle-π/z-last.bin"};
      for (std::size_t i = 0; i < paths.size(); ++i) { CHECK(info.files().file_path(lt::file_index_t{static_cast<int>(i)}) == paths[i]); }
      CHECK(info.files().file_size(lt::file_index_t{1}) == 0);
      CHECK(read(source / "a-first.bin") == first && read(source / "b-dir" / "02-שب.bin") == second);
      CHECK(fs::is_empty(source / "empty-dir"));
    });
    run("zero total file directory and empty directory are explicit failures", [] {
      Area area; write(area.source(), ""); expect(createTorrent(area.options()), CreateErrorCode::Empty); area.cleanOutput();
      fs::create_directory(area.root / "empty"); auto options = area.options(); options.source = area.root / "empty";
      expect(createTorrent(options), CreateErrorCode::Empty); area.cleanOutput();
      write(options.source / "zero", ""); expect(createTorrent(options), CreateErrorCode::Empty); area.cleanOutput();
    });
    run("every supported original manual size and automatic source boundary", [] {
      Area area; write(area.source(), "actual small payload"); auto options = area.options();
      for (const auto size : CreatePieceSizes) {
        options.pieceSize = size; options.output = area.root / (std::to_string(size) + ".torrent");
        const auto result = createTorrent(options); CHECK(result && result->pieceSize == size && result->pieceCount == 1);
        CHECK(load(options.output).piece_length() == static_cast<int>(size));
      }
      // Original computes floor(total / size), so one byte past 2048 pieces
      // retains 32 KiB and produces 2049 hashes. No large payload allocation.
      const int fd = ::open(area.source().c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
      CHECK(fd >= 0 && ::ftruncate(fd, 32768LL * 2048 + 1) == 0 && ::close(fd) == 0);
      options = area.options(); const auto boundary = createTorrent(options);
      CHECK(boundary && boundary->pieceSize == 32768 && boundary->pieceCount == 2049);
    });
    run("original256MiB pieces stream across an actual sparse boundary", [] {
      constexpr std::uint32_t size = 268435456U;
      Area area; write(area.source(), "payload");
      const int fd = ::open(area.source().c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
      CHECK(fd >= 0 && ::ftruncate(fd, static_cast<off_t>(size) + 17) == 0 && ::close(fd) == 0);
      struct stat before{}, after{}; CHECK(::stat(area.source().c_str(), &before) == 0);
      auto options = area.options(); options.pieceSize = size;
      std::uint64_t observed = 0;
      const auto result = createTorrent(options, [&](const CreateProgress& progress) {
        if (progress.stage == CreateStage::Hashing) {
          CHECK(progress.completedBytes >= observed); observed = progress.completedBytes;
          CHECK(observed <= static_cast<std::uint64_t>(size) + 17);
        }
      });
      CHECK(result && result->pieceCount == 2 && result->pieceSize == size);
      CHECK(observed == static_cast<std::uint64_t>(size) + 17);
      const auto info = load(area.output()); CHECK(info.piece_length() == static_cast<int>(size));
      lt::hasher first; first.update("payload", 7);
      const std::array<char, 65536> zeros{};
      std::uint32_t remaining = size - 7;
      while (remaining) {
        const auto chunk = std::min<std::uint32_t>(remaining, zeros.size());
        first.update(zeros.data(), static_cast<int>(chunk)); remaining -= chunk;
      }
      CHECK(info.hash_for_piece(lt::piece_index_t{0}) == first.final());
      CHECK(info.hash_for_piece(lt::piece_index_t{1}) == lt::hasher(zeros.data(), 17).final());
      CHECK(::stat(area.source().c_str(), &after) == 0);
      CHECK(before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size);
      CHECK(before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec);
      CHECK(before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec);
    });
    run("existing output and racing publication never replace data", [] {
      Area area; write(area.source(), data(90000)); write(area.output(), "existing output");
      expect(createTorrent(area.options()), CreateErrorCode::Conflict); CHECK(read(area.output()) == "existing output");
      fs::remove(area.output()); bool inserted = false;
      const auto result = createTorrent(area.options(), [&](const CreateProgress& value) {
        if (value.stage == CreateStage::Publishing) { write(area.output(), "concurrent output"); inserted = true; }
      });
      CHECK(inserted); expect(result, CreateErrorCode::Conflict); CHECK(read(area.output()) == "concurrent output");
      fs::remove(area.output()); area.cleanOutput();
    });
    run("cancel before during hashing and at final callback publishes nothing", [] {
      for (const auto stage : {CreateStage::Scanning, CreateStage::Hashing, CreateStage::Publishing}) {
        Area area; write(area.source(), data(180000)); const auto before = read(area.source()); std::stop_source stop;
        const auto result = createTorrent(area.options(), [&](const CreateProgress& value) {
          if (value.stage == stage && (stage != CreateStage::Hashing || value.completedBytes > 0)) { stop.request_stop(); }
        }, stop.get_token());
        expect(result, CreateErrorCode::Cancelled); area.cleanOutput(); CHECK(read(area.source()) == before);
      }
    });
    run("callback exception is contained before any output", [] {
      Area area; write(area.source(), data(90000));
      expect(createTorrent(area.options(), [](const CreateProgress&) { throw std::runtime_error("observer"); }), CreateErrorCode::Callback);
      area.cleanOutput();
    });
    run("callback mutation cannot redirect frozen options", [] {
      Area area; const auto bytes = data(70000); write(area.source(), bytes); auto options = area.options();
      const auto result = createTorrent(options, [&](const CreateProgress& value) {
        if (value.stage == CreateStage::Scanning) {
          options.source = area.root / "unrequested"; options.output = area.root / "redirected.torrent";
          options.pieceSize = 134217728; options.comment = "changed during callback";
        }
      });
      CHECK(result && result->output == area.output() && result->pieceSize == 32768);
      CHECK(!fs::exists(options.output)); verifyPieces(load(area.output()), bytes);
    });
    run("symlink source ancestors nested links and special files never followed", [] {
      Area area; write(area.source(), "safe"); const auto outside = area.root / "outside"; fs::create_directory(outside);
      fs::create_symlink(area.source(), area.root / "link"); auto options = area.options(); options.source = area.root / "link";
      expect(createTorrent(options), CreateErrorCode::UnsafeSource); area.cleanOutput();
      fs::create_directory_symlink(outside, area.root / "linked-directory"); options.source = area.root / "linked-directory" / "missing";
      expect(createTorrent(options), CreateErrorCode::Path); area.cleanOutput();
      const auto directory = area.root / "tree"; fs::create_directory(directory); write(directory / "good", "payload");
      fs::create_symlink(area.source(), directory / "bad"); options.source = directory;
      expect(createTorrent(options), CreateErrorCode::UnsafeSource); area.cleanOutput(); fs::remove(directory / "bad");
      CHECK(::mkfifo((directory / "fifo").c_str(), 0600) == 0);
      expect(createTorrent(options), CreateErrorCode::UnsafeSource); area.cleanOutput();
      CHECK(read(area.source()) == "safe");
    });
    run("output inside source symlink parent and dot aliases are rejected", [] {
      Area area; const auto directory = area.root / "tree"; fs::create_directory(directory); write(directory / "data", "payload");
      auto options = area.options(); options.source = directory; options.output = directory / "new.torrent";
      expect(createTorrent(options), CreateErrorCode::Path); CHECK(!fs::exists(options.output));
      fs::create_directory_symlink(area.root, area.root / "alias"); options.output = area.root / "alias" / "new.torrent";
      expect(createTorrent(options), CreateErrorCode::Path);
      options.output = area.root / "tree" / ".." / "new.torrent"; expect(createTorrent(options), CreateErrorCode::Path);
      options.output = "relative.torrent"; expect(createTorrent(options), CreateErrorCode::Path); area.cleanOutput();
    });
    run("same-length writes replacements and added entries fail final snapshot check", [] {
      for (const int kind : {0, 1, 2}) {
        Area area; const auto tree = area.root / "tree"; fs::create_directory(tree); const auto file = tree / "data"; write(file, data(90000));
        auto options = area.options(); options.source = tree; bool changed = false;
        const auto result = createTorrent(options, [&](const CreateProgress& value) {
          if (value.stage != CreateStage::Publishing || changed) { return; } changed = true;
          if (kind == 0) { write(file, data(90000, 23)); }
          else if (kind == 1) { fs::rename(file, tree / "moved"); write(file, data(90000)); }
          else { write(tree / "added", "new"); }
        });
        CHECK(changed); expect(result, CreateErrorCode::Changed); area.cleanOutput();
      }
    });
    run("piece count depth and option bounds reject without publication", [] {
      Area area; write(area.source(), "payload"); auto options = area.options();
      for (const auto size : {1u, 16384u, 536870912u}) { options.pieceSize = size; expect(createTorrent(options), CreateErrorCode::Options); }
      options = area.options(); options.comment = std::string(CreateMaximumCommentBytes + 1, 'x'); expect(createTorrent(options), CreateErrorCode::Options);
      options = area.options(); options.trackerTiers = {{}}; expect(createTorrent(options), CreateErrorCode::Options);
      options.trackerTiers = {{"file:///tmp/tracker"}}; expect(createTorrent(options), CreateErrorCode::Options);
      options.trackerTiers = {{"http:///missing"}}; expect(createTorrent(options), CreateErrorCode::Options);
      options.trackerTiers = {{"udp://127.0.0.1:19123/white space"}}; expect(createTorrent(options), CreateErrorCode::Options);
      options.trackerTiers = {{std::string("http://host/\0hidden", 19)}}; expect(createTorrent(options), CreateErrorCode::Options);
      options = area.options(); options.comment = std::string("\xc0\x80", 2); expect(createTorrent(options), CreateErrorCode::Options);
      const int fd = ::open(area.source().c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
      CHECK(fd >= 0 && ::ftruncate(fd, static_cast<off_t>(CreateMaximumPieces + 1) * 32768) == 0 && ::close(fd) == 0);
      options = area.options(); options.pieceSize = 32768; expect(createTorrent(options), CreateErrorCode::Limit);
      // A short component tree exceeds the depth bound without long names.
      options.source = area.root / "deep"; fs::create_directory(options.source); auto leaf = options.source;
      for (std::size_t i = 0; i <= CreateMaximumDepth; ++i) { leaf /= "d"; fs::create_directory(leaf); }
      expect(createTorrent(options), CreateErrorCode::Limit); area.cleanOutput();
    });
    run("post-publication directory sync error reports existing valid file", [] {
      Area area; write(area.source(), data(40000)); failDirectorySync = true;
      const auto result = createTorrent(area.options()); failDirectorySync = false;
      CHECK(result && result->publication == CreatePublication::PublishedDirectorySyncFailed);
      CHECK(result->publicationError == std::errc::io_error && fs::exists(area.output()));
      verifyPieces(load(area.output()), read(area.source()));
      expect(createTorrent(area.options()), CreateErrorCode::Conflict);
      for (const auto& entry : fs::directory_iterator(area.root)) { CHECK(!entry.path().filename().native().starts_with(".biglybtc-create-")); }
    });
    std::cout << "PASS " << families << " torrent creator families; " << checks << " assertions\n";
    return 0;
  } catch (const std::exception& error) {
    failDirectorySync = false; std::cerr << "FAIL torrent creator: " << error.what() << '\n'; return 1;
  }
}
