from pathlib import Path
import shutil
out=Path(__file__).parent; native=Path('/home/yurii/Java/BiglyBTC')
rel=Path('preview/native/tests/managed_storage_tests.cpp');before=(native/rel).read_text();p=out/'before'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(before)
new=r'''  test("legacy-path-roots-are-captured-before-payload-creation",[] {
    for(const bool legacyKey:{true,false}) {
      Fixture f;f.fullPayload();auto inventory=f.saved();const auto& original=row(inventory);std::string migrated="d";
      for(std::size_t i=0;i<original.children.size();i+=2){const auto& key=original.children[i];if(key.bytes=="save_dir"||key.bytes=="save_file"){continue;}migrated+=encode(key)+encode(original.children[i+1]);}
      migrated+=b(legacyKey?"path":"save_dir")+b(f.path().string())+'e';require(inventory.replaceValue("downloads","l"+migrated+'e').has_value(),"legacy file path row");write(f.profile/"downloads.config",inventory.serialize());
      auto owner=f.prepare();require(owner.parameters().save_path==f.save.string()&&owner.parameters().renamed_files.at(libtorrent::file_index_t{0})=="saved.bin","legacy file path has original interpretation");
      auto current=f.saved();require(row(current).find("save_dir")->bytes==f.save.string()&&row(current).find("save_file")->bytes=="saved.bin"&&row(current).find("private_row_value")&&current.find("opaque_root"),"canonical fields saved before payload changes, unknowns retained");
      checkpoint(owner,f,{1,1},true);require(row(f.saved()).find("downloaded")->integer==9,"legacy migration checkpoint has captured cumulative baseline once");
    }
    Fixture f(true);const auto path=f.save/"bundle";auto inventory=f.saved();const auto& original=row(inventory);std::string migrated="d";
    for(std::size_t i=0;i<original.children.size();i+=2){const auto& key=original.children[i];if(key.bytes=="save_dir"||key.bytes=="save_file"){continue;}migrated+=encode(key)+encode(original.children[i+1]);}
    migrated+=b("path")+b(path.string())+'e';require(inventory.replaceValue("downloads","l"+migrated+'e').has_value(),"legacy nonexistent multifile root");write(f.profile/"downloads.config",inventory.serialize());
    auto owner=f.prepare();require(owner.parameters().save_path==path.string()&&fs::exists(path/"bundle/a.bin"),"missing directory whose name matches metadata is captured as parent");auto current=f.saved();require(row(current).find("save_dir")->bytes==path.string()&&row(current).find("save_file")->bytes=="bundle","new parent existence cannot reinterpret legacy root on restart");
    checkpoint(owner,f,{0,0},false);
  });
  test("missing-store-types-use-held-original-reorder-defaults",[] {
    for(const bool reorder:{false,true}) {
      Fixture f;auto state=f.state();auto attrs=core::Document::parse("de");require(attrs.has_value(),"default attributes");require(attrs->replaceValue("opaque.default","li-3e3:rawe").has_value(),"default unknown attribute");require(state.replaceValue("attributes",attrs->serialize()).has_value(),"missing store types");write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
      write(f.profile/"biglybt.config",std::string("d")+b("Enable reorder storage mode")+(reorder?"i1e":"i0e")+b("Reorder storage mode min MB")+"i0ee");
      auto owner=f.prepare();const auto current=f.state();require(current.find("attributes")->find("storetypes")->children[0].bytes==(reorder?"R":"L"),"source default policy materialized");require(current.find("attributes")->find("opaque.default"),"default synthesis retains opaque state");
      require(bool(owner.mappedFiles())==reorder,"R default uses actual original mapped owner");if(reorder){require(current.find("attributes")->find("reordermb")->integer==0,"source global threshold captured");}
      checkpoint(owner,f,{0,0},false);
    }
  });
  test("missing-types-never-treat-compact-control-as-linear",[] {
    Fixture f(true,true);auto inventory=f.saved();auto first=core::Document::parse(encode(row(inventory)));require(first&&first->replaceValue("file_priorities","li0ei1ee").has_value(),"first compact file explicitly skipped");require(inventory.replaceValue("downloads","l"+first->serialize()+'e').has_value(),"skipped compact row");write(f.profile/"downloads.config",inventory.serialize());write(f.path(0),std::string(3616,'A'));write(f.path(1),std::string_view(f.body).substr(20000));auto state=f.state();auto attrs=core::Document::parse("de");require(attrs.has_value(),"compact default attrs");require(state.replaceValue("attributes",attrs->serialize()).has_value(),"compact missing stores");write(f.profile/"active"/(f.hash.hex()+".dat"),state.serialize());
    const auto control=f.profile/"active"/f.hash.hex()/"fmfile0.dat";write(control,"d7:versioni0e6:lengthi20000ee");
    auto owner=f.prepare();require(owner.mappedFiles()!=nullptr&&fs::file_size(f.path(0))==20000&&!fs::exists(control),"default L expands actual C even for skipped file");
    const auto bytes=read(f.path(0));require(bytes.substr(16384)==std::string(3616,'A'),"actual compact edge is relocated to original linear tail");require(f.state().find("attributes")->find("storetypes")->children[0].bytes=="L","postconversion type is durable L");checkpoint(owner,f,{0,0},false);
  });
'''
anchor='  std::cout<<"PASS "<<families';assert before.count(anchor)==1;s=before.replace(anchor,new+anchor);p=out/'staging'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s)
# Existing eight file-operation families remain byte-exact; add one boundary family.
rel=Path('preview/native/tests/file_operations_tests.cpp');before=(native/rel).read_text();p=out/'before'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(before)
new=r'''void legacyRootsAndDefaultStoreRecovery(){
  for(const bool dictionary:{false,true}) {
    Fixture f;auto row=doc(f.selected.row);std::string legacy="d";
    for(std::size_t i=0;i<row.root().children.size();i+=2){const auto& key=row.root().children[i];if(key.bytes=="save_dir"||key.bytes=="save_file"){continue;}legacy+=*core::encodeValue(key)+*core::encodeValue(row.root().children[i+1]);}
    legacy+=b("path")+b(f.selected.files[0].string())+'e';
    write(f.profile/"downloads.config",dictionary?"d"+b("legacy.download")+legacy+b("opaque")+b("raw")+'e':"d9:downloadsl"+legacy+"e6:opaque3:rawe");
    auto state=doc(read(f.selected.state));auto attrs=doc("d12:opaque.stateli7e3:rawee");put(state,"attributes",attrs.serialize());write(f.selected.state,state.serialize());
    const auto inventoryBefore=read(f.profile/"downloads.config"),stateBefore=read(f.selected.state);auto p=f.plan(st::FileOperationKind::Relocate);check(read(f.profile/"downloads.config")==inventoryBefore&&read(f.selected.state)==stateBefore,"legacy/default prepare stays read-only");check(p.preview().sourceRoot==f.selected.files[0]&&p.preview().files[0].source==f.selected.files[0],"legacy exact root in preview");auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));interruptAt=st::FileOperationStage::DestinationPublished;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),c,interrupt)&&st::hasPendingFileOperation(f.lease),"legacy move durable before source removal");
    write(f.profile/"biglybt.config","d27:Enable reorder storage modei1e27:Reorder storage mode min MBi0ee");
    const auto result=take(st::recoverFileOperation(f.lease));check(result&&result->filesMoved==1&&read(f.destination/"file.bin")=="hello"&&!fs::exists(f.selected.files[0]),"recovery uses captured physical policy rather than changed config");auto inventory=doc(read(f.profile/"downloads.config"));auto library=core::readLibrary(inventory);check(library&&library->downloads.size()==1&&library->legacyMap==dictionary&&library->downloads[0].saveDirectory==f.destination.string()&&library->downloads[0].saveFile=="file.bin"&&inventory.find("opaque")->bytes=="raw","target migration retains inventory form and opaque data");
    const auto current=doc(read(f.selected.state));check(current.find("attributes")->find("storetypes")->children[0].bytes=="L"&&current.find("attributes")->find("opaque.state"),"move writes captured desired defaults and preserves unknown attributes");
  }
  {Fixture f;auto row=doc(f.selected.row);std::string legacy="d";for(std::size_t i=0;i<row.root().children.size();i+=2){const auto& key=row.root().children[i];if(key.bytes=="save_dir"||key.bytes=="save_file"){continue;}legacy+=*core::encodeValue(key)+*core::encodeValue(row.root().children[i+1]);}legacy+=b("save_dir")+b(f.source.string())+'e';write(f.profile/"downloads.config","d"+b("old")+legacy+b("opaque")+b("raw")+'e');auto p=f.plan(st::FileOperationKind::RemoveKeepData);const auto c=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(p),c));auto after=doc(read(f.profile/"downloads.config"));check(!after.find("old")&&after.find("opaque")->bytes=="raw"&&read(f.selected.files[0])=="hello","legacy dictionary keep removes only selected row and retains data");}
}

'''
anchor='void realCopyBranch(){';assert before.count(anchor)==1;s=before.replace(anchor,new+anchor);anchor='authenticatedExternalLinks();std::cout<<"eight generated file-operation families passed';assert s.count(anchor)==1;s=s.replace(anchor,'authenticatedExternalLinks();legacyRootsAndDefaultStoreRecovery();std::cout<<"nine generated file-operation families passed');p=out/'staging'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s)
