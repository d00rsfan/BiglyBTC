  std::vector<engine::SubscriptionSeedHash> subscriptionSeedHashes;
  std::vector<engine::SubscriptionSeedHash> subscriptionPublicationHashes;
  bool subscriptionSeedListening=false;
  std::map<std::string,bool> subscriptionSeedPopularity;
  std::chrono::steady_clock::time_point nextSubscriptionSeedScan{};
  std::chrono::steady_clock::time_point nextI2pSubscriptionSeedScan{};
  std::optional<core::TorrentHash> lastI2pSubscriptionBootstrap;
  struct SubscriptionUpgradePrompt {
    search::I2pSubscriptionUpgrade update;
    quint64 request;
    std::chrono::steady_clock::time_point expires;
    bool downloading=false;
  };
  std::optional<SubscriptionUpgradePrompt> subscriptionUpgradePrompt;
  quint64 nextSubscriptionUpgradeRequest=0;
  std::map<std::string,std::uint32_t> promptedSubscriptionVersions;
  const std::int64_t subscriptionSeedCreationSeconds=wallMillis()/1000;
  const std::int64_t subscriptionPresenceNonce=static_cast<std::int64_t>(QRandomGenerator::system()->generate64()>>1);

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
      lastI2pSubscriptionBootstrap.reset();
      if (i2p) { i2p->updateSubscriptionSeeds({},{}); }
      return;
    }
    const auto now=std::chrono::steady_clock::now();
    if (now<nextI2pSubscriptionSeedScan) { return; }
    nextI2pSubscriptionSeedScan=now+std::chrono::seconds(5);
    constexpr auto network=search::SubscriptionSeedNetwork::I2P;
    const auto saved=search::savedSubscriptionSeedCandidates(lease,network);
    std::vector<core::TorrentHash> candidates;
    auto serving=std::make_shared<std::set<std::string,std::less<>>>();
    if (saved) {
      for (const auto& candidate : *saved) {
        const auto hash=core::TorrentHash::parse({candidate.hash.data(),candidate.hash.size()});
        if (!hash) { continue; }
        candidates.push_back(*hash);
        if (selectSubscriptionSeed(candidate)) { serving->emplace(candidate.hash.data(),candidate.hash.size()); }
      }
    }
    if (candidates.empty()) {
      lastI2pSubscriptionBootstrap.reset();
      if (i2p) { i2p->updateSubscriptionSeeds({},{}); }
      return;
    }
    auto alias=lease.retain();
    if (!alias) { return; }
    const engine::HeldSubscriptionSeedResolver resolve=
        [profile=std::move(*alias),serving=std::move(serving),creation=subscriptionSeedCreationSeconds,
            nonce=subscriptionPresenceNonce](const storage::ProfileSourceAccess& access,
            std::string_view key,engine::SubscriptionSeedRead read)->std::optional<engine::SubscriptionSeedSource> {
      const auto operation=read==engine::SubscriptionSeedRead::Prepare
          ?search::SubscriptionSeedFile::Prepare:search::SubscriptionSeedFile::Verify;
      auto seed=search::resolveVersionedSubscriptionSeed(*profile,access,key,network,creation,operation);
      if (!seed || !*seed) { return {}; }
      auto& checked=**seed;
      engine::SubscriptionSeedSource source;
      source.servePayload=serving->contains(key);
      if (checked.metainfo.presence) {
        const auto& presence=*checked.metainfo.presence;
        auto wire=search::encodeSubscriptionPresence(presence,nonce);
        if (wire) {
          source.presence=engine::SubscriptionSeedPublication{presence.dhtKey,std::move(*wire),
              static_cast<std::int32_t>(presence.version),presence.applicationKey};
          for (auto& record:checked.associations) {
            source.associations.push_back({record.dhtKey,{record.value.begin(),record.value.end()},
                static_cast<std::int32_t>(presence.version),std::move(record.applicationKey)});
          }
        }
        if (source.servePayload&&checked.ddbTransferAllowed) {
          auto response=search::subscriptionSeedDdbResponse(checked.metainfo);
          if (response) {
            source.ddbMetainfo=engine::SubscriptionSeedMetainfo{checked.metainfo.lookupKey,std::move(*response)};
          }
        }
      }
      if (checked.upgrade) {
        auto& probe=*checked.upgrade;
        source.upgrade=engine::SubscriptionSeedUpgradeProbe{
            std::move(probe.publicKey),std::move(probe.associations),probe.minimumVersion};
      }
      source.metainfo=std::move(checked.metainfo.metainfo);
      source.directory=std::move(checked.directory);
      source.directoryHandle=std::move(checked.directoryHandle);
      return source;
    };
    if (!i2p) {
      const auto config=storage::ConfigFile::load(lease,"biglybt.config");
      if (!config) { return; }
      const auto options=readSavedI2pOptions(config->document(),*i2pOptions);
      if (!options) { return; }
      auto seedOptions=*options;
      ensureCommonTagRates();
      seedOptions.externalQueue=coexistence()||(privateOnly()&&commonTagRates);
      const auto first=engine::nextSubscriptionSeedCandidate(candidates,lastI2pSubscriptionBootstrap);
      const auto count=std::min(candidates.size(),engine::PrivateSubscriptionCandidateLimit);
      for (std::size_t offset=0;offset<count;++offset) {
        const auto& hash=candidates[(first+offset)%candidates.size()];
        lastI2pSubscriptionBootstrap=hash;
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
    if (mixed) {
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
      mixed->setSubscriptionRelay(profileReady&&chats?chats->subscriptionRelay(lease):OwnedMixedI2pService::SubscriptionRelay{});
#endif
    }
    pollI2pSubscriptionSeeds(profileReady);
    if(!profileReady||!engine) {
      subscriptionSeedHashes.clear();
      subscriptionPublicationHashes.clear();
      subscriptionSeedListening=false;
      nextSubscriptionSeedScan={};
      if(engine) { engine->refreshPublicSubscriptionSeeds({},{}); }
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
      if(chats) { chats->updateSubscriptionCatalog({},{}); }
#endif
      return;
    }
    const auto now=std::chrono::steady_clock::now();
    const bool refreshed=now>=nextSubscriptionSeedScan;
    if(refreshed) {
      nextSubscriptionSeedScan=now+std::chrono::seconds(5);
      subscriptionSeedHashes.clear();
      subscriptionPublicationHashes.clear();
      const auto saved=search::savedSubscriptionSeedCandidates(lease,search::SubscriptionSeedNetwork::Public);
      if(saved) {
        for(const auto& candidate:*saved) {
          subscriptionPublicationHashes.push_back(candidate.hash);
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
    const bool listenerChanged=std::exchange(subscriptionSeedListening,observing)!=observing;
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
    if(chats&&(listenerChanged||refreshed)) {
      const auto active=observing?std::span<const engine::SubscriptionSeedHash>(subscriptionSeedHashes)
          :std::span<const engine::SubscriptionSeedHash>{};
      chats->updateSubscriptionCatalog(subscriptionPublicationHashes,active,subscriptionSeedCreationSeconds);
    }
#else
    static_cast<void>(listenerChanged);
#endif
  }
