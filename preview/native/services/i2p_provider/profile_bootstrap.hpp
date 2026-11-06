// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "dht.hpp"
#include <chrono>
#include <stop_token>
namespace biglybtc::services::i2p::provider {
enum class BootstrapMode {Preview,Normal};
enum class BootstrapKey {Mixed=0,Pure=1};
struct BootstrapOptions {
  BootstrapMode mode=BootstrapMode::Preview;
  BootstrapKey key=BootstrapKey::Pure;
  std::string pluginDirectory="azneti2phelper";
};
struct BootstrapResult {
  Destination destination;
  std::optional<dht::Id> nodeId;
  std::uint16_t queryPort=0;
  bool keyCreated=false,configCreated=false,discoveryEnabled=false;
};
enum class BootstrapStage {JournalSynced,KeyTemporarySynced,KeyPublished,
  ConfigTemporarySynced,ConfigPublished,Completed};
using BootstrapObserver=void(*)(BootstrapStage);
// Construction/external-I/O worker only. Explicit Normal + actual held OFD +
// original enabled plugin/index settings; Preview always refuses without I/O.
// No private key input/output, network/Actor/source grant or provider admission.
// Pending marker/temp files conservatively require preserved review; no replay.
// Observer is outside the source gate. No callback may retain/destroy the lease.
std::expected<BootstrapResult,storage::Error> bootstrapI2pProfile(
    const storage::ProfileLease&,BootstrapOptions={},std::stop_token={},
    std::chrono::steady_clock::time_point end=std::chrono::steady_clock::time_point::max(),
    BootstrapObserver=nullptr);
}
