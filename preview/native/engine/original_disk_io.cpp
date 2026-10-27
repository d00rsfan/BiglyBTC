#include "original_disk_io.hpp"

#include <libtorrent/disk_buffer_holder.hpp>
#include <libtorrent/disk_observer.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/peer_request.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <deque>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace biglybtc::engine::detail {
namespace lt = libtorrent;
namespace {
storage::Error missing() { return {storage::ErrorCode::Missing, "Original storage contains an unallocated or transient range"}; }
lt::storage_error diskError(lt::operation_t op, int value = EIO) {
  return {lt::error_code(value, lt::generic_category()), op};
}
lt::storage_error translated(const std::expected<void, storage::Error>& result, lt::operation_t op) {
  return result ? lt::storage_error{} : diskError(op, result.error().code == storage::ErrorCode::Missing ? ENOENT : EIO);
}
// Global allocator has no session state. A delivered disk_buffer_holder may
// outlive a storage holder and is always safe to destroy on the network thread.
class ReadAllocator final : public lt::buffer_allocator_interface {
public:
  void free_disk_buffer(char* data) override { std::free(data); }
};
ReadAllocator readAllocator;
struct LatencySubmission {
  DiskLatency& latency;std::uint64_t token;bool submitted=false;
  ~LatencySubmission(){if(!submitted){latency.finish(token,false);}}
};

class OriginalDisk final : public lt::disk_interface {
  struct Slot {
    std::shared_ptr<OriginalDiskFiles> files;
    std::shared_ptr<void> owner;
  };
  static constexpr std::size_t HighWater = 4 * 1024 * 1024;
  static constexpr std::size_t LowWater = HighWater / 2;
  static constexpr std::size_t MaximumWriteBytes = 32 * 1024 * 1024;
  static constexpr std::size_t MaximumJobs = 65536;
public:
  OriginalDisk(lt::io_context& context, OriginalDiskLookup lookup)
      : context_(context), work_(boost::asio::make_work_guard(context)), lookup_(std::move(lookup)) {}
  ~OriginalDisk() override { abort(true); }
  lt::storage_holder new_torrent(const lt::storage_params& params, const std::shared_ptr<void>& owner) override {
    auto files = lookup_(params.path);
    if (!files || !files->matches(params)) { throw std::invalid_argument("Original storage admission identity/geometry differs"); }
    if (slots_.size() >= MaximumJobs) { throw std::length_error("Original storage admission limit"); }
    // Ordinary linear and metadata-only sessions need no extra disk worker.
    if (!worker_.joinable()) { worker_ = std::thread([this] { run(); }); }
    const auto index = lt::storage_index_t{static_cast<std::uint32_t>(slots_.size())};
    slots_.push_back({std::move(files), owner}); return {index, *this};
  }
  void remove_torrent(lt::storage_index_t index) override { slots_.at(static_cast<std::uint32_t>(index)) = {}; }
  void async_read(lt::storage_index_t index, const lt::peer_request& request,
      std::function<void(lt::disk_buffer_holder, const lt::storage_error&)> handler, lt::disk_job_flags_t) override {
    const auto slot = at(index);
    const auto latencyToken=slot.files->diskLatency().begin(DiskIoAxis::Read);
    LatencySubmission latencySubmission{slot.files->diskLatency(),latencyToken};
    auto failed = [this, slot, handler, latencyToken]() mutable {
      slot.files->diskLatency().finish(latencyToken,false);
      boost::asio::post(context_, [slot, handler = std::move(handler)]() mutable { handler({}, diskError(lt::operation_t::file_read)); });
    };
    enqueue(slot, [this, slot, request, handler = std::move(handler), latencyToken]() mutable {
      lt::storage_error error;
      std::unique_ptr<char,decltype(&std::free)> data(nullptr,std::free);
      if (!validRequest(*slot.files, request)) { error = diskError(lt::operation_t::file_read, EINVAL); }
      else {
        data.reset(static_cast<char*>(std::malloc(static_cast<std::size_t>(request.length))));
        if (!data) { error = diskError(lt::operation_t::file_read, ENOMEM); }
        else { lt::file_index_t failedFile{-1};error = translated(slot.files->read(offset(*slot.files, request), {data.get(), static_cast<std::size_t>(request.length)},&failedFile), lt::operation_t::file_read);if(error.ec&&static_cast<int>(failedFile)>=0){error.file(failedFile);} }
      }
      slot.files->diskLatency().finish(latencyToken,!error.ec);
      if(error.ec){data.reset();}
      lt::disk_buffer_holder buffer(readAllocator, data.release(), error.ec ? 0 : request.length);
      boost::asio::post(context_, [slot, handler = std::move(handler), buffer = std::move(buffer), error]() mutable { handler(std::move(buffer), error); });
    }, std::move(failed));
    latencySubmission.submitted=true;
  }
  bool async_write(lt::storage_index_t index, const lt::peer_request& request, const char* data,
      std::shared_ptr<lt::disk_observer> observer, std::function<void(const lt::storage_error&)> handler, lt::disk_job_flags_t) override {
    const auto slot = at(index);
    if (!validRequest(*slot.files, request)) {
      boost::asio::post(context_, [slot, handler = std::move(handler)] { handler(diskError(lt::operation_t::file_write, EINVAL)); }); return false;
    }
    if(queuedBytes_>MaximumWriteBytes-static_cast<std::size_t>(request.length)) {
      slot.files->recordFailure();
      boost::asio::post(context_,[slot,handler=std::move(handler)]{handler(diskError(lt::operation_t::file_write,ENOBUFS));});return false;
    }
    const auto latencyToken=slot.files->diskLatency().begin(DiskIoAxis::Write);
    LatencySubmission latencySubmission{slot.files->diskLatency(),latencyToken};
    // LT releases its input when this call returns; retain an owned block.
    std::vector<char> copy(data, data + request.length);
    const auto bytes = copy.size(); queuedBytes_ += bytes;
    const bool full = queuedBytes_ >= HighWater;
    if (full && observer) { observers_.insert(observer); }
    auto failed = [this, slot, handler, bytes, latencyToken]() mutable {
      slot.files->diskLatency().finish(latencyToken,false);
      boost::asio::post(context_, [this, slot, handler = std::move(handler), bytes]() mutable { queuedBytes_ -= bytes; handler(diskError(lt::operation_t::file_write)); notifyWriters(); });
    };
    enqueue(slot, [this, slot, request, handler = std::move(handler), copy = std::move(copy), bytes, latencyToken]() mutable {
      const auto error = translated(slot.files->write(offset(*slot.files, request), copy), lt::operation_t::file_write);
      slot.files->diskLatency().finish(latencyToken,!error.ec);
      boost::asio::post(context_, [this, slot, handler = std::move(handler), error, bytes] {
        queuedBytes_ -= bytes; handler(error);
        notifyWriters();
      });
    }, std::move(failed));
    latencySubmission.submitted=true;
    return full;
  }
  void async_hash(lt::storage_index_t index, lt::piece_index_t piece, lt::span<lt::sha256_hash> hashes,
      lt::disk_job_flags_t flags, std::function<void(lt::piece_index_t, const lt::sha1_hash&, const lt::storage_error&)> handler) override {
    const auto slot = at(index);
    auto failed = [this, slot, handler, piece]() mutable {
      boost::asio::post(context_, [slot, handler = std::move(handler), piece]() mutable { handler(piece, {}, diskError(lt::operation_t::file_read)); });
    };
    enqueue(slot, [this, slot, piece, hashes, flags, handler = std::move(handler)] {
      lt::storage_error error; lt::sha1_hash result;
      const auto& files = slot.files->metadata().files();
      if (static_cast<int>(piece) < 0 || static_cast<int>(piece) >= files.num_pieces()) { error = diskError(lt::operation_t::file_read, EINVAL); }
      else {
        const bool v1 = bool(flags & v1_hash), v2 = !hashes.empty();
        const int size1 = v1 ? files.piece_size(piece) : 0, size2 = v2 ? files.piece_size2(piece) : 0;
        const int blocks2 = v2 ? files.blocks_in_piece2(piece) : 0;
        if (v2 && hashes.size() < blocks2) { error = diskError(lt::operation_t::file_read, EINVAL); }
        else {
          lt::hasher h; std::array<char, lt::default_block_size> block{};
          for (int start = 0; start < std::max(size1, size2) && !error.ec; start += lt::default_block_size) {
            const int length1 = std::clamp(size1 - start, 0, lt::default_block_size), length2 = std::clamp(size2 - start, 0, lt::default_block_size);
            const int length = std::max(length1, length2);
            lt::file_index_t failedFile{-1};
            error = translated(slot.files->read(static_cast<std::uint64_t>(static_cast<int>(piece)) * files.piece_length() + start,
                std::span<char>(block).first(static_cast<std::size_t>(length)),&failedFile), lt::operation_t::file_read);
            if(error.ec&&static_cast<int>(failedFile)>=0){error.file(failedFile);}
            if (!error.ec) {
              if (length1) { h.update({block.data(), length1}); }
              if (length2) { hashes[start / lt::default_block_size] = lt::hasher256(lt::span<const char>(block.data(), length2)).final(); }
            }
          }
          if (!error.ec && v1) { result = h.final(); }
          else if(error.ec&&v1){
            // LT still compares the v1 result after a nonfatal ENOENT/EOF.
            // Even untrusted metainfo advertising an all-zero hash cannot
            // turn an unreadable piece into a verified bit.
            result=slot.files->metadata().hash_for_piece(piece);result[0]^=0xff;
          }
          if(error.ec){std::fill(hashes.begin(),hashes.end(),lt::sha256_hash{});}
          if (!error.ec && bool(flags & flush_piece)) { error = translated(slot.files->flush(), lt::operation_t::file_write); }
        }
      }
      boost::asio::post(context_, [slot, handler = std::move(handler), piece, result, error] { handler(piece, result, error); });
    }, std::move(failed));
  }
  void async_hash2(lt::storage_index_t index, lt::piece_index_t piece, int start, lt::disk_job_flags_t flags,
      std::function<void(lt::piece_index_t, const lt::sha256_hash&, const lt::storage_error&)> handler) override {
    const auto slot = at(index);
    auto failed = [this, slot, handler, piece]() mutable {
      boost::asio::post(context_, [slot, handler = std::move(handler), piece]() mutable { handler(piece, {}, diskError(lt::operation_t::file_read)); });
    };
    enqueue(slot, [this, slot, piece, start, flags, handler = std::move(handler)] {
      const auto& files = slot.files->metadata().files(); lt::storage_error error; lt::sha256_hash result;
      if (static_cast<int>(piece) < 0 || static_cast<int>(piece) >= files.num_pieces() || start < 0 || start % lt::default_block_size
          || start >= files.piece_size2(piece)) { error = diskError(lt::operation_t::file_read, EINVAL); }
      else {
        std::array<char, lt::default_block_size> block{}; const int length = std::min(lt::default_block_size, files.piece_size2(piece) - start);
        lt::file_index_t failedFile{-1};
        error = translated(slot.files->read(static_cast<std::uint64_t>(static_cast<int>(piece)) * files.piece_length() + start,
            std::span<char>(block).first(static_cast<std::size_t>(length)),&failedFile), lt::operation_t::file_read);
        if(error.ec&&static_cast<int>(failedFile)>=0){error.file(failedFile);}
        if (!error.ec) { result = lt::hasher256(lt::span<const char>(block.data(), length)).final(); }
        if (!error.ec && bool(flags & flush_piece)) { error = translated(slot.files->flush(), lt::operation_t::file_write); }
      }
      boost::asio::post(context_, [slot, handler = std::move(handler), piece, result, error] { handler(piece, result, error); });
    }, std::move(failed));
  }
  void async_check_files(lt::storage_index_t index, const lt::add_torrent_params*, lt::aux::vector<std::string, lt::file_index_t> links,
      std::function<void(lt::status_t, const lt::storage_error&)> handler) override {
    const auto slot = at(index);
    auto failed = [this, slot, handler]() mutable {
      boost::asio::post(context_, [slot, handler = std::move(handler)]() mutable { handler(lt::status_t::fatal_disk_error, diskError(lt::operation_t::check_resume)); });
    };
    enqueue(slot, [this, slot, links = std::move(links), handler = std::move(handler)] {
      const bool unsupported = std::ranges::any_of(links, [](const auto& value) { return !value.empty(); });
      boost::asio::post(context_, [slot, handler = std::move(handler), unsupported] {
        handler(unsupported ? lt::status_t::fatal_disk_error : lt::status_t::need_full_check,
            unsupported ? diskError(lt::operation_t::check_resume, ENOTSUP) : lt::storage_error{});
      });
    }, std::move(failed));
  }
  void async_release_files(lt::storage_index_t index, std::function<void()> handler = {}) override { barrier(index, std::move(handler)); }
  void async_stop_torrent(lt::storage_index_t index, std::function<void()> handler = {}) override { barrier(index, std::move(handler)); }
  void async_clear_piece(lt::storage_index_t index, lt::piece_index_t piece, std::function<void(lt::piece_index_t)> handler) override {
    const auto slot = at(index);
    auto failed = [this, slot, handler, piece]() mutable {
      boost::asio::post(context_, [slot, handler = std::move(handler), piece]() mutable { handler(piece); });
    };
    enqueue(slot, [this, slot, piece, handler = std::move(handler)] { boost::asio::post(context_, [slot, piece, handler = std::move(handler)] { handler(piece); }); }, std::move(failed));
  }
  void async_set_file_priority(lt::storage_index_t index, lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities,
      std::function<void(const lt::storage_error&, lt::aux::vector<lt::download_priority_t, lt::file_index_t>)> handler) override {
    const auto slot = at(index);
    auto failed = [this, slot, handler, priorities]() mutable {
      boost::asio::post(context_, [slot, handler = std::move(handler), priorities]() mutable { handler(diskError(lt::operation_t::file_write), std::move(priorities)); });
    };
    enqueue(slot, [this, slot, priorities = std::move(priorities), handler = std::move(handler)]() mutable {
      // No partfile: skipped boundary fragments stay in their original codec.
      // Selecting compact requires the stopped owner's explicit conversion.
      boost::asio::post(context_, [slot, priorities = std::move(priorities), handler = std::move(handler)]() mutable { handler({}, std::move(priorities)); });
    }, std::move(failed));
  }
  void async_move_storage(lt::storage_index_t index, std::string path, lt::move_flags_t,
      std::function<void(lt::status_t, const std::string&, const lt::storage_error&)> handler) override {
    const auto slot = at(index); boost::asio::post(context_, [slot, path = std::move(path), handler = std::move(handler)] { handler(lt::status_t::fatal_disk_error, path, diskError(lt::operation_t::file_rename, ENOTSUP)); });
  }
  void async_rename_file(lt::storage_index_t index, lt::file_index_t file, std::string name,
      std::function<void(const std::string&, lt::file_index_t, const lt::storage_error&)> handler) override {
    const auto slot = at(index); boost::asio::post(context_, [slot, name = std::move(name), file, handler = std::move(handler)] { handler(name, file, diskError(lt::operation_t::file_rename, ENOTSUP)); });
  }
  void async_delete_files(lt::storage_index_t index, lt::remove_flags_t,
      std::function<void(const lt::storage_error&)> handler) override {
    const auto slot = at(index); boost::asio::post(context_, [slot, handler = std::move(handler)] { handler(diskError(lt::operation_t::file_remove, ENOTSUP)); });
  }
  void update_stats_counters(lt::counters&) const override {}
  std::vector<lt::open_file_state> get_status(lt::storage_index_t) const override { return {}; }
  void abort(bool wait) override {
    { const std::lock_guard lock(mutex_); stopping_ = true; }
    ready_.notify_one();
    // No lazy worker means nobody has taken the constructor's guard. Release
    // it here so ordinary/mmap-only sessions can finish their network loop.
    if (!worker_.joinable()) { work_.reset(); }
    if (wait && worker_.joinable()) { worker_.join(); }
  }
  void submit_jobs() override { ready_.notify_one(); }
  void settings_updated() override {}
private:
  Slot at(lt::storage_index_t index) const {
    const auto slot = slots_.at(static_cast<std::uint32_t>(index));
    if (!slot.files) { throw std::logic_error("Retired original storage index"); } return slot;
  }
  static bool validRequest(const OriginalDiskFiles& files, const lt::peer_request& request) {
    const auto& metadata = files.metadata(); const int piece = static_cast<int>(request.piece);
    return piece >= 0 && piece < metadata.num_pieces() && request.start >= 0 && request.length > 0
        && request.length <= lt::default_block_size && request.start <= metadata.piece_size(request.piece) - request.length;
  }
  static std::uint64_t offset(const OriginalDiskFiles& files, const lt::peer_request& request) {
    return static_cast<std::uint64_t>(static_cast<int>(request.piece)) * files.metadata().piece_length() + request.start;
  }
  void barrier(lt::storage_index_t index, std::function<void()> handler) {
    const auto slot = at(index);
    auto failed = [this, slot, handler]() mutable {
      boost::asio::post(context_, [slot, handler = std::move(handler)]() mutable { if (handler) { handler(); } });
    };
    enqueue(slot, [this, slot, handler = std::move(handler)] {
      // Remember failed flushes in the context; the owner must reject its
      // checkpoint because these LT barrier callbacks cannot carry errors.
      (void)slot.files->flush();
      boost::asio::post(context_, [slot, handler = std::move(handler)] { if (handler) { handler(); } });
    }, std::move(failed));
  }
  void notifyWriters() {
    if (queuedBytes_ >= LowWater) { return; }
    auto observers = std::move(observers_); observers_.clear();
    for (const auto& weak : observers) { if (const auto observer = weak.lock()) { observer->on_disk(); } }
  }
  void enqueue(Slot slot, std::function<void()> job, std::function<void()> failed) {
    bool accepted=false;
    try {
      const std::lock_guard lock(mutex_);
      if (!stopping_ && jobs_.size() < MaximumJobs) {
        jobs_.push_back([slot, job = std::move(job), failed] {
          try { job(); } catch (...) { slot.files->recordFailure(); failed(); }
        }); accepted=true;
      }
    } catch (const std::bad_alloc&) { /* settle through the same error callback */ }
    if(!accepted){slot.files->recordFailure();failed();return;}
    ready_.notify_one();
  }
  void run() {
    // LT abort_stage2 releases its session guard after abort(false). Keep its
    // network loop alive until ALL disk jobs and their final ownership
    // retirement have been posted to the network context. A thread-start
    // failure leaves work_ in the constructor so abort() can release it.
    [[maybe_unused]] auto work = std::move(work_);
    for (;;) {
      std::function<void()> job;
      { std::unique_lock lock(mutex_); ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (jobs_.empty()) { return; } job = std::move(jobs_.front()); jobs_.pop_front(); }
      job();
      // Success may already have run concurrently. The consumed wrapper still
      // owns Slot and success/failure callback copies, so retire those captures
      // on the context too; never invoke the consumed job a second time.
      boost::asio::post(context_, [finished = std::move(job)]() mutable { finished = {}; });
    }
  }
  lt::io_context& context_;
  boost::asio::executor_work_guard<lt::io_context::executor_type> work_;
  OriginalDiskLookup lookup_;
  std::vector<Slot> slots_;
  std::size_t queuedBytes_ = 0;
  std::set<std::weak_ptr<lt::disk_observer>,std::owner_less<std::weak_ptr<lt::disk_observer>>> observers_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<std::function<void()>> jobs_;
  bool stopping_ = false;
  std::thread worker_;
};
} // namespace

