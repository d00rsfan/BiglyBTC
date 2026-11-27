  std::vector<engine::SubscriptionSeedHash> subscriptionSeedHashes;
  std::map<std::string,bool> subscriptionSeedPopularity;
  std::chrono::steady_clock::time_point nextSubscriptionSeedScan{};
  const std::int64_t subscriptionSeedCreationSeconds=wallMillis()/1000;

  void pollSubscriptionSeeds(bool profileReady) {
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
          if(candidate.popularity>100) {
            auto [decision,inserted]=subscriptionSeedPopularity.try_emplace(candidate.publicKey,false);
            if(inserted) {
              const auto buckets=1+static_cast<std::uint64_t>(candidate.popularity-1)/100;
              std::uniform_int_distribution<std::uint64_t> choose(0,buckets-1);
              decision->second=choose(*QRandomGenerator::global())==0;
            }
            if(!decision->second) { continue; }
          }
          subscriptionSeedHashes.push_back(candidate.hash);
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
            return engine::SubscriptionSeedSource{std::move((**seed).metainfo.metainfo),std::move((**seed).directory)};
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
