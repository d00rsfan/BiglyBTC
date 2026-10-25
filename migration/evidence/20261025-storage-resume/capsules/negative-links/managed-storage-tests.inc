  // Insert in managed_storage_tests.cpp main alongside existing test(...).
  test("negative-original-links-use-authenticated-target-and-original-base",[] {
    Fixture f;
    auto state=f.state(); auto attrs=core::Document::parse(encode(*state.find("attributes")));
    require(attrs.has_value(),"negative-link attributes");
    const auto external=f.root/"external.bin"; write(external,f.body);
    const auto original=f.path().string();
    const auto links="l"+b("0\n/stale/indexed-source\n"+(f.root/"not-used").string())
        +b(" -2147483648\t\n"+original+"\n"+external.string())
        +b("-1\n"+external.string()+"\n"+(f.root/"never-followed").string())+'e';
    require(attrs->replaceValue("filelinks2",links).has_value(),"negative-link rows");
    require(state.replaceValue("attributes",attrs->serialize()).has_value(),"negative-link state");
    write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
    auto owner=f.prepare(); require(owner.mappedFiles()!=nullptr,"migrated external link uses descriptor backend");
    std::string bytes(f.body.size(),'x');
    require(owner.mappedFiles()->read(0,std::span<char>(bytes)).has_value()&&bytes==f.body,
        "negative row selects actual authenticated external payload; linked destination is never a migration base");
    require(!fs::exists(f.path())&&!fs::exists(f.root/"never-followed"),"negative migration never scans or creates fallback paths");
  });
