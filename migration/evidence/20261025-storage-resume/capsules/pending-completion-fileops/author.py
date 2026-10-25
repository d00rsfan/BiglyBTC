from pathlib import Path
import shutil
out=Path(__file__).parent;native=Path('/home/yurii/Java/BiglyBTC')
for rel in ['preview/native/adapters/original_file_io.hpp','preview/native/adapters/original_file_io.cpp']:
 p=out/'before'/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(native/rel,p)
def write(rel,text):
 p=out/'staging'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
rel='preview/native/adapters/original_file_io.hpp';s=(out/'before'/rel).read_text();s=s.replace('#include <span>','#include <span>\n#include <string_view>');a='class OriginalFileIo final {\npublic:';assert s.count(a)==1;s=s.replace(a,a+'''
  // Pure control validation through the same geometry/map decoder as open().
  // No descriptors, recovery, publication or payload access.
  static std::expected<void,Error> validateControl(OriginalFileGeometry,
      OriginalStorageKind,std::string_view);
''');write(rel,s)
rel='preview/native/adapters/original_file_io.cpp';s=(out/'before'/rel).read_text();a='OriginalFileIo::OriginalFileIo(std::unique_ptr<Impl> p)';assert s.count(a)==1
method='''std::expected<void,Error> OriginalFileIo::validateControl(OriginalFileGeometry g,Kind k,std::string_view raw){return guarded([&]()->std::expected<void,Error>{
  require(k!=Kind::Linear&&raw.size()<=MaxControl,ErrorCode::InvalidFormat,"Invalid original control validation input");
  const auto form=shape(g,k);Impl candidate(-1,g,k,form,std::nullopt,{});const std::string bytes(raw);(void)candidate.decode(bytes);
  // Optimized short layouts do not consume a map in open(), but a selected
  // control still needs its source-required envelope before it can supersede
  // a backup containing native recovery state. No second map decoder.
  if(!form.compact&&!form.reordered){const auto d=parse(bytes);
    if(k==Kind::Compact){const auto n=integer(d,"length");require(integer(d,"version")==0&&n>=0&&static_cast<std::uint64_t>(n)<=g.fileBytes,ErrorCode::InvalidFormat,"Invalid short compact control");}
    else{const auto n=integer(d,"len"),next=integer(d,"next");const auto* pieces=d.find("pieces"),*st=d.find("st");require(n>=0&&static_cast<std::uint64_t>(n)<=g.fileBytes&&next>=1&&static_cast<std::uint64_t>(next)<=form.count&&pieces&&pieces->type==core::Node::Type::Bytes&&pieces->bytes.size()==form.count*4&&(!st||(st->type==core::Node::Type::Integer&&(st->integer==3||st->integer==4))),ErrorCode::InvalidFormat,"Invalid short reordered control");}
  }return {};
});}
'''
s=s.replace(a,method+a);write(rel,s)
rel='preview/native/adapters/file_operations.cpp';s=(out/'before'/rel).read_text()
a='  std::vector<Control> controls;';assert s.count(a)==1;s=s.replace(a,a+'\n  std::vector<OriginalFileGeometry> controlGeometry;')
a='void checkControls(const Plan& p,bool recovering=false){\n  for(const auto& c:p.controls){require(!inspect(fs::path(c.path.string()+".saving")),ErrorCode::Conflict,"Original control recovery is pending");const auto now=read(c.path,MaximumDocument);if(recovering&&p.preview.kind!=FileOperationKind::Relocate&&p.preview.kind!=FileOperationKind::Relink&&!now.bytes){continue;}require(now.bytes==c.before.bytes&&(!c.before.stamp||(now.stamp&&unchanged(*now.stamp,*c.before.stamp))),ErrorCode::Conflict,"Original control file changed after preparation");}\n}'
b='''enum class CompletionRecord { None, Cleared, Pending, Malformed };
CompletionRecord completionRecord(const std::optional<std::string>& raw){
  if(!raw){return CompletionRecord::None;}constexpr std::string_view key="BiglyBTC.reorderedCompletion";
  const auto d=core::Document::parse(*raw,limits(MaximumDocument));
  if(!d||d->root().type!=Node::Type::Dictionary){return raw->find(key)==std::string::npos?CompletionRecord::None:CompletionRecord::Malformed;}
  const Node* record=nullptr;for(std::size_t i=0;i<d->root().children.size();i+=2){if(d->root().children[i].bytes==key){if(record){return CompletionRecord::Malformed;}record=&d->root().children[i+1];}}
  if(!record){return CompletionRecord::None;}if(record->type!=Node::Type::Dictionary){return CompletionRecord::Malformed;}
  return record->children.empty()?CompletionRecord::Cleared:CompletionRecord::Pending;
}
void validateCompletionControls(const std::vector<Control>& controls,std::span<const OriginalFileGeometry> geometry){
  if(controls.empty()){return;}require(controls.size()==geometry.size()*4,ErrorCode::InvalidFormat,"Original control geometry inventory differs");
  for(std::size_t i=0;i<geometry.size();++i){for(std::size_t family=0;family<2;++family){const auto& primary=controls[i*4+family*2];const auto& backup=controls[i*4+family*2+1];
    const auto a=completionRecord(primary.before.bytes),b=completionRecord(backup.before.bytes);
    require(a!=CompletionRecord::Pending&&a!=CompletionRecord::Malformed,ErrorCode::Conflict,"Pending or malformed reordered completion must settle before file operation");
    if(b==CompletionRecord::Pending||b==CompletionRecord::Malformed){const auto kind=family?OriginalStorageKind::Reordered:OriginalStorageKind::Compact;
      require(primary.before.bytes&&OriginalFileIo::validateControl(geometry[i],kind,*primary.before.bytes).has_value(),ErrorCode::Conflict,"Authoritative backup reordered completion must settle before file operation");}
  }}
}
void captureControlGeometry(Plan& p,const compat::OriginalLayout& layout){
  for(const auto& file:layout.files){require(layout.pieceLength>0&&layout.pieceLength<=UINT32_MAX&&file.offset>=0&&file.bytes>=0,ErrorCode::InvalidFormat,"Invalid control geometry");p.controlGeometry.push_back({static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout.pieceLength)});}
}
void checkControls(const Plan& p,bool recovering=false){
  validateCompletionControls(p.controls,p.controlGeometry);std::vector<Control> current;current.reserve(p.controls.size());
  for(const auto& c:p.controls){require(!inspect(fs::path(c.path.string()+".saving")),ErrorCode::Conflict,"Original control recovery is pending");auto now=read(c.path,MaximumDocument);
    // An authenticated removal journal can already have removed its primary.
    // Its accepted cleared beforeimage remains the authority for stale backup
    // cleanup; move/relink never receive this exception.
    if(recovering&&p.preview.kind!=FileOperationKind::Relocate&&p.preview.kind!=FileOperationKind::Relink&&!now.bytes){current.push_back(c);continue;}
    current.push_back({c.path,std::move(now)});
  }
  validateCompletionControls(current,p.controlGeometry);
  for(std::size_t i=0;i<current.size();++i){const auto& now=current[i].before;const auto& expected=p.controls[i].before;require(now.bytes==expected.bytes&&(!expected.stamp||(now.stamp&&unchanged(*now.stamp,*expected.stamp))),ErrorCode::Conflict,"Original control file changed after preparation");}
}'''
assert s.count(a)==1;s=s.replace(a,b)
a='  auto checkedDefaults=resolveOriginalStorageTypes';assert s.count(a)==1;s=s.replace(a,'  captureControlGeometry(p,*layout);validateCompletionControls(p.controls,p.controlGeometry);\n'+a)
a='  for(const auto& f:p.files){const auto maximum=physicalMaximum(*layout,attrs.root(),f.view.index);';assert s.count(a)==1;s=s.replace(a,'  captureControlGeometry(p,*layout);validateCompletionControls(p.controls,p.controlGeometry);\n'+a)
write(rel,s)
rel='preview/native/tests/file_operations_tests.cpp';s=(out/'before'/rel).read_text()
body=r'''void pendingReorderedCompletionGuards(){
  const auto redo=[](std::string_view field){return "d"+b("BiglyBTC.reorderedCompletion")+std::string(field)+'e';};
  const std::vector<std::string> unsafe={redo("d7:versioni1ee"),redo("i0e"),redo("le"),"d"+b("BiglyBTC.reorderedCompletion")};
  for(const auto kind:{st::FileOperationKind::RemoveKeepData,st::FileOperationKind::RemoveDeleteData,st::FileOperationKind::Relocate,st::FileOperationKind::Relink}){
    for(const bool backup:{false,true}){for(const auto& raw:unsafe){Fixture f;attributes(f,"l1:Re");if(kind==st::FileOperationKind::Relink){write(f.destination/"file.bin","hello");}auto path=control(f,0,true);if(backup){path+=std::string(".bak");}write(path,raw);const auto inventory=read(f.profile/"downloads.config"),state=read(f.selected.state);auto result=st::prepareFileOperation(f.lease,{f.selected.hash,kind,kind==st::FileOperationKind::Relocate||kind==st::FileOperationKind::Relink?std::optional<fs::path>(f.destination):std::nullopt});check(!result&&result.error().code==st::ErrorCode::Conflict&&result.error().message.find("reordered completion")!=std::string::npos,"native redo in authoritative primary/backup refuses all operation kinds");check(read(f.selected.files[0])=="hello"&&read(path)==raw&&read(f.profile/"downloads.config")==inventory&&read(f.selected.state)==state&&!st::hasPendingFileOperation(f.lease),"redo preparation refuses before payload/document/journal effects");}}
  }
  const auto cleared=[](){std::string map(4,'\0');map+=std::string(4,static_cast<char>(0xff));return "d3:leni5e4:nexti1e6:pieces"+b(map)+b("BiglyBTC.reorderedCompletion")+"dee";};
  {Fixture f;attributes(f,"l1:Re");const auto c=control(f,0,true);write(c,cleared());write(c.string()+".bak",redo("d7:versioni1ee"));auto p=f.plan(st::FileOperationKind::RemoveKeepData);const auto accept=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,false));take(st::applyFileOperation(f.lease,std::move(p),accept));check(read(f.selected.files[0])=="hello"&&!fs::exists(c)&&!fs::exists(c.string()+".bak"),"valid cleared primary permits obsolete pending backup and exact cleanup");}
  {Fixture f;attributes(f,"l1:Re");const auto c=control(f,0,true);write(c,"opaque malformed primary");write(c.string()+".bak",redo("d7:versioni1ee"));auto result=st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}});check(!result&&result.error().message.find("reordered completion")!=std::string::npos&&read(f.selected.files[0])=="hello","malformed primary cannot authorize pending backup transport");}
  {Fixture f(true,true);attributes(f,"l1:L1:Re");const auto c=control(f,1,true);std::string map(8,'\0');map+=std::string(8,static_cast<char>(0xff));write(c,"d3:leni10e4:nexti1e6:pieces"+b(map)+b("BiglyBTC.reorderedCompletion")+"dee");write(c.string()+".bak",redo("d7:versioni1ee"));auto result=st::prepareFileOperation(f.lease,{f.selected.hash,st::FileOperationKind::RemoveKeepData,{}});check(!result&&result.error().message.find("reordered completion")!=std::string::npos&&read(f.selected.files[1])=="world!tail","invalid duplicate map cannot supersede pending backup even with cleared native key");}
  for(const bool recovery:{false,true}){Fixture f;attributes(f,"l1:Re");const auto c=control(f,0,true);write(c,cleared());auto p=f.plan(st::FileOperationKind::RemoveDeleteData);const auto accept=take(st::confirmFileOperation(p,p.preview().approvalScopeDigest,true));
    if(recovery){interruptAt=st::FileOperationStage::JournalSynced;interruptArmed=true;check(!st::applyFileOperation(f.lease,std::move(p),accept,interrupt)&&st::hasPendingFileOperation(f.lease),"accepted pre-effect removal journal retained");write(c,redo("d7:versioni1ee"));auto result=st::recoverFileOperation(f.lease);check(!result&&result.error().message.find("reordered completion")!=std::string::npos&&read(f.selected.files[0])=="hello"&&fs::exists(f.selected.state)&&st::hasPendingFileOperation(f.lease),"recovery independently refuses newly pending native redo before effects");}
    else{write(c,redo("d7:versioni1ee"));auto result=st::applyFileOperation(f.lease,std::move(p),accept);check(!result&&result.error().message.find("reordered completion")!=std::string::npos&&read(f.selected.files[0])=="hello"&&!st::hasPendingFileOperation(f.lease),"apply revalidates redo before creating operation journal");}
  }
}

'''
a='void realCopyBranch(){';assert s.count(a)==1;s=s.replace(a,body+a)
a='legacyRootsAndDefaultStoreRecovery();std::cout<<"nine generated file-operation families passed';assert s.count(a)==1;s=s.replace(a,'legacyRootsAndDefaultStoreRecovery();pendingReorderedCompletionGuards();std::cout<<"ten generated file-operation families passed');write(rel,s)
