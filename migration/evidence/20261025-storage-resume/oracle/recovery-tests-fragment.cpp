// Insert these two families inside the existing managed_storage_tests.cpp main
// try block. Reuses Fixture/b/i/read/write/checkpoint; no new target or harness.
// Snapshot phases model durable boundaries, not power-loss or fault injection.
  test("compact-conversion-replays-original-edges-at-each-durable-boundary",[] {
    // File0 has 20000 bytes at torrent offset0. Original compact storage keeps
    // just its final3616-byte fragment, which shares real piece1 with file1.
    // Four snapshots: intent before expansion; expanded while C; L before
    // control retirement; L after retirement but before clearing the intent.
    for (const int phase : {0,1,2,3}) {
      Fixture f(true,false,{},phase<2?"l1:C1:Le":"l1:L1:Le");
      const std::string edge(3616,'A');
      std::string expanded(20000,'\0'); expanded.replace(0,edge.size(),edge);
      expanded.replace(16384,edge.size(),edge);
      write(f.path(0),phase==0?edge:expanded);
      write(f.path(1),std::string_view(f.body).substr(20000));
      struct stat identity{}; require(::lstat(f.path(0).c_str(),&identity)==0,"actual compact replay inode");
      const auto directory=f.profile/"active"/f.hash.hex();
      const auto marker=directory/"fmfile0.dat";
      const auto control="d"+b("version")+"i0e"+b("length")+i(20000)+b("opaque")+"li7e3:keye"+"e";
      if (phase!=3) {
        write(marker,control);
        // Retirement must clear both original resilient candidates. A crash
        // after unlinking primary is equivalent to a valid backup-only state.
        write(directory/"fmfile0.dat.bak",control);
      }
      auto state=f.state(); auto attributes=core::Document::parse(encode(*state.find("attributes")));
      require(attributes.has_value(),"compact replay attributes");
      const auto intent="ld"+b("index")+"i0e"+b("device")+i(static_cast<std::int64_t>(identity.st_dev))
          +b("inode")+i(static_cast<std::int64_t>(identity.st_ino))+"ee";
      require(attributes->replaceValue("BiglyBTC.compactConversion",intent).has_value(),"persist exact replay identity");
      require(state.replaceValue("attributes",attributes->serialize()).has_value(),"persist replay state");
      write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
      {
        auto owner=f.prepare(); require(owner.mappedFiles()!=nullptr,"replay uses authenticated mapped descriptors");
        const auto actual=read(f.path(0));
        require(actual==expanded,"replay preserves packed edge and copies its exact original tail once");
        require(read(f.path(1))==std::string_view(f.body).substr(20000),"replay leaves adjacent selected file unchanged");
        struct stat after{}; require(::lstat(f.path(0).c_str(),&after)==0&&after.st_dev==identity.st_dev
            &&after.st_ino==identity.st_ino,"conversion retains exact admitted payload inode");
        const auto saved=f.state(); const auto* attrs=saved.find("attributes");
        require(attrs->find("storetypes")->children[0].bytes=="L"
            &&attrs->find("BiglyBTC.compactConversion")->children.empty(),"store L is durable before intent clears");
        require(!fs::exists(marker)&&!fs::exists(directory/"fmfile0.dat.bak"),"no compact marker can redirect subsequent Java linear reads");
        require(encode(*attrs->find("unknown"))=="li-7e3:keye","unknown original attribute remains raw-compatible");
        const auto* resume=saved.find("resume")->find("data");
        require(resume->find("valid")->integer==0&&resume->find("resume data")->bytes==std::string(2,'\2'),
            "new sparse zero interior remains unverified RECHECK, never complete");
        std::string tail(edge.size(),'x');
        require(owner.mappedFiles()->read(16384,std::span<char>(tail)).has_value()&&tail==edge,
            "replayed actual original tail remains logically readable");
        // Only actual piece1 can exist initially. Do not claim a synthetic
        // verified flag here: real Java/native piece checking is in run.py.
      }
      auto reopened=f.prepare();
      require(reopened.parameters().ti!=nullptr,"replayed owner retains actual original metadata");
      require(read(f.path(0))==expanded&&read(f.path(1))==std::string_view(f.body).substr(20000),
          "second prepare after cleared intent is idempotent and retains adjacent bytes");
      const auto resumed=f.state();
      require(resumed.find("resume")->find("data")->find("valid")->integer==0,"reopen still requires actual hash checks");
    }
  });
  test("compact-conversion-refuses-replaced-journal-inode-before-any-mutation",[] {
    Fixture f(true,false,{},"l1:C1:Le");
    write(f.path(0),std::string(3616,'A')); write(f.path(1),std::string_view(f.body).substr(20000));
    struct stat original{}; require(::lstat(f.path(0).c_str(),&original)==0,"old journal inode exists");
    const auto marker=f.profile/"active"/f.hash.hex()/"fmfile0.dat";
    write(marker,"d"+b("version")+"i0e"+b("length")+i(20000)+"e");
    auto state=f.state(); auto attrs=core::Document::parse(encode(*state.find("attributes")));
    require(attrs.has_value(),"replaced journal attributes");
    const auto intent="ld"+b("index")+"i0e"+b("device")+i(static_cast<std::int64_t>(original.st_dev))
        +b("inode")+i(static_cast<std::int64_t>(original.st_ino))+"ee";
    require(attrs->replaceValue("BiglyBTC.compactConversion",intent).has_value(),"original exact inode intent");
    require(state.replaceValue("attributes",attrs->serialize()).has_value(),"original intent state");
    write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
    // Keep the old inode alive under an owned alternate name so replacement
    // cannot accidentally reuse it, then install a distinct current leaf.
    const auto retired=f.root/"old-compact-inode"; fs::rename(f.path(0),retired);
    write(f.path(0),std::string(3616,'Z'));
    struct stat replacement{}; require(::lstat(f.path(0).c_str(),&replacement)==0
        &&replacement.st_ino!=original.st_ino,"actual replacement is a different inode");
    const auto stateBefore=read(f.profile/"active"/(f.hash.hex()+".dat")), inventoryBefore=read(f.profile/"downloads.config");
    const auto markerBefore=read(marker), payloadBefore=read(f.path(0)), siblingBefore=read(f.path(1));
    auto rejected=Storage::prepare(*f.lease,f.hash,{});
    require(!rejected&&rejected.error().code==Code::Conflict,"stale conversion inode is refused");
    require(read(f.profile/"active"/(f.hash.hex()+".dat"))==stateBefore&&read(f.profile/"downloads.config")==inventoryBefore
        &&read(marker)==markerBefore&&read(f.path(0))==payloadBefore&&read(f.path(1))==siblingBefore,
        "rejected replay does not alter state/control/current or adjacent payload");
    require(read(retired)==std::string(3616,'A'),"original unmatched inode also stays untouched");
  });
  test("unchanged-reordered-map-still-authenticates-control-at-checkpoint",[] {
    // This short reordered fixture uses source-linear data but still has its
    // original .dat.2 format marker. No new allocation or map edit is needed.
    Fixture f(false,false,{},"l1:Re"); f.fullPayload(); auto owner=f.prepare();
    require(owner.mappedFiles()!=nullptr&&owner.mappedFiles()->flush().has_value(),"establish acknowledged original control");
    const auto marker=f.profile/"active"/f.hash.hex()/"fmfile0.dat.2";
    const auto bytes=read(marker); struct stat original{};
    require(::lstat(marker.c_str(),&original)==0,"acknowledged actual control inode");
    const auto replacement=fs::path(marker.string()+".replacement"); write(replacement,bytes);
    struct stat next{}; require(::lstat(replacement.c_str(),&next)==0&&next.st_ino!=original.st_ino,
        "same-byte control replacement has a different real inode");
    fs::rename(replacement,marker);
    const auto stateBefore=read(f.profile/"active"/(f.hash.hex()+".dat")), inventoryBefore=read(f.profile/"downloads.config");
    auto rejected=owner.checkpoint(*f.lease,std::array<std::uint8_t,2>{0,0},false,0,0);
    require(!rejected&&rejected.error().code==Code::Conflict,"unchanged cached map cannot bypass control identity validation");
    require(read(marker)==bytes&&read(f.path())==f.body,"refusal does not replace control or payload");
    require(read(f.profile/"active"/(f.hash.hex()+".dat"))==stateBefore&&read(f.profile/"downloads.config")==inventoryBefore,
        "uncertain original control prevents progress/checkpoint publication");
  });
  test("external-file-completion-is-per-file-and-applies-original-dnd-prefix-rule",[] {
    // Normal completion, replay of an actual two-name v4 intent, and DND
    // completion retaining its collision-prevention prefix. Piece0 stays
    // unverified; real fixture piece1 wholly completes only the second file.
    for (const int scenario : {0,1,2}) {
      const bool skipped=scenario==2, interrupted=scenario==1;
      Fixture f(true,skipped,".part"); write(f.path(0),std::string_view(f.body).substr(0,20000));
      const auto source=f.root/"external"/"dnd_b.bin.part";
      const auto target=source.parent_path()/(skipped?"dnd_b.bin":"b.bin");
      write(source,std::string_view(f.body).substr(20000));
      struct stat original{}; require(::lstat(source.c_str(),&original)==0,"actual external completion inode");
      auto state=f.state(); auto attributes=core::Document::parse(encode(*state.find("attributes")));
      require(attributes.has_value(),"external completion attributes");
      require(attributes->replaceValue("dnd_pfx",b("dnd_")).has_value(),"source DND prefix");
      const auto link="1\n"+f.path(1,false).string()+"\n"+source.string();
      require(attributes->replaceValue("filelinks2","l"+b(link)+"e").has_value(),"actual external original link");
      if (interrupted) {
        require(::link(source.c_str(),target.c_str())==0,"actual external interrupted completion aliases");
        const auto journal="d"+b("version")+"i4e"+b("files")+"ld"+b("index")+"i1e"+b("source")+b(source.string())
            +b("target")+b(target.string())+b("device")+i(static_cast<std::int64_t>(original.st_dev))
            +b("inode")+i(static_cast<std::int64_t>(original.st_ino))+"eee";
        require(attributes->replaceValue("BiglyBTC.completion",journal).has_value(),"source-bound external v4 intent");
      }
      require(state.replaceValue("attributes",attributes->serialize()).has_value(),"external completion state");
      write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
      auto owner=f.prepare(); require(owner.mappedFiles()!=nullptr,"external completion keeps descriptor-backed ownership");
      checkpoint(owner,f,{0,1},false);
      require(!fs::exists(source)&&read(target)==std::string_view(f.body).substr(20000),"completed external file gets source suffix/prefix mapping");
      struct stat final{}; require(::lstat(target.c_str(),&final)==0&&final.st_dev==original.st_dev&&final.st_ino==original.st_ino,
          "per-file completion retains the exact authenticated inode");
      require(fs::exists(f.path(0))&&!fs::exists(f.path(0,false))
          &&read(f.path(0))==std::string_view(f.body).substr(0,20000),"incomplete adjacent file keeps suffix and bytes");
      require(!fs::exists(f.path(1))&&!fs::exists(f.path(1,false)),"external completion does not create a metadata-path copy");
      const auto saved=f.state(); const auto* attrs=saved.find("attributes");
      const auto* links=attrs->find("filelinks2"); bool mapped=false;
      for (const auto& item:links->children) { if(item.bytes=="1\n"+f.path(1,false).string()+"\n"+target.string()){mapped=true;} }
      require(mapped&&attrs->find("BiglyBTC.completion")->children.empty(),"final absolute original link is saved before journal clears");
      require(attrs->find("resumecomplete")->integer==1&&saved.find("resume")->find("data")->find("valid")->integer==0,
          "per-file completion cannot imply whole-torrent completion");
      const auto* counts=attrs->find("filedownloaded")->find("downloaded");
      require(counts->children[0].integer==3616&&counts->children[1].integer==12768,
          "per-file completion is driven by actual verified-piece intersections");
    }
  });