OriginalDiskFiles::OriginalDiskFiles(std::shared_ptr<lt::torrent_info> metadata,
    std::vector<std::optional<OriginalDiskFile>> files,OriginalDiskAccess access) : access_(access),metadata_(std::move(metadata)), files_(std::move(files)) {
  if (!metadata_ || files_.size() != static_cast<std::size_t>(metadata_->num_files())) { throw std::invalid_argument("Original storage file count differs"); }
  if(access_!=OriginalDiskAccess::ReadWrite&&access_!=OriginalDiskAccess::ReadOnly&&access_!=OriginalDiskAccess::SelectedReadOnly){throw std::invalid_argument("Unknown original disk access policy");}
  if(access_==OriginalDiskAccess::ReadOnly){for(int n=0;n<metadata_->num_files();++n){const auto index=lt::file_index_t{n};if(metadata_->files().pad_file_at(index)){continue;}const auto& file=files_[static_cast<std::size_t>(n)];struct stat stamp{};const int flags=file?::fcntl(file->descriptor.get(),F_GETFL):-1;
    if(!file||flags<0||(flags&O_ACCMODE)!=O_RDONLY
        ||::fstat(file->descriptor.get(),&stamp)!=0||!S_ISREG(stamp.st_mode)||stamp.st_uid!=::geteuid()||stamp.st_nlink!=1||stamp.st_size!=metadata_->files().file_size(index)){
      throw std::invalid_argument("Read-only original storage requires complete owned read-only descriptors");}
  }}
  if(access_==OriginalDiskAccess::SelectedReadOnly){for(int n=0;n<metadata_->num_files();++n){
    const auto index=lt::file_index_t{n};if(metadata_->files().pad_file_at(index)){continue;}
    const auto& file=files_[static_cast<std::size_t>(n)];if(!file){continue;}struct stat stamp{};
    const int flags=::fcntl(file->descriptor.get(),F_GETFL);
    if(flags<0||(flags&O_ACCMODE)!=O_RDONLY||!file->validateControl
        ||::fstat(file->descriptor.get(),&stamp)!=0||!S_ISREG(stamp.st_mode)
        ||stamp.st_uid!=::geteuid()||stamp.st_nlink!=1||stamp.st_size<0){
      throw std::invalid_argument("Selected read-only storage requires authenticated owned O_RDONLY leaves");}
  }}
}
bool OriginalDiskFiles::matches(const lt::storage_params& params) const {
  const auto& files = metadata_->files();
  if (params.info_hash != metadata_->info_hash() || params.files.num_files() != files.num_files()
      || params.files.total_size() != files.total_size() || params.files.piece_length() != files.piece_length()) { return false; }
  for (int i = 0; i < files.num_files(); ++i) { const auto index = lt::file_index_t{i};
    if (params.files.file_size(index) != files.file_size(index) || params.files.file_offset(index) != files.file_offset(index)
        || params.files.pad_file_at(index) != files.pad_file_at(index)) { return false; }
  }
  return true;
}
std::expected<void, storage::Error> OriginalDiskFiles::read(std::uint64_t offset, std::span<char> output,lt::file_index_t* failedFile) {
  if(failedFile){*failedFile=lt::file_index_t{-1};}
  if (offset > static_cast<std::uint64_t>(metadata_->total_size()) || output.size() > static_cast<std::uint64_t>(metadata_->total_size()) - offset) { return std::unexpected(missing()); }
  const auto& files = metadata_->files(); std::size_t done = 0;
  while (done < output.size()) {
    const auto index = files.file_index_at_offset(static_cast<std::int64_t>(offset));
    const auto within = offset - static_cast<std::uint64_t>(files.file_offset(index));
    const auto length = std::min<std::uint64_t>(output.size() - done, static_cast<std::uint64_t>(files.file_size(index)) - within);
    if (!length) { return std::unexpected(missing()); }
    auto part = output.subspan(done, static_cast<std::size_t>(length));
    if (files.pad_file_at(index)) { std::ranges::fill(part, '\0'); }
    else {
      if(failedFile){*failedFile=index;}
      auto& file = files_.at(static_cast<int>(index)); if (!file) { return std::unexpected(missing()); }
      if(access_!=OriginalDiskAccess::ReadWrite&&file->validateControl){const auto proof=file->validateControl();if(!proof){return std::unexpected(proof.error());}}
      const auto result = file->io.read(within, std::as_writable_bytes(part));
      if (!result) { return std::unexpected(result.error()); } if (!result->complete()) { return std::unexpected(missing()); }
      if(access_!=OriginalDiskAccess::ReadWrite&&file->validateControl){const auto proof=file->validateControl();if(!proof){return std::unexpected(proof.error());}}
    }
    done += static_cast<std::size_t>(length); offset += length;
  }
  if(failedFile){*failedFile=lt::file_index_t{-1};}
  return {};
}
std::expected<void, storage::Error> OriginalDiskFiles::write(std::uint64_t offset, std::span<const char> input) {
  if (const auto error=failure()) { return std::unexpected(*error); }
  if(access_!=OriginalDiskAccess::ReadWrite){failure_=storage::Error{storage::ErrorCode::Unsupported,"Existing-only payload cannot be created, repaired or written"};return std::unexpected(*failure_);}
  if (offset > static_cast<std::uint64_t>(metadata_->total_size()) || input.size() > static_cast<std::uint64_t>(metadata_->total_size()) - offset) { return std::unexpected(missing()); }
  const auto& files = metadata_->files(); std::size_t done = 0;
  while (done < input.size()) {
    const auto index = files.file_index_at_offset(static_cast<std::int64_t>(offset));
    const auto within = offset - static_cast<std::uint64_t>(files.file_offset(index));
    const auto length = std::min<std::uint64_t>(input.size() - done, static_cast<std::uint64_t>(files.file_size(index)) - within);
    if (!length) { return std::unexpected(missing()); }
    if (!files.pad_file_at(index)) {
      auto& file = files_.at(static_cast<int>(index)); if (!file) { return std::unexpected(missing()); }
      auto result = file->io.write(within, std::as_bytes(input.subspan(done, static_cast<std::size_t>(length))));
      if (!result) { failure_ = result.error(); return result; }
    }
    done += static_cast<std::size_t>(length); offset += length;
  }
  return {};
}
std::expected<void, storage::Error> OriginalDiskFiles::flush() {
  if (const auto error=failure()) { return std::unexpected(*error); }
  for (auto& file : files_) { if (file) {
    auto result = access_!=OriginalDiskAccess::ReadWrite?std::expected<void,storage::Error>{}:file->io.flush();
    if(result&&file->validateControl){result=file->validateControl();}
    if (!result) { failure_ = result.error(); return result; }
  } }
  return {};
}
std::expected<void, storage::Error> OriginalDiskFiles::finishComplete(std::span<const std::uint8_t> complete) {
  if (const auto error=failure()) { return std::unexpected(*error); }
  if (complete.size()!=files_.size() || std::ranges::any_of(complete,[](auto value){return value>1;})) {
    return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Invalid actual complete-file checkpoint"});
  }
  for (std::size_t i=0;i<files_.size();++i) { if (files_[i]) {
    std::expected<void,storage::Error> result;
    if(access_==OriginalDiskAccess::ReadWrite){
      if(complete[i]){result=files_[i]->io.finishCompleteReordered();}
      else{const auto normalized=files_[i]->io.finishAllocatedReordered();if(!normalized){result=std::unexpected(normalized.error());}}
    }
    if (result&&files_[i]->validateControl) { result=files_[i]->validateControl(); }
    if (!result) { failure_=result.error();return result; }
  } }
  return {};
}
std::optional<storage::Error> OriginalDiskFiles::failure() const {
  if(failedException_.load()){return storage::Error{storage::ErrorCode::Io,"Original disk worker failed; admission requires recheck"};}
  return failure_;
}
void OriginalDiskFiles::recordFailure() { failedException_.store(true); }
std::unique_ptr<lt::disk_interface> originalDiskConstructor(lt::io_context& context, OriginalDiskLookup lookup) {
  return std::make_unique<OriginalDisk>(context, std::move(lookup));
}
} // namespace biglybtc::engine::detail
