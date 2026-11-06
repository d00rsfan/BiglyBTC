// Included in the actual Host test's private slots. No Mode/grant/factory hook.
  void normalProfileBootstrapsIdentityOnly_data(){
    QTest::addColumn<bool>("coexist");QTest::newRow("pure-normal")<<false;QTest::newRow("mixed-normal")<<true;
  }
  void normalProfileBootstrapsIdentityOnly(){
    QFETCH(bool,coexist);
    if(coexist&&!engine::PersistentSession::mixedNetworksAvailable()){QSKIP("Actual qualified mixed role unavailable");}
    namespace test=owned_i2p_test;test::Fixture f;
    auto inventory=core::Document::parse(test::read(f.path/"downloads.config"));QVERIFY(inventory);auto library=core::readLibrary(*inventory);QVERIFY(library);
    auto row=core::Document::parse(core::encodeValue(inventory->find("downloads")->children[0]).value());QVERIFY(row&&row->replaceValue("state","i70e"));QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));test::write(f.path/"downloads.config",inventory->serialize());
    auto config=core::Document::parse(test::read(f.path/"biglybt.config"));QVERIFY(config);
    for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){QVERIFY(config->replaceValue(key,"i0e"));}
    QVERIFY(config->replaceValue("Bind IP",test::b("127.0.0.1")));QVERIFY(config->replaceValue("Enforce Bind IP","i1e"));test::write(f.path/"biglybt.config",config->serialize());
    const auto beforeConfig=test::read(f.path/"biglybt.config"),beforeState=test::read(f.state());
    const auto payload=std::filesystem::path(f.payloadDirectory.path().toStdString())/"owned.bin";QVERIFY(!std::filesystem::exists(payload));
    QVERIFY(std::filesystem::remove(f.key));f.lease.reset();QTcpServer router;QVERIFY(router.listen(QHostAddress::LocalHost,0));QSignalSpy connections(&router,&QTcpServer::newConnection);
    app::I2pServiceOptions options;options.routerPort=router.serverPort();options.publicCoexistence=coexist;app::SessionBridge bridge(nullptr,{},{},{},options);
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),packets(&bridge,&app::SessionBridge::i2pReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    const bool supported=app::OwnedI2pService::available();QCOMPARE(std::filesystem::exists(f.key),supported);QCOMPARE(std::filesystem::exists(f.key.parent_path()/"dht1.config"),supported);
    if(supported){struct stat st{};QVERIFY(::stat(f.key.c_str(),&st)==0);QCOMPARE(st.st_mode&07777,mode_t(0600));const auto key=test::read(f.key),properties=test::read(f.key.parent_path()/"dht1.config");QVERIFY(!key.empty()&&!properties.empty());
      if(coexist){QVERIFY(std::filesystem::exists(f.key.parent_path()/"dest_key.dat"));QVERIFY(std::filesystem::exists(f.key.parent_path()/"dht.config"));QVERIFY(test::read(f.key.parent_path()/"dest_key.dat")!=key);}
      QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());
      const auto old=st;QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2,10000);
      QVERIFY(::stat(f.key.c_str(),&st)==0);QCOMPARE(st.st_ino,old.st_ino);QCOMPARE(st.st_mtim.tv_sec,old.st_mtim.tv_sec);QCOMPARE(st.st_mtim.tv_nsec,old.st_mtim.tv_nsec);QCOMPARE(test::read(f.key),key);QCOMPARE(test::read(f.key.parent_path()/"dht1.config"),properties);
    }
    for(const auto& signal:packets){const auto packet=qvariant_cast<ui::I2pPresentation>(signal[0]);QVERIFY(packet);QVERIFY(packet->torrents.empty());QCOMPARE(packet->trackerAnnounces,quint64(0));}
    for(const auto& signal:snapshots){const auto snapshot=qvariant_cast<ui::Presentation>(signal[0]);QVERIFY(snapshot);for(const auto& item:snapshot->items){QCOMPARE(item.providerAdmission,quint64(0));QCOMPARE(item.providerGeneration,quint64(0));}}
    QCOMPARE(connections.count(),0);QCOMPARE(test::read(f.path/"biglybt.config"),beforeConfig);QCOMPARE(test::read(f.state()),beforeState);QVERIFY(!std::filesystem::exists(payload));
    const auto expected=closed.count()+1;QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),expected,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());auto lease=storage::ProfileLease::acquire(f.path);QVERIFY(lease);
  }
  void previewDisabledAndPendingBootstrapNeverClaimAReadyOwner_data(){
    QTest::addColumn<int>("kind");for(int kind=0;kind<5;++kind){QTest::newRow(std::to_string(kind).c_str())<<kind;}
  }
  void previewDisabledAndPendingBootstrapNeverClaimAReadyOwner(){
    QFETCH(int,kind);namespace test=owned_i2p_test;test::Fixture f;
    auto inventory=core::Document::parse(test::read(f.path/"downloads.config"));QVERIFY(inventory);auto row=core::Document::parse(core::encodeValue(inventory->find("downloads")->children[0]).value());QVERIFY(row&&row->replaceValue("state","i70e"));QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));test::write(f.path/"downloads.config",inventory->serialize());
    if(kind>0&&kind<4){auto config=core::Document::parse(test::read(f.path/"biglybt.config"));QVERIFY(config);const char* key=kind==1?"PluginInfo.azneti2phelper.enabled":kind==2?"Plugin.azneti2phelper.enable":"Plugin.azneti2phelper.azi2phelper.pure.enabled";QVERIFY(config->replaceValue(key,"i0e"));test::write(f.path/"biglybt.config",config->serialize());}
    QVERIFY(std::filesystem::remove(f.key));const auto marker=f.key.parent_path()/".BiglyBTC.i2p-bootstrap1";
    if(kind==4){test::write(marker,"preserved incomplete bootstrap");QVERIFY(::chmod(marker.c_str(),0600)==0);}
    const auto config=test::read(f.path/"biglybt.config"),state=test::read(f.state());f.lease.reset();QTcpServer router;QVERIFY(router.listen(QHostAddress::LocalHost,0));QSignalSpy connections(&router,&QTcpServer::newConnection);
    app::I2pServiceOptions options;options.routerPort=router.serverPort();app::SessionBridge bridge(nullptr,{},{},{},options);QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),packets(&bridge,&app::SessionBridge::i2pReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(kind==0?bridge.open(f.path,0):bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    QVERIFY(!std::filesystem::exists(f.key));QVERIFY(!std::filesystem::exists(f.key.parent_path()/"dht1.config"));QCOMPARE(connections.count(),0);QCOMPARE(test::read(f.path/"biglybt.config"),config);QCOMPARE(test::read(f.state()),state);
    if(kind==4&&app::OwnedI2pService::available()){QVERIFY(!packets.empty());const auto packet=qvariant_cast<ui::I2pPresentation>(packets.last()[0]);QVERIFY(packet);QVERIFY(packet->state.contains(QStringLiteral("review")));QVERIFY(!packet->failure.isEmpty());QCOMPARE(test::read(marker),std::string("preserved incomplete bootstrap"));}
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());auto lease=storage::ProfileLease::acquire(f.path);QVERIFY(lease);
  }
  void cancelledNormalBootstrapJoinsAndNeverStartsAnOwner(){
    namespace test=owned_i2p_test;test::Fixture f;test::write(f.path/"downloads.config","d9:downloadslee");QVERIFY(std::filesystem::remove(f.key));f.lease.reset();
    QTcpServer router;QVERIFY(router.listen(QHostAddress::LocalHost,0));QSignalSpy connections(&router,&QTcpServer::newConnection);app::I2pServiceOptions options;options.routerPort=router.serverPort();app::SessionBridge bridge(nullptr,{},{},{},options);QSignalSpy closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.path));bridge.cancel();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QCOMPARE(connections.count(),0);
    if(std::filesystem::exists(f.key)){QVERIFY(std::filesystem::exists(f.key.parent_path()/"dht1.config")||std::filesystem::exists(f.key.parent_path()/".BiglyBTC.i2p-bootstrap1"));}
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());auto lease=storage::ProfileLease::acquire(f.path);QVERIFY(lease);
  }
  void laterKeyLossRefusesActualStartWithoutRegeneratingIdentity(){
#if BIGLYBTC_HOST_I2P_OWNER
    namespace test=owned_i2p_test;test::Fixture f;auto inventory=core::Document::parse(test::read(f.path/"downloads.config"));QVERIFY(inventory);auto row=core::Document::parse(core::encodeValue(inventory->find("downloads")->children[0]).value());QVERIFY(row&&row->replaceValue("state","i70e"));QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));test::write(f.path/"downloads.config",inventory->serialize());
    QVERIFY(std::filesystem::remove(f.key));f.lease.reset();QTcpServer router;QVERIFY(router.listen(QHostAddress::LocalHost,0));QSignalSpy connections(&router,&QTcpServer::newConnection);app::I2pServiceOptions options;options.routerPort=router.serverPort();app::SessionBridge bridge(nullptr,{},{},{},options);
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&!snapshots.empty(),10000);QVERIFY(std::filesystem::exists(f.key));const auto properties=test::read(f.key.parent_path()/"dht1.config"),state=test::read(f.state());QVERIFY(std::filesystem::remove(f.key));
    const auto snapshot=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(snapshot&&!snapshot->items.empty());const auto item=std::find_if(snapshot->items.begin(),snapshot->items.end(),[&](const auto& value){return value.hash==QString::fromStdString(f.hash.hex());});QVERIFY(item!=snapshot->items.end());
    QVERIFY(bridge.downloadAction({ui::Target{item->hash,snapshot->revision}},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    QVERIFY(std::any_of(messages.begin(),messages.end(),[](const auto& signal){return signal[0].toString().contains(QStringLiteral("identity"),Qt::CaseInsensitive);}));
    const auto saved=core::Document::parse(test::read(f.path/"downloads.config"));QVERIFY(saved);const auto library=core::readLibrary(*saved);QVERIFY(library&&!library->downloads.empty());QVERIFY(library->downloads[0].startup!=core::StartupState::Stopped);
    QVERIFY(!std::filesystem::exists(f.key));QCOMPARE(test::read(f.key.parent_path()/"dht1.config"),properties);QCOMPARE(test::read(f.state()),state);QCOMPARE(connections.count(),0);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());auto lease=storage::ProfileLease::acquire(f.path);QVERIFY(lease);
#else
    QSKIP("Actual optional owner is unavailable; stock refusal is covered separately");
#endif
  }

  void pendingBootstrapDoesNotBlockTheOtherSelectedIdentity_data(){
    QTest::addColumn<bool>("pendingPure");
    QTest::newRow("pending-pure-valid-mixed")<<true;
    QTest::newRow("pending-mixed-valid-pure")<<false;
  }
  void pendingBootstrapDoesNotBlockTheOtherSelectedIdentity(){
#if BIGLYBTC_HOST_I2P_OWNER
    QFETCH(bool,pendingPure);
    if(!app::OwnedI2pService::available()||!engine::PersistentSession::mixedNetworksAvailable()){QSKIP("Actual qualified coexistence roles unavailable");}
    namespace test=owned_i2p_test;test::Fixture f;
    auto inventory=core::Document::parse(test::read(f.path/"downloads.config"));QVERIFY(inventory);
    auto row=core::Document::parse(core::encodeValue(inventory->find("downloads")->children[0]).value());QVERIFY(row&&row->replaceValue("state","i70e"));
    QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));test::write(f.path/"downloads.config",inventory->serialize());
    if(pendingPure){auto state=core::Document::parse(test::read(f.state()));QVERIFY(state);QVERIFY(state->replaceValue("attributes",test::dict({{"networks","l6:Public3:I2Pe"},{"peersources","l7:Trackere"}})));test::write(f.state(),state->serialize());}
    auto config=core::Document::parse(test::read(f.path/"biglybt.config"));QVERIFY(config);
    for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){QVERIFY(config->replaceValue(key,"i0e"));}
    QVERIFY(config->replaceValue("Bind IP",test::b("127.0.0.1")));QVERIFY(config->replaceValue("Enforce Bind IP","i1e"));test::write(f.path/"biglybt.config",config->serialize());
    const auto beforeConfig=config->serialize(),beforeState=test::read(f.state()),beforeInventory=test::read(f.path/"downloads.config");
    const auto directory=f.key.parent_path();QVERIFY(std::filesystem::remove(f.key));f.lease.reset();
    QTcpServer router;QVERIFY(router.listen(QHostAddress::LocalHost,0));QSignalSpy connections(&router,&QTcpServer::newConnection);
    app::I2pServiceOptions options;options.routerPort=router.serverPort();options.publicCoexistence=true;app::SessionBridge bridge(nullptr,{},{},{},options);
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),packets(&bridge,&app::SessionBridge::i2pReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    // Both identities come from the actual startup producer, not a test key setter.
    QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,10000);
    for(const auto* name:{"dest_key.dat","dest_key1.dat","dht.config","dht1.config"}){QVERIFY(std::filesystem::exists(directory/name));}
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());
    const std::string pendingSuffix=pendingPure?"1":"",healthySuffix=pendingPure?"":"1";
    const auto pendingKey=directory/("dest_key"+pendingSuffix+".dat"),pendingConfig=directory/("dht"+pendingSuffix+".config");
    const auto oldKey=test::read(pendingKey),oldProperties=test::read(pendingConfig);struct stat oldKeyStat{};QVERIFY(::stat(pendingKey.c_str(),&oldKeyStat)==0);
    const auto healthyKey=directory/("dest_key"+healthySuffix+".dat"),healthyConfig=directory/("dht"+healthySuffix+".config");
    QVERIFY(std::filesystem::remove(healthyKey));QVERIFY(std::filesystem::remove(healthyConfig));
    const auto marker=directory/(".BiglyBTC.i2p-bootstrap"+pendingSuffix);const std::string markerBytes="preserved independent-index review";
    test::write(marker,markerBytes);QVERIFY(::chmod(marker.c_str(),0600)==0);const auto markerTime=std::filesystem::last_write_time(marker);
    const auto firstPackets=packets.count();QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2,10000);
    // A Pure error comes first in the producer loop; Mixed must still be created.
    // The converse proves a later Mixed error does not discard the valid Pure work.
    QVERIFY(std::filesystem::exists(healthyKey));QVERIFY(std::filesystem::exists(healthyConfig));
    QVERIFY(!std::filesystem::exists(directory/(".BiglyBTC.i2p-bootstrap"+healthySuffix)));
    QVERIFY(test::read(pendingKey)==oldKey);QVERIFY(test::read(pendingConfig)==oldProperties);QCOMPARE(test::read(marker),markerBytes);QVERIFY(std::filesystem::last_write_time(marker)==markerTime);
    struct stat afterKeyStat{};QVERIFY(::stat(pendingKey.c_str(),&afterKeyStat)==0);QCOMPARE(afterKeyStat.st_ino,oldKeyStat.st_ino);QCOMPARE(afterKeyStat.st_mtim.tv_sec,oldKeyStat.st_mtim.tv_sec);QCOMPARE(afterKeyStat.st_mtim.tv_nsec,oldKeyStat.st_mtim.tv_nsec);
    QVERIFY(packets.count()>firstPackets);const auto review=qvariant_cast<ui::I2pPresentation>(packets.last()[0]);QVERIFY(review);QVERIFY(review->failure.contains(pendingPure?QStringLiteral("Pure I2P: "):QStringLiteral("Mixed I2P: ")));
    QCOMPARE(test::read(f.path/"biglybt.config"),beforeConfig);QCOMPARE(test::read(f.path/"downloads.config"),beforeInventory);QCOMPARE(test::read(f.state()),beforeState);QCOMPARE(connections.count(),0);
    QVERIFY(!std::filesystem::exists(std::filesystem::path(f.payloadDirectory.path().toStdString())/"owned.bin"));
    // Actual explicit Start must reach the unaffected route's I2CP channel. This
    // checks the later factory latch too; the loopback peer sends no fake reply.
    QVERIFY(!snapshots.empty());const auto snapshot=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(snapshot);
    const auto selected=std::find_if(snapshot->items.begin(),snapshot->items.end(),[&](const auto& item){return item.hash==QString::fromStdString(f.hash.hex());});QVERIFY(selected!=snapshot->items.end());
    QVERIFY(bridge.downloadAction({ui::Target{selected->hash,snapshot->revision}},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&connections.count()>0,10000);
    QVERIFY(test::read(pendingKey)==oldKey);QVERIFY(test::read(pendingConfig)==oldProperties);QCOMPARE(test::read(marker),markerBytes);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());
    auto held=storage::ProfileLease::acquire(f.path);QVERIFY(held);
#else
    QSKIP("Actual optional owner is unavailable; stock refusal remains covered separately");
#endif
  }
