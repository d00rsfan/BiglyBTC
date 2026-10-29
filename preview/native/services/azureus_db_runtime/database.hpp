// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded integration of original DHTDBImpl and qualified Mapping/DbValue.
#pragma once
#include "mapping.hpp"
#include "values.hpp"
#include "storage_diversification.hpp"
#include <memory>
#include <thread>
namespace biglybtc::services::azureus_db_runtime {
namespace mapping=azureus_db_mapping;
namespace lifecycle=azureus_db_value;
enum class Error {Invalid,Unsupported,Thread,Busy,Closed,Capacity,Memory,Mapping};
enum class StorageStatsMode { Absent,SuspendedDiversification,OriginalDiversification };
struct Limits {
  std::size_t maximumKeys=32,maximumEntriesPerKey=64;
  std::size_t payloadQuota=4*1024*1024; // Source existing bytes+4*entries, checked before expiry.
  std::int32_t originalRepublishMillis=8*60*60*1000;
  StorageStatsMode storageStats=StorageStatsMode::Absent;
  // Required only by OriginalDiversification. Owner retains this actual control;
  // no implicit entropy, suspension deadline, file persistence or activation.
  std::shared_ptr<azureus_storage_stats::DiversificationControl> diversificationControl;
};
struct Snapshot {std::size_t keys=0,values=0,bytes=0;std::uint64_t stores=0,reads=0,expired=0,localPuts=0;bool closed=false;};
struct Read {std::int8_t diversification=1;std::vector<azureus_values::Value> values;};
// Serialized volatile facade, no network/profile/timer/retained clock/provider.
// Authorization and routing are supplied by the actual owned transport policy.
// Clock reentry is refused. Closing/destruction must occur outside clock calls.
class Owner final {
public:
  static std::expected<std::unique_ptr<Owner>,Error> create(Limits={});
  ~Owner();
  Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
  std::expected<bool,Error> contains(std::string_view)const;
  // Immutable owning copy; used to bind a normal receiver to its actual socket.
  std::expected<std::optional<azureus_nodes::Contact>,Error> localContact()const;
  // Source local store: actual bound contact, durable externally reserved
  // version, normal flags/lifetime/replication only. No remote quota bypass is
  // exposed through store(); explicit owner capacity still bounds local keys.
  std::expected<lifecycle::DbValue,Error> putLocal(std::string_view key,
      std::string_view payload,std::int32_t reservedVersion,lifecycle::WallClock&);

  // Bind exactly once before records exist; transport supplies actual bound socket/NodeCache contact.
  std::expected<void,Error> bindLocal(const azureus_nodes::Contact&);
  std::expected<std::int8_t,Error> store(std::string_view,const azureus_nodes::Contact& sender,
      std::span<const azureus_values::Value>,lifecycle::WallClock&);
  std::expected<std::optional<Read>,Error> read(std::string_view,const azureus_nodes::Contact& reader,
      std::int32_t maximumValues,std::int16_t flags,lifecycle::WallClock&);
  std::expected<void,Error> expire(lifecycle::WallClock&,bool force=false);
  std::expected<Snapshot,Error> snapshot()const;
  void close()noexcept;
private:
  struct Impl;explicit Owner(std::unique_ptr<Impl>);std::unique_ptr<Impl> impl_;
};
}
