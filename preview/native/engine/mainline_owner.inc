// This separate service owns its own DHT alert queue. The torrent session's
// single pump, payload counters and retirement certificates remain independent.
std::unique_ptr<MainlineDhtService> mainline;
const storage::ProfileLease* mainlineLease=nullptr;
std::optional<storage::MainlineCheckpoint> mainlineCheckpoint;
Clock::time_point mainlineCheckpointNext{};
std::optional<std::vector<std::pair<std::string,int>>> mainlineBootstrap;
MainlineDiscoveryStatus mainlineStatus;
std::vector<lt::sha1_hash> mainlineActive;
static lt::sha1_hash discoveryHash(const core::TorrentHash& hash) {
  const auto bytes=hash.bytes();
  detail::require(bytes.size()>=20,"Mainline discovery requires a complete selected identity");
  return lt::sha1_hash(reinterpret_cast<const char*>(bytes.data()));
}
void startMainline() noexcept {
  if (!network || !networkBinding || network->mode!=NetworkMode::Normal || !network->dht) { return; }
  try {
    MainlineDhtOptions options;
    options.port=network->mainlinePort;
    options.ipv4=network->mainlineIpv4;
    options.ipv6=network->mainlineIpv6 && network->ipv6;
    options.onlyPeerBootstrap=network->mainlinePeerBootstrapOnly;
    options.restoreIdentity=network->mainlineRestoreIdentity;
    if (mainlineBootstrap) { options.bootstrapNodes=*mainlineBootstrap; }
    // Optional state never changes route policy. Unsupported proxies do not
    // even open these files; the service itself reports unavailable below.
    if (mainlineLease && network->proxy.kind==NetworkProxyKind::None) {
      auto cached=storage::MainlineCheckpoint::load(*mainlineLease,network->mainlineDirectory);
      if (cached) {
        mainlineCheckpoint.emplace(std::move(*cached));
        if (mainlineCheckpoint->state()) { options.restoredState=*mainlineCheckpoint->state(); }
      } else { mainlineStatus.checkpointFailure=cached.error(); }
      const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      auto legacy=storage::readLegacyMldhtHints(*mainlineLease,network->mainlineDirectory,now);
      if (legacy) {
        for (const auto& hint:legacy->endpoints) {
          const bool ipv6=hint.address.find(':')!=std::string::npos;
          if ((ipv6 && options.ipv6) || (!ipv6 && options.ipv4)) { options.restoredNodes.emplace_back(hint.address,hint.port); }
        }
      } else { mainlineStatus.checkpointFailure=legacy.error(); }
    }
    auto created=MainlineDhtService::create(networkBinding->effective,options);
    if (!created && !options.restoredState.empty() && created.error().code==storage::ErrorCode::InvalidFormat) {
      // Known optional-cache corruption only. Preserve the file and revoke
      // writes; repeat with identical routes and no untrusted saved state.
      mainlineStatus.checkpointFailure=created.error(); mainlineCheckpoint.reset();
      options.restoredState.clear();
      created=MainlineDhtService::create(networkBinding->effective,options);
    }
    if (!created) {
      mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
      mainlineStatus.failure=created.error(); return;
    }
    mainline=std::move(*created);
    mainlineStatus.checkpointLoaded=!options.restoredState.empty();
    mainlineStatus.legacyHints=options.restoredNodes.size();
    mainlineStatus.state=MainlineDiscoveryStatus::State::Running;
    mainlineCheckpointNext=Clock::now()+5min;
  } catch (...) {
    mainline.reset(); mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
    try { mainlineStatus.failure=storage::Error{storage::ErrorCode::Io,"Mainline service startup failed"}; } catch (...) {}
  }
}
void persistMainline() noexcept {
  if (!mainline || !mainlineCheckpoint || !mainlineLease || !mainlineLease->valid()
      || mainline->status().phase!=MainlineDhtPhase::Listening) { return; }
  try {
    auto snapshot=mainline->checkpoint();
    if (!snapshot) { mainlineStatus.checkpointFailure=snapshot.error(); mainlineCheckpoint.reset(); return; }
    auto saved=mainlineCheckpoint->save(*mainlineLease,*snapshot);
    if (!saved) { mainlineStatus.checkpointFailure=saved.error(); mainlineCheckpoint.reset(); return; }
    mainlineStatus.checkpointSaved=true;
  } catch (...) {
    mainlineCheckpoint.reset();
    try { mainlineStatus.checkpointFailure=storage::Error{storage::ErrorCode::Io,"Optional Mainline checkpoint failed; original data preserved"}; } catch (...) {}
  }
}
bool discoverable(const Managed& value) const {
  return value.status.retained && !value.stop && !value.removing && !value.manualPaused
      && !value.policyHeld && !value.status.verificationHeld && value.handle.is_valid()
      && (value.status.phase==ManagedPhase::Downloading || value.status.phase==ManagedPhase::Seeding)
      && value.storage.mainlinePolicy().discover
      && (!network->mainlineBackupOnly || !value.mainlineTrackerHealthy);
}
bool discoverable(const Magnet& value) const {
  return value.status.retained && !value.cancel && !value.shutdown && !value.removing
      && value.status.phase==MagnetPhase::Resolving && value.handle.is_valid();
}
void advanceMainline() noexcept {
  if (!mainline || closingAdmission || poisoned || closed) { return; }
  try {
    const auto now=Clock::now();
    if (now>=mainlineCheckpointNext) { persistMainline(); mainlineCheckpointNext=now+5min; }
    std::vector<lt::sha1_hash> current;
    const auto retain=[&](const lt::sha1_hash& hash) {
      if (std::ranges::find(current,hash)==current.end() && current.size()<MainlineDhtService::MaximumActiveHashes) { current.push_back(hash); }
      return std::ranges::find(current,hash)!=current.end();
    };
    // Revoke obsolete queries before spending the bounded active-hash budget.
    for (const auto& hash:mainlineActive) {
      const bool managedCurrent=std::ranges::any_of(managed,[&](const auto& item){return discoverable(*item.second) && discoveryHash(item.second->storage.identity().originalKey)==hash;});
      const bool magnetCurrent=std::ranges::any_of(magnets,[&](const auto& item){return discoverable(*item.second) && discoveryHash(item.second->intent.hash)==hash;});
      if (!managedCurrent && !magnetCurrent) { static_cast<void>(mainline->forget(hash)); }
    }
    for (auto& [key,entry]:managed) {
      static_cast<void>(key); auto& value=*entry;
      if (!discoverable(value)) { continue; }
      const auto hash=discoveryHash(value.storage.identity().originalKey);
      if (!retain(hash)) { continue; }
      if (now>=value.mainlineNext) {
        if (mainline->lookup(hash)) { ++mainlineStatus.lookups; }
        const bool announce=value.storage.mainlinePolicy().announce
            && (!network->mainlineBackupOnly || !value.mainlineTrackerHealthy);
        const auto port=session->listen_port();
        if (announce && network->incomingTcp && port>0 && mainline->announce(hash,port)) { ++mainlineStatus.announces; }
        value.mainlineNext=now+15min;
      }
    }
    for (auto& [key,entry]:magnets) {
      static_cast<void>(key); auto& value=*entry;
      if (!discoverable(value)) { continue; }
      const auto hash=discoveryHash(value.intent.hash);
      if (!retain(hash)) { continue; }
      if (now>=value.mainlineNext) {
        for (const auto& node:value.intent.dhtNodes) { static_cast<void>(mainline->addNode(node)); }
        if (mainline->lookup(hash)) { ++mainlineStatus.lookups; }
        value.mainlineNext=now+15min; // Metadata discovery NEVER announces.
      }
    }
    for (const auto& hash:mainlineActive) {
      if (std::ranges::find(current,hash)==current.end()) { static_cast<void>(mainline->forget(hash)); }
    }
    mainlineActive=std::move(current);
    auto replies=mainline->poll();
    if (!replies) {
      mainlineStatus.failure=replies.error(); mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
      mainline.reset(); return;
    }
    // Peer hints cannot authorize storage, metadata, network changes or a stale
    // handle. Re-evaluate the current admission at delivery, after stop/retire.
    for (const auto& reply:*replies) {
      for (auto& [key,entry]:managed) {
        static_cast<void>(key); auto& value=*entry;
        if (!discoverable(value) || discoveryHash(value.storage.identity().originalKey)!=reply.hash) { continue; }
        for (const auto& peer:reply.peers) { value.handle.connect_peer(peer,lt::peer_info::dht); ++mainlineStatus.deliveredPeers; }
      }
      for (auto& [key,entry]:magnets) {
        static_cast<void>(key); auto& value=*entry;
        if (!discoverable(value) || discoveryHash(value.intent.hash)!=reply.hash) { continue; }
        for (const auto& peer:reply.peers) { value.handle.connect_peer(peer,lt::peer_info::dht); ++mainlineStatus.deliveredPeers; }
      }
    }
  } catch (...) {
    mainline.reset(); mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
    try { mainlineStatus.failure=storage::Error{storage::ErrorCode::Io,"Mainline service failed; torrent owner remains available"}; } catch (...) {}
  }
}
