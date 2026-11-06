// Real generated profile/source readers; no Mode or EffectAuthority setter.
  void pendingNativeBootstrapNeverBecomesAnOriginalSource_data(){
    QTest::addColumn<bool>("mixed");QTest::addColumn<int>("part");
    for(bool mixed:{false,true}){for(int part=0;part<3;++part){const auto label=QByteArray::number(part)+(mixed?"-mixed":"-pure");QTest::newRow(label.constData())<<mixed<<part;}}
  }
  void pendingNativeBootstrapNeverBecomesAnOriginalSource(){
    QFETCH(bool,mixed);QFETCH(int,part);Fixture f;if(mixed){static_cast<void>(mixedKey(f));}
    const std::array<std::string,3> paths=mixed?std::array<std::string,3>{".BiglyBTC.i2p-bootstrap",".BiglyBTC.dest_key.dat.pending",".BiglyBTC.dht.config.pending"}:
        std::array<std::string,3>{".BiglyBTC.i2p-bootstrap1",".BiglyBTC.dest_key1.dat.pending",".BiglyBTC.dht1.config.pending"};
    const auto path=f.key.parent_path()/paths[static_cast<std::size_t>(part)],key= mixed?f.key.parent_path()/"dest_key.dat":f.key;
    const auto keyBefore=read(key),configBefore=read(f.path/"biglybt.config"),inventoryBefore=read(f.path/"downloads.config");
    write(path,"preserve bootstrap prefix");QVERIFY(::chmod(path.c_str(),0600)==0);
    auto source=mixed?sam::ProfileSource::createMixedSession(*f.lease):sam::ProfileSource::createSession(*f.lease);
    QVERIFY(!source);QCOMPARE(source.error().code,storage::ErrorCode::Conflict);QVERIFY(source.error().message.find("review")!=std::string::npos);
    QCOMPARE(read(path),std::string("preserve bootstrap prefix"));QCOMPARE(read(key),keyBefore);QCOMPARE(read(f.path/"biglybt.config"),configBefore);QCOMPARE(read(f.path/"downloads.config"),inventoryBefore);
    QVERIFY(fs::remove(path));source=mixed?sam::ProfileSource::createMixedSession(*f.lease):sam::ProfileSource::createSession(*f.lease);QVERIFY(source);QVERIFY((*source)->validate());
    write(path,"later incomplete source");QVERIFY(!(*source)->validate());QCOMPARE(read(path),std::string("later incomplete source"));QCOMPARE(read(key),keyBefore);
  }
  void bootstrapRefusalIsIndexScopedAndUnknownFilesRemainInert(){
    Fixture f;static_cast<void>(mixedKey(f));const auto marker=f.key.parent_path()/".BiglyBTC.i2p-bootstrap1";write(marker,"pure prefix");
    auto mixed=sam::ProfileSource::createMixedSession(*f.lease);QVERIFY(mixed);QVERIFY((*mixed)->validate());QVERIFY(!sam::ProfileSource::createSession(*f.lease));
    QVERIFY(fs::remove(marker));const auto unknown=f.key.parent_path()/".BiglyBTC.i2p-bootstrap1.future";write(unknown,"unknown retained bytes");
    auto pure=sam::ProfileSource::createSession(*f.lease);QVERIFY(pure);QVERIFY((*pure)->validate());QVERIFY((*mixed)->validate());QCOMPARE(read(unknown),std::string("unknown retained bytes"));
  }
