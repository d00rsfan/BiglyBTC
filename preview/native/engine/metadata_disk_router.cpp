#include "metadata_disk_router.hpp"
#include "original_disk_io.hpp"
#include "mmap_disk_latency.hpp"

#include <libtorrent/disabled_disk_io.hpp>
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/mmap_disk_io.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace biglybtc::engine::detail {
namespace {
namespace lt = libtorrent;
std::string normalized(const std::filesystem::path& path) {
  if (!path.is_absolute()) { throw std::invalid_argument("Metadata disk routing requires an absolute path"); }
  auto value = path.lexically_normal().generic_string();
  while (value.size() > 1 && value.back() == '/') { value.pop_back(); }
  if (value.empty() || value == "/" || value.size() > 4096 || value.find('\0') != std::string::npos) {
    throw std::invalid_argument("Invalid metadata disk routing path");
  }
  return value;
}

// Network-thread confined exactly like the stock disk_interface. Only the
// registry is shared with the worker. Slots retain the BACKEND holder, not an
// extra torrent reference; all callbacks and borrowed parameters pass through.
class MetadataDiskRouter final : public lt::disk_interface {
  struct Slot {
    lt::disk_interface* backend;
    lt::storage_holder holder;
    std::weak_ptr<MmapDiskLatency> latency;
  };
public:
  MetadataDiskRouter(std::shared_ptr<MetadataDiskPolicy> policy, lt::io_context& context,
      const lt::settings_interface& settings, lt::counters& counters)
      : policy_(std::move(policy)) {
    // No allocation after a backend has returned its owning holder.
    slots_.reserve(MetadataDiskPolicy::MaximumStorageAdmissions);
#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
    real_ = lt::mmap_disk_io_constructor(context, settings, counters);
#else
    throw std::runtime_error("Metadata disk routing requires the reviewed mmap backend");
#endif
    try { disabled_ = lt::disabled_disk_io_constructor(context, settings, counters); }
    catch (...) { real_->abort(true); throw; }
    try { original_=originalDiskConstructor(context,[policy=policy_](const std::string& path){return policy->originalFiles(path);}); }
    catch (...) {disabled_->abort(true);real_->abort(true);throw;}
  }
  ~MetadataDiskRouter() override {retireObservations();}

  lt::storage_holder new_torrent(const lt::storage_params& parameters,
      const std::shared_ptr<void>& torrent) override {
    if (slots_.size() >= MetadataDiskPolicy::MaximumStorageAdmissions) {
      throw std::length_error("Metadata disk routing admission limit reached");
    }
    // Normalize/lookup BEFORE either backend sees the torrent. Never catch a
    // normalization/allocation failure and fall back to real file IO.
    auto* backend = policy_->isMetadataPath(std::filesystem::path(parameters.path)) ? disabled_.get() : policy_->isOriginalPath(std::filesystem::path(parameters.path)) ? original_.get() : real_.get();
    std::shared_ptr<MmapDiskLatency> latency;
    if(backend==real_.get()){
      std::array<unsigned char,20> hash{};std::copy(parameters.info_hash.begin(),parameters.info_hash.end(),hash.begin());
      latency=policy_->claimMmapLatency(std::filesystem::path(parameters.path),hash);
    }
    auto holder = backend->new_torrent(parameters, torrent);
    if (!holder) { throw std::runtime_error("Disk backend returned no storage holder"); }
    const auto index = lt::storage_index_t{static_cast<std::uint32_t>(slots_.size())};
    slots_.push_back({backend, std::move(holder),latency});
    return {index, *this};
  }
  void remove_torrent(lt::storage_index_t index) override {
    const auto offset = static_cast<std::uint32_t>(index);
    if (offset < slots_.size() && slots_[offset].holder) {
      if(auto latency=slots_[offset].latency.lock()){latency->retire();}
      slots_[offset].holder.reset(); // Exactly one stock remove; leave a tombstone, never reuse index.
    }
  }
  void async_read(lt::storage_index_t index, const lt::peer_request& request,
      std::function<void(lt::disk_buffer_holder, const lt::storage_error&)> handler,
      lt::disk_job_flags_t flags = {}) override {
    auto& slot = at(index);
    if(auto latency=slot.latency.lock();latency&&latency->active()&&handler){
      if(auto receipt=latency->begin(DiskIoAxis::Read)){
        std::function<void(lt::disk_buffer_holder,const lt::storage_error&)> wrapped;
        try{wrapped=[copy=handler,receipt](lt::disk_buffer_holder buffer,const lt::storage_error& error) mutable {
          receipt->complete(!error.ec);copy(std::move(buffer),error);
        };}catch(...){latency->lost();} // Original handler remains untouched.
        if(wrapped){slot.backend->async_read(slot.holder,request,std::move(wrapped),flags);return;}
      }
    }
    slot.backend->async_read(slot.holder,request,std::move(handler),flags);
  }
  bool async_write(lt::storage_index_t index, const lt::peer_request& request, const char* buffer,
      std::shared_ptr<lt::disk_observer> observer, std::function<void(const lt::storage_error&)> handler,
      lt::disk_job_flags_t flags = {}) override {
    auto& slot = at(index);
    if(auto latency=slot.latency.lock();latency&&latency->active()&&handler){
      if(auto receipt=latency->begin(DiskIoAxis::Write)){
        std::function<void(const lt::storage_error&)> wrapped;
        try{wrapped=[copy=handler,receipt](const lt::storage_error& error) mutable {
          receipt->complete(!error.ec);copy(error);
        };}catch(...){latency->lost();} // Preserve original handler/buffer/observer.
        if(wrapped){return slot.backend->async_write(slot.holder,request,buffer,std::move(observer),std::move(wrapped),flags);}
      }
    }
    return slot.backend->async_write(slot.holder,request,buffer,std::move(observer),std::move(handler),flags);
  }
  void async_hash(lt::storage_index_t index, lt::piece_index_t piece, lt::span<lt::sha256_hash> hashes,
      lt::disk_job_flags_t flags, std::function<void(lt::piece_index_t, const lt::sha1_hash&, const lt::storage_error&)> handler) override {
    auto& slot = at(index); slot.backend->async_hash(slot.holder, piece, hashes, flags, std::move(handler));
  }
  void async_hash2(lt::storage_index_t index, lt::piece_index_t piece, int offset, lt::disk_job_flags_t flags,
      std::function<void(lt::piece_index_t, const lt::sha256_hash&, const lt::storage_error&)> handler) override {
    auto& slot = at(index); slot.backend->async_hash2(slot.holder, piece, offset, flags, std::move(handler));
  }
  void async_move_storage(lt::storage_index_t index, std::string path, lt::move_flags_t flags,
      std::function<void(lt::status_t, const std::string&, const lt::storage_error&)> handler) override {
    auto& slot = at(index); slot.backend->async_move_storage(slot.holder, std::move(path), flags, std::move(handler));
  }
  void async_release_files(lt::storage_index_t index, std::function<void()> handler = {}) override {
    auto& slot = at(index);
    if (slot.backend == disabled_.get() && !handler) { handler = [] {}; }
    slot.backend->async_release_files(slot.holder, std::move(handler));
  }
  void async_check_files(lt::storage_index_t index, const lt::add_torrent_params* resume,
      lt::aux::vector<std::string, lt::file_index_t> links,
      std::function<void(lt::status_t, const lt::storage_error&)> handler) override {
    auto& slot = at(index); slot.backend->async_check_files(slot.holder, resume, std::move(links), std::move(handler));
  }
  void async_stop_torrent(lt::storage_index_t index, std::function<void()> handler = {}) override {
    auto& slot = at(index);
    if (slot.backend == disabled_.get() && !handler) { handler = [] {}; }
    slot.backend->async_stop_torrent(slot.holder, std::move(handler));
  }
  void async_rename_file(lt::storage_index_t index, lt::file_index_t file, std::string name,
      std::function<void(const std::string&, lt::file_index_t, const lt::storage_error&)> handler) override {
    auto& slot = at(index); slot.backend->async_rename_file(slot.holder, file, std::move(name), std::move(handler));
  }
  void async_delete_files(lt::storage_index_t index, lt::remove_flags_t options,
      std::function<void(const lt::storage_error&)> handler) override {
    auto& slot = at(index); slot.backend->async_delete_files(slot.holder, options, std::move(handler));
  }
  void async_set_file_priority(lt::storage_index_t index, lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities,
      std::function<void(const lt::storage_error&, lt::aux::vector<lt::download_priority_t, lt::file_index_t>)> handler) override {
    auto& slot = at(index); slot.backend->async_set_file_priority(slot.holder, std::move(priorities), std::move(handler));
  }
  void async_clear_piece(lt::storage_index_t index, lt::piece_index_t piece, std::function<void(lt::piece_index_t)> handler) override {
    auto& slot = at(index); slot.backend->async_clear_piece(slot.holder, piece, std::move(handler));
  }
  void update_stats_counters(lt::counters& counters) const override { real_->update_stats_counters(counters); }
  std::vector<lt::open_file_state> get_status(lt::storage_index_t index) const override {
    const auto& slot = at(index); return slot.backend->get_status(slot.holder);
  }
  void abort(bool wait) override { retireObservations();original_->abort(wait); disabled_->abort(wait); real_->abort(wait); }
  void submit_jobs() override { real_->submit_jobs(); disabled_->submit_jobs(); original_->submit_jobs(); }
  void settings_updated() override { real_->settings_updated(); disabled_->settings_updated(); original_->settings_updated(); }
private:
  void retireObservations() noexcept {
    for(const auto& slot:slots_){if(auto latency=slot.latency.lock()){latency->retire();}}
  }
  Slot& at(lt::storage_index_t index) {
    auto& slot = slots_.at(static_cast<std::uint32_t>(index));
    if (!slot.holder) { throw std::logic_error("Retired disk routing index"); }
    return slot;
  }
  const Slot& at(lt::storage_index_t index) const {
    const auto& slot = slots_.at(static_cast<std::uint32_t>(index));
    if (!slot.holder) { throw std::logic_error("Retired disk routing index"); }
    return slot;
  }
  std::shared_ptr<MetadataDiskPolicy> policy_;
  // Holders must be destroyed BEFORE their stock backends.
  std::unique_ptr<lt::disk_interface> real_, disabled_, original_;
  std::vector<Slot> slots_;
};
} // namespace

std::expected<void, storage::Error> MetadataDiskPolicy::registerMetadataPath(const std::filesystem::path& path) {
  try {
    const auto key = normalized(path);
    const std::lock_guard lock(mutex_);
    if (originals_.contains(key)) { return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Original disk path already reserved"}); }
    if (paths_.contains(key)) { return {}; }
    if (paths_.size() >= MaximumMetadataPaths) {
      return std::unexpected(storage::Error{storage::ErrorCode::Unsupported, "Metadata path reservation limit reached"});
    }
    paths_.insert(key); return {};
  } catch (const std::invalid_argument&) {
    return std::unexpected(storage::Error{storage::ErrorCode::UnsafePath, "Invalid absolute metadata save path"});
  } catch (...) {
    return std::unexpected(storage::Error{storage::ErrorCode::Io, "Metadata path reservation failed"});
  }
}
bool MetadataDiskPolicy::isMetadataPath(const std::filesystem::path& path) const {
  const auto key = normalized(path);
  const std::lock_guard lock(mutex_);
  return paths_.contains(key);
}
std::expected<void,storage::Error> MetadataDiskPolicy::registerOriginalPath(const std::filesystem::path& path,std::shared_ptr<OriginalDiskFiles> files) {
  try {const auto key=normalized(path);const std::lock_guard lock(mutex_);
    if(!files||paths_.contains(key)||originals_.contains(key)){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Duplicate or invalid original storage reservation"});}
    if(originals_.size()>=MaximumStorageAdmissions){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Original storage reservation limit"});}
    originals_.emplace(key,files);return {};
  }catch(...){return std::unexpected(storage::Error{storage::ErrorCode::UnsafePath,"Invalid original storage reservation"});}
}
bool MetadataDiskPolicy::registerMmapLatency(const std::filesystem::path& path,const std::array<unsigned char,20>& hash,
    const std::shared_ptr<MmapDiskLatency>& latency) noexcept {
  if(!latency||!latency->admission()||!latency->active()){return false;}
  try{const auto key=std::make_pair(normalized(path),hash);const std::lock_guard lock(mutex_);
    if(paths_.contains(key.first)||originals_.contains(key.first)){latency->lost();return false;}
    if(const auto found=latencies_.find(key);found!=latencies_.end()){
      if(auto previous=found->second.lock();previous&&previous->active()){latency->lost();return false;}
      found->second=latency;return true;
    }
    if(latencies_.size()>=MaximumStorageAdmissions){latency->lost();return false;}
    latencies_.emplace(key,latency);return true;
  }catch(...){latency->lost();return false;}
}
std::shared_ptr<MmapDiskLatency> MetadataDiskPolicy::claimMmapLatency(const std::filesystem::path& path,
    const std::array<unsigned char,20>& hash) noexcept {
  try{const auto key=std::make_pair(normalized(path),hash);const std::lock_guard lock(mutex_);
    const auto found=latencies_.find(key);if(found==latencies_.end()){return {};}
    auto value=found->second.lock();return value&&value->claim()?value:std::shared_ptr<MmapDiskLatency>{};
  }catch(...){return {};}
}
bool MetadataDiskPolicy::isOriginalPath(const std::filesystem::path& path) const {const auto key=normalized(path);const std::lock_guard lock(mutex_);return originals_.contains(key);}
std::shared_ptr<OriginalDiskFiles> MetadataDiskPolicy::originalFiles(const std::filesystem::path& path) const {
  const auto key=normalized(path);const std::lock_guard lock(mutex_);const auto found=originals_.find(key);
  if(found==originals_.end()){throw std::invalid_argument("Unreserved original storage path");}
  auto result=found->second.lock();if(!result){throw std::invalid_argument("Retired original storage path");}return result;
}
lt::disk_io_constructor_type metadataDiskConstructor(std::shared_ptr<MetadataDiskPolicy> policy) {
  if (!policy) { throw std::invalid_argument("Metadata disk policy is required"); }
  return [policy = std::move(policy)](lt::io_context& context, const lt::settings_interface& settings, lt::counters& counters) {
    return std::make_unique<MetadataDiskRouter>(policy, context, settings, counters);
  };
}
} // namespace biglybtc::engine::detail
