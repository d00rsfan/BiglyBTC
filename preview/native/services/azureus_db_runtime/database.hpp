// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded integration of original DHTDBImpl and qualified Mapping/DbValue.
#pragma once
#include "mapping.hpp"
#include "values.hpp"
#include "storage_diversification.hpp"
#include "cache_republication.hpp"
#include <memory>
#include <biglybtc/azureus_local_diversification.hpp>
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
// Original DDB and tracker single-value flags. ANON is protocol0x10, not anonymous
// network routing, and never permits arbitrary flag/replication overrides.
enum class LocalPublicationFlags:std::uint8_t {Normal=0,Downloading=1,Seeding=2,Anonymous=0x10};
struct Snapshot {std::size_t keys=0,values=0,bytes=0;std::uint64_t stores=0,reads=0,expired=0,localPuts=0;bool closed=false;};
struct LocalCheckpoint {std::uint64_t revision=1;std::vector<core::AzureusLocalDiversification> entries;};
struct Read {std::int8_t diversification=1;std::vector<azureus_values::Value> values;std::uint64_t revision=0;};
struct CacheReceipt {std::uint64_t content=0,store=0;bool operator==(const CacheReceipt&)const=default;};
struct CachedMapping {std::string key;CacheReceipt receipt;std::vector<lifecycle::DbValue> values;bool surveyOnly=false;};
struct CachedCycle {std::uint64_t value=0;};
struct SurveyCycle {std::uint64_t value=0;};
struct SurveySubject {std::string key;std::uint64_t incarnation=0;};
struct SurveyStart {SurveyCycle cycle;std::vector<SurveySubject> live;};
struct SurveyMapping {SurveySubject subject;CacheReceipt receipt;std::int8_t diversification=1;std::vector<lifecycle::DbValue> values;};
struct SurveyPage {std::vector<SurveyMapping> mappings;std::size_t next=0;bool more=false;};

struct CachedPage {std::vector<CachedMapping> mappings;std::size_t next=0;bool more=false;std::string through;};
// Serialized volatile facade, no network/profile/timer/retained clock/provider.
// Authorization and routing are supplied by the actual owned transport policy.
// Clock reentry is refused. Closing/destruction must occur outside clock calls.
class Owner final {
public:
  static std::expected<std::unique_ptr<Owner>,Error> create(Limits={});
  ~Owner();
  Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
  std::expected<bool,Error> contains(std::string_view)const;
  // Stable original mapping-object identity within THIS DB session. Updates and
  // empty-but-live records retain it; deletion/recreation never reuses it.
  std::expected<std::optional<std::uint64_t>,Error> mappingIdentity(std::string_view)const;

  // Original six-byte prefix index and two-byte key-tail reply. No ordinary
  // read, expiry scan, clock, diversification counter or source authority.
  std::expected<std::vector<std::optional<std::string>>,Error> queryStoredKeys(std::span<const std::string>)const;
  std::expected<std::vector<std::optional<std::string>>,Error> queryStoredPrefixes(std::span<const std::string>)const;

  // Immutable owning copy; used to bind a normal receiver to its actual socket.
  std::expected<std::optional<azureus_nodes::Contact>,Error> localContact()const;
  // Source local store: actual bound contact, durable externally reserved
  // version, typed normal/anonymous flags and normal lifetime/replication. No remote quota bypass is
  // exposed through store(); explicit owner capacity still bounds local keys.
  std::expected<lifecycle::DbValue,Error> putLocal(std::string_view key,
      std::string_view payload,std::int32_t reservedVersion,lifecycle::WallClock&);
  std::expected<lifecycle::DbValue,Error> putLocal(std::string_view key,
      std::string_view payload,std::int32_t reservedVersion,lifecycle::WallClock&,LocalPublicationFlags);

  // Remove only this actor's local value; the returned original deletion value
  // may be forwarded by a separately source-authorised tracker withdrawal.
  std::expected<std::optional<lifecycle::DbValue>,Error> removeLocal(std::string_view key,
      std::int32_t reservedVersion,lifecycle::WallClock&);
  // Bind exactly once before records exist; transport supplies actual bound socket/NodeCache contact.
  std::expected<void,Error> bindLocal(const azureus_nodes::Contact&);
  // One initial source/network seed, before values/checkpoint observations. Persistent
  // type/expiry only; original volatile counters/bloom are not restored.
  std::expected<void,Error> restoreLocalDiversifications(std::span<const core::AzureusLocalDiversification>,std::int64_t now);
  // Observational: no clock/provider callbacks or file/endpoint authority.
  std::expected<std::uint64_t,Error> localDiversificationRevision()const;
  // Exact current record/content identity, with no read/counter/clock effect.
  std::expected<std::optional<std::uint64_t>,Error> readRevision(std::string_view key)const;
  std::expected<std::shared_ptr<const LocalCheckpoint>,Error> localDiversificationCheckpoint()const;

  std::expected<std::int8_t,Error> store(std::string_view,const azureus_nodes::Contact& sender,
      std::span<const azureus_values::Value>,lifecycle::WallClock&);
  std::expected<std::optional<Read>,Error> read(std::string_view,const azureus_nodes::Contact& reader,
      std::int32_t maximumValues,std::int16_t flags,lifecycle::WallClock&);
  std::expected<void,Error> expire(lifecycle::WallClock&,bool force=false);
  // Original raw getValues selection: includes duplicate and empty remote values.
  // No ordinary read, new local version, source gate, profile or network authority.
  // Freeze only bounded keys and a store-epoch fence, not all value metadata.
  // Pages skip records changed after the cycle began. No file/network authority.
  // Original survey expiry scan, independent from cache-forwarding cycles.
  // Freeze only keys/incarnations/store fence; pages copy bounded raw values.
  std::expected<SurveyStart,Error> beginSurvey(lifecycle::WallClock&);
  std::expected<SurveyPage,Error> surveyPage(SurveyCycle,std::size_t cursor,std::size_t maximumKeys=64,std::size_t maximumValues=256);
  std::expected<void,Error> endSurvey(SurveyCycle);
  // Current owning data under an exact record/content/store receipt. No normal
  // read, expiry, clock/provider callback or authority is inferred.
  std::expected<std::optional<SurveyMapping>,Error> surveyMapping(std::string_view,std::uint64_t incarnation,CacheReceipt)const;
  std::expected<CachedCycle,Error> beginCachedRepublish(std::int64_t now,std::int64_t period,lifecycle::WallClock&,std::string_view after={});
  std::expected<CachedPage,Error> cachedRepublishPage(CachedCycle,std::size_t cursor,std::size_t maximumKeys=64,std::size_t maximumValues=256);
  std::expected<void,Error> endCachedRepublish(CachedCycle);
  std::expected<std::vector<CachedMapping>,Error> prepareCachedRepublish(std::int64_t now,std::int64_t period,lifecycle::WallClock&);
  std::expected<std::optional<CacheReceipt>,Error> cacheReceipt(std::string_view key)const;
  std::expected<bool,Error> eraseCachedMapping(std::string_view key,CacheReceipt);
  // Bounded owning copy for original related-content getDHTInfos. No routing,
  // network effect, counters, storage adapter or permission is inferred.
  std::expected<std::vector<std::string>,Error> nonLocalPayloads(std::size_t maximum=8192)const;
  std::expected<Snapshot,Error> snapshot()const;
  void close()noexcept;
private:
  struct Impl;explicit Owner(std::unique_ptr<Impl>);std::unique_ptr<Impl> impl_;
};
}
