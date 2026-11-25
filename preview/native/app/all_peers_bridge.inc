// SPDX-License-Identifier: GPL-2.0-or-later
// Passive connection inspection and offline location data.
void SessionBridge::setPeersActive(bool active){peersActive_.store(active,std::memory_order_relaxed);}
void SessionBridge::publishPeers(){
 if(!host_||!peersActive_.load(std::memory_order_relaxed)||host_->peerSequence==UINT64_MAX){return;}const auto now=std::chrono::steady_clock::now();if(now<host_->nextPeerSnapshot){return;}host_->nextPeerSnapshot=now+std::chrono::seconds(2);
 auto packet=std::make_shared<ui::PeerPacket>();packet->session=host_->columnLayoutSession;packet->sequence=++host_->peerSequence;packet->prepared=host_->startupDone;packet->preparationTotal=host_->startupTotal;
 if(!host_->peerLayoutAttempted){host_->peerLayoutAttempted=true;auto config=storage::ConfigFile::load(host_->lease,"tables.config");if(config){const auto* table=config->document().find("Table.AllPeers");if(!table){table=config->document().find("TableDefault.AllPeers");}if(table){auto value=core::encodeValue(*table,storage::compatibleConfigLimits());if(value){const std::string key="Table.MyTorrents";auto wrapped=core::Document::parse("d"+std::to_string(key.size())+":"+key+*value+"e");if(wrapped){auto layouts=core::readColumnLayouts(*wrapped);if(layouts){host_->peerLayout=std::make_shared<const core::SavedTableLayout>(std::move(layouts->incomplete));}}}}}}packet->layout=host_->peerLayout;
 if(host_->engine){auto actual=host_->engine->inspectPeers();if(actual){packet->available=true;packet->admitted=actual->admitted;packet->downloading=actual->downloading;packet->seeding=actual->seeding;packet->queued=actual->queued;packet->checking=actual->checking;packet->paused=actual->paused;packet->failed=actual->failed;packet->unavailable=actual->unavailable;packet->truncated=actual->truncated;
  if(!host_->countryProviderAttempted&&countryProviderFactory_){host_->countryProviderAttempted=true;try{host_->countryProvider=countryProviderFactory_(host_->lease);}catch(...){}}
  auto* countryProvider=host_->countryProvider.get();
  auto* asnProvider=countryProvider;
  packet->countryAvailable=countryProvider&&countryProvider->available();
  if(!host_->signedCountryVersion.isEmpty()||!host_->signedCountryFailure.isEmpty()){
    // Older signed modules may lack ASN support or DB-IP filenames.
    if(!host_->peerLocationProviderAttempted&&countryProviderFactory_){
      host_->peerLocationProviderAttempted=true;
      try{host_->peerLocationProvider=countryProviderFactory_(host_->lease);}catch(...){}
    }
    if(auto* bundled=host_->peerLocationProvider.get()){
      asnProvider=bundled;
      if(!packet->countryAvailable){
        countryProvider=bundled;
        packet->countryAvailable=bundled->available();
      }
    }
  }
  struct PeerLocation {QString countryCode,countryName,asn,asnStatus;};
  QHash<QString,PeerLocation> locations;
  QHash<QString,QString> names;if(!actual->peers.empty()&&host_->lastSnapshot){for(const auto& item:host_->lastSnapshot->items){names.insert(item.hash,item.name);}}
  QHash<QString,QString> localNames;for(const auto& interface:QNetworkInterface::allInterfaces()){for(const auto& entry:interface.addressEntries()){localNames.insert(entry.ip().toString(),interface.name());}}
  for(const auto& peer:actual->peers){ui::PeerRow row;row.hash=text(peer.hash);row.name=names.value(row.hash,row.hash);row.admission=peer.admission;row.address=text(peer.address);row.localAddress=text(peer.localAddress);row.localInterface=localNames.value(row.localAddress);row.port=peer.port;row.localPort=peer.localPort;row.client=text(peer.client);row.peerId=QString::fromLatin1(QByteArray(peer.peerId.data(),qsizetype(peer.peerId.size())).toHex().toUpper());row.protocol=peer.webSeed?QStringLiteral("HTTP"):peer.utp?QStringLiteral("uTP"):QStringLiteral("TCP");row.encryption=peer.encrypted?QStringLiteral("Encrypted"):QStringLiteral("Plain");row.key=row.hash+'|'+QString::number(row.admission)+'|'+row.address+':'+QString::number(row.port)+'|'+row.localAddress+':'+QString::number(row.localPort)+'|'+row.protocol;
   row.received=peer.received;row.sent=peer.sent;row.downRate=peer.downRate;row.upRate=peer.upRate;row.protocolDownRate=peer.protocolDownRate;row.protocolUpRate=peer.protocolUpRate;row.state=peer.state;row.progressPpm=peer.progressPpm;row.pieces=peer.pieces;row.totalPieces=peer.totalPieces;row.rtt=peer.rtt;row.downloadRequests=peer.downloadRequests;row.uploadRequests=peer.uploadRequests;row.queuedBytes=peer.queuedBytes;row.incoming=peer.incoming;row.seed=peer.seed;row.interesting=peer.interesting;row.choked=peer.choked;row.remoteInterested=peer.remoteInterested;row.remoteChoked=peer.remoteChoked;row.snubbed=peer.snubbed;row.optimistic=peer.optimistic;if(peer.progressPpm>=0&&peer.totalPieces>0){row.availability=QByteArray(reinterpret_cast<const char*>(peer.availability.data()),qsizetype(peer.availability.size()));}
   QStringList sources;for(unsigned i=0;i<6;++i){if(peer.sources&(1u<<i)){sources.push_back(QString::fromLatin1(std::array{"Tracker","DHT","PEX","Local","Saved","Incoming"}[i]));}}row.source=sources.join(", ");
   auto location=locations.find(row.address);
   if(location==locations.end()){
     const QHostAddress address(row.address);
     core::PeerNumericEndpoint endpoint;
     bool ipv4=false;
     const auto v4=address.toIPv4Address(&ipv4);
     if(ipv4){
       endpoint.family=core::PeerAddressFamily::V4;
       for(std::size_t i=0;i<4;++i){endpoint.address[i]=static_cast<std::uint8_t>(v4>>(24-i*8));}
     }else if(address.protocol()==QAbstractSocket::IPv6Protocol){
       endpoint.family=core::PeerAddressFamily::V6;
       const auto v6=address.toIPv6Address();
       std::copy(std::begin(v6.c),std::end(v6.c),endpoint.address.begin());
     }
     PeerLocation result;
     if(engine::detail::classifyPeerPayloadEndpoint(endpoint)==core::PeerNetworkClass::NonLocal){
       if(packet->countryAvailable){
         const auto country=countryProvider->lookup(endpoint);
         if(country.kind==core::CountryLookupKind::Country){
           result.countryCode=QString::fromLatin1(country.code.data(),2);
           result.countryName=QString::fromUtf8(country.name.data());
         }
       }
       if(asnProvider){
         if(const auto asn=asnProvider->lookupAsn(endpoint)){
           result.asn=QStringLiteral("AS")+QString::number(asn->number);
           if(asn->organization[0]){result.asn+=' '+QString::fromUtf8(asn->organization.data());}
           result.asnStatus=tr("Offline ASN database");
         }
       }
       if(result.asn.isEmpty()){
         const auto cached=std::ranges::find_if(host_->networkIdentityConfig.saved,[&](const auto& entry){
           return services::network_identity::matches(entry,address);
         });
         if(cached!=host_->networkIdentityConfig.saved.end()){
           result.asn="AS"+cached->number+(cached->name.isEmpty()?QString{}:' '+cached->name);
           result.asnStatus=tr("Cached network information");
         }
       }
     }
     if(result.asn.isEmpty()){result.asnStatus=tr("No ASN data for this address");}
     location=locations.insert(row.address,std::move(result));
   }
   row.countryCode=location->countryCode;row.countryName=location->countryName;
   row.asn=location->asn;row.asnStatus=location->asnStatus;
   packet->rows.push_back(std::move(row));
  }
  if(const auto observed=host_->engine->latest();observed&&observed->observed){packet->sessionReceived=observed->observed->counters.payloadReceived;packet->sessionSent=observed->observed->counters.payloadSent;}
 }else{packet->failure=text(actual.error().message);}}
 else{packet->failure=tr("No active peer session");}
 {const std::lock_guard guard(progressMutex_);latestPeers_=std::move(packet);if(peerDeliveryQueued_){return;}peerDeliveryQueued_=true;}
 QMetaObject::invokeMethod(this,[this]{std::optional<ui::PeerPresentation> packet;{const std::lock_guard guard(progressMutex_);packet.swap(latestPeers_);peerDeliveryQueued_=false;}if(packet&&peersActive_.load(std::memory_order_relaxed)&&(*packet)->session==publishedColumnLayoutSession_){emit peersReady(std::move(*packet));}},Qt::QueuedConnection);
}
