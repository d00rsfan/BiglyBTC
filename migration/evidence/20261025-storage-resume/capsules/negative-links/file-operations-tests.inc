// Insert both blocks inside authenticatedExternalLinks() in file_operations_tests.cpp.
  {
    Fixture f; const auto external=f.area.root/"external/negative"; write(external,"hello");
    const auto original=f.selected.files[0].string();
    attributes(f,"l1:Le","l"+b("0\n/stale-indexed-from\n"+(f.area.root/"unused").string())
        +b("-1\n"+original+"\n"+external.string())
        +b("-1\n"+external.string()+"\n"+(f.area.root/"not-authorized").string())+'e');
    auto plan=f.plan(st::FileOperationKind::RemoveKeepData);
    check(plan.preview().files.size()==1&&plan.preview().files[0].source==external,
        "file-operation preview shares exact original-base negative migration, not chained links");
    auto confirmation=take(st::confirmFileOperation(plan,plan.preview().approvalScopeDigest,false));
    take(st::applyFileOperation(f.lease,std::move(plan),confirmation));
    check(read(external)=="hello"&&read(f.selected.files[0])=="hello"&&!fs::exists(f.selected.state)
        &&!fs::exists(f.area.root/"not-authorized"),
        "RemoveKeep uses authenticated migrated source and preserves original and external payload bytes");
  }
  {
    Fixture f; const auto forbidden=f.profile/"not-payload"; write(forbidden,"hello");
    attributes(f,"l1:Le","l"+b("-1\n"+f.selected.files[0].string()+"\n"+forbidden.string())+'e');
    const auto state=read(f.selected.state),inventory=read(f.profile/"downloads.config");
    check(!st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}})
        &&read(forbidden)=="hello"&&read(f.selected.state)==state&&read(f.profile/"downloads.config")==inventory
        &&!st::hasPendingFileOperation(f.lease),"negative index never bypasses held-profile exclusion or mutates on failed authority");
  }
