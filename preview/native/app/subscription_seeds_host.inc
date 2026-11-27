  std::vector<engine::SubscriptionSeedHash> subscriptionSeedHashes;
  std::map<std::string,bool> subscriptionSeedPopularity;
  std::chrono::steady_clock::time_point nextSubscriptionSeedScan{};
  std::chrono::steady_clock::time_point nextI2pSubscriptionSeedScan{};
  const std::int64_t subscriptionSeedCreationSeconds=wallMillis()/1000;

  bool selectSubscriptionSeed(const search::SavedSubscriptionSeedCandidate& candidate) {
    if (candidate.popularity<=100) { return true; }
    auto [decision,inserted]=subscriptionSeedPopularity.try_emplace(candidate.publicKey,false);
    if (inserted) {
      const auto buckets=1+static_cast<std::uint64_t>(candidate.popularity-1)/100;
      std::uniform_int_distribution<std::uint64_t> choose(0,buckets-1);
      decision->second=choose(*QRandomGenerator::global())==0;
    }
    return decision->second;
  }

  void pollI2pSubscriptionSeeds(bool profileReady) {
    if (!i2pOptions || !OwnedI2pService::available()) { return; }
    if (!profileReady || !tagManagerAtOpen || bootstrapFailure(false)) {
      nextI2pSubscriptionSeedScan={};
      if (i2p) { i2p->updateSubscriptionSeeds({},{}); }
      return;
    }
    const auto now=std::chrono::steady_clock::now();
    if (now<nextI2pSubscriptionSeedScan) { return; }
    nextI2pSubscriptionSeedScan=now+std::chrono::seconds(5);
    constexpr auto network=search::SubscriptionSeedNetwork::I2P;
    const auto saved=search::savedSubscriptionSeedCandidates(lease,network);
    std::vector<core::TorrentHash> candidates;
    if (saved) {
      for (const auto& candidate : *saved) {
        if (!selectSubscriptionSeed(candidate)) { continue; }
        const auto hash=core::TorrentHash::parse({candidate.hash.data(),candidate.hash.size()});
        if (hash) { candidates.push_back(*hash); }
        if (candidates.size()==engine::PrivateSubscriptionCandidateLimit) { break; }
      }
    }
    if (candidates.empty()) {
      if (i2p) { i2p->updateSubscriptionSeeds({},{}); }
      return;
    }
    auto alias=lease.retain();
    if (!alias) { return; }
    const engine::HeldSubscriptionSeedResolver resolve=
        [profile=std::move(*alias),creation=subscriptionSeedCreationSeconds](const storage::ProfileSourceAccess& access,
            std::string_view key,engine::SubscriptionSeedRead read)->std::optional<engine::SubscriptionSeedSource> {
      const auto operation=read==engine::SubscriptionSeedRead::Prepare
          ?search::SubscriptionSeedFile::Prepare:search::SubscriptionSeedFile::Verify;
      auto seed=search::resolveVersionedSubscriptionSeed(*profile,access,key,network,creation,operation);
      if (!seed || !*seed) { return {}; }
      return engine::SubscriptionSeedSource{std::move((**seed).metainfo.metainfo),
          std::move((**seed).directory),std::move((**seed).directoryHandle)};
    };
    if (!i2p) {
      const auto config=storage::ConfigFile::load(lease,"biglybt.config");
      if (!config) { return; }
      const auto options=readSavedI2pOptions(config->document(),*i2pOptions);
      if (!options) { return; }
      auto seedOptions=*options;
      ensureCommonTagRates();
      seedOptions.externalQueue=coexistence()||(privateOnly()&&commonTagRates);
      for (const auto& hash : candidates) {
        auto started=OwnedI2pService::startSubscription(lease,hash,resolve,seedOptions,&*tagManagerAtOpen);
        if (started) {
          i2p=std::move(*started);
          coexistQueue.privateOwnerChanged();
          i2pJoined=false;
          break;
        }
        if (started.error().code!=storage::ErrorCode::Missing) { break; }
      }
    }
    if (i2p) { i2p->updateSubscriptionSeeds(candidates,resolve); }
  }

  void pollSubscriptionSeeds(bool profileReady) {
    pollI2pSubscriptionSeeds(profileReady);
    if(!engine) { return; }
    if(!profileReady||!engine->publicSubscriptionSeedsAvailable()) {
      subscriptionSeedHashes.clear();
      nextSubscriptionSeedScan={};
      engine->refreshPublicSubscriptionSeeds({},{});
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
      if(chats) { chats->updateSubscriptionSeeds({}); }
#endif
      return;
    }
    const auto now=std::chrono::steady_clock::now();
    const bool refreshed=now>=nextSubscriptionSeedScan;
    if(refreshed) {
      nextSubscriptionSeedScan=now+std::chrono::seconds(5);
      subscriptionSeedHashes.clear();
      const auto saved=search::savedSubscriptionSeedCandidates(lease,search::SubscriptionSeedNetwork::Public);
      if(saved) {
        for(const auto& candidate:*saved) {
          if (selectSubscriptionSeed(candidate)) { subscriptionSeedHashes.push_back(candidate.hash); }
        }
      }
    }
    const auto observing=engine->refreshPublicSubscriptionSeeds(subscriptionSeedHashes,
        [this](std::string_view key,engine::SubscriptionSeedRead mode)->std::optional<engine::SubscriptionSeedSource> {
          constexpr auto network=search::SubscriptionSeedNetwork::Public;
          const auto operation=mode==engine::SubscriptionSeedRead::Prepare
              ?search::SubscriptionSeedFile::Prepare:search::SubscriptionSeedFile::Verify;
          auto seed=search::resolveVersionedSubscriptionSeed(lease,key,network,subscriptionSeedCreationSeconds,operation);
          if(seed&&*seed) {
            return engine::SubscriptionSeedSource{std::move((**seed).metainfo.metainfo),
                std::move((**seed).directory),std::move((**seed).directoryHandle)};
          }
          return {};
        });
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
    if(chats&&(!observing||refreshed)) {
      const auto active=observing?std::span<const engine::SubscriptionSeedHash>(subscriptionSeedHashes)
          :std::span<const engine::SubscriptionSeedHash>{};
      chats->updateSubscriptionSeeds(active,subscriptionSeedCreationSeconds);
    }
#else
    static_cast<void>(observing);
#endif
  }
