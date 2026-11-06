// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the existing isolated generated-profile test's anonymous scope.
constexpr const char* CdTestJournal="BiglyBTC.category-delete.config";
std::string cdSha256(std::string_view bytes){std::string out(32,'\0');unsigned count=0;check(EVP_Digest(bytes.data(),bytes.size(),reinterpret_cast<unsigned char*>(out.data()),&count,EVP_sha256(),nullptr)==1&&count==32,"Fixture v2 full hash failed");return out;}
c::TorrentHash cdAddTarget(Area& a,const std::string& name,const std::string& category,bool inInventory=true){
  const auto info=dict({{"length","i3e"},{"name",bytes(name)},{"piece length","i16384e"},{"pieces",bytes(sha1("abc"))}});
  const auto hash=*c::TorrentHash::parse(sha1(info));
  write(a.root/"active"/(hash.hex()+".dat"),dict({{"attributes",dict({{"category",bytes(category)},{"future",bytes("preserve-member")}})},{"info",info},{"resume",dict({{"future-resume",bytes("untouched")}})}}));
  write(a.root/name,"abc");write(a.root/(name+".torrent"),dict({{"info",info}}));
  if(inInventory){auto inventory=c::Document::parse(read(a.root/"downloads.config"));check(bool(inventory),"Generated complete inventory");const auto* rows=inventory->find("downloads");check(rows,"Generated downloads list");std::string all="l";for(const auto& row:rows->children){all+=raw(*inventory,&row);}all+=dict({{"torrent_hash",bytes(std::string(reinterpret_cast<const char*>(hash.bytes().data()),hash.bytes().size()))},{"torrent",bytes((a.root/(name+".torrent")).string())},{"save_dir",bytes(a.root.string())},{"save_file",bytes(name)},{"state","i70e"},{"persistent","i1e"}});all+='e';check(inventory->replaceValue("downloads",all).has_value(),"Append generated inventory");write(a.root/"downloads.config",inventory->serialize());}
  return hash;
}
void cdDefinitions(Area& a){auto store=opened(a);auto defs=take(store->create(snap(*store),"Alpha")).snapshot;take(store->create(defs,"Beta"));take(store->close());}
bool cdHas(const s::CategorySnapshot& source,const std::string& name){for(const auto& v:source.categories()->categories){if(v.name==name){return true;}}return false;}
s::CategoryDeletionStage cdExitAt=s::CategoryDeletionStage::Completed;
void cdCrash(s::CategoryDeletionStage stage,std::size_t){if(stage==cdExitAt){::_exit(73);}}
void cdRunCrash(Area& a,s::CategoryDeletionStage stage){
  const auto parent=::getpid();const auto pid=::fork();check(pid>=0,"Fork actual journal prefix");if(pid==0){if(::prctl(PR_SET_PDEATHSIG,SIGKILL,0,0,0)!=0||::getppid()!=parent){::_exit(75);}cdExitAt=stage;auto store=opened(a);auto result=store->remove(snap(*store),"Alpha",{},std::chrono::steady_clock::now()+std::chrono::seconds{5},cdCrash);static_cast<void>(result);::_exit(74);}
  int status=0;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{10};
  for(;;){const auto joined=::waitpid(pid,&status,WNOHANG);if(joined==pid){break;}check(joined==0,"Join generated prefix child");if(std::chrono::steady_clock::now()>=end){(void)::kill(pid,SIGKILL);(void)::waitpid(pid,&status,0);throw std::runtime_error("Generated prefix child deadline");}std::this_thread::sleep_for(std::chrono::milliseconds{5});}
  check(WIFEXITED(status)&&WEXITSTATUS(status)==73,"Actual prefix was not reached");
}
void actualDeletionAndPrefixRecovery(){
  for(const bool compressed:{false,true}){Area a;a.target("Alpha");const auto original=c::Document::parse(read(a.state()));check(bool(original),"Original healthy deletion state");if(compressed){write(a.state(),gzip(read(a.state())));}cdDefinitions(a);const auto b=cdAddTarget(a,"second.bin","Alpha");const auto orphan=cdAddTarget(a,"orphan.bin","Alpha",false);const auto orphanFile=a.root/"active"/(orphan.hex()+".dat");const auto orphanBefore=read(orphanFile),payload=read(a.payload),otherPayload=read(a.root/"second.bin"),inventory=read(a.root/"downloads.config");
    const auto azp=dict({{"future-property",bytes("authoritative-cache-plugin-value")}}),azpp=dict({{"future-private",bytes(std::string("x\0y",3))}});
    const auto selected=dict({{"attributes",dict({{"category",bytes("Alpha")},{"future",bytes("cache-is-authoritative")}})},{"azp",azp},{"azpp",azpp},{"hash",bytes(std::string(reinterpret_cast<const char*>(a.hash.bytes().data()),a.hash.bytes().size()))}});
    const auto unprovided=dict({{"attributes",dict({{"category",bytes("Alpha")}})},{"hash",bytes(std::string(20,'z'))},{"future",bytes("keep-orphan-cache")}});
    write(a.root/"active/cache.dat",gzip(dict({{"future",bytes("keep-cache-root")},{"state",list({selected,unprovided})}})));
    auto store=opened(a);check(store->deletionSupported(),"Actual journal backend hint missing");auto result=take(store->remove(snap(*store),"Alpha"));check(result.changed&&!cdHas(result.snapshot,"Alpha")&&cdHas(result.snapshot,"Beta")&&!store->recoveryRequired(),"Healthy complete delete not Saved");
    for(const auto& h:{a.hash,b}){const auto target=take(store->readTarget(result.snapshot,h));check(!target.assignment()->declaredName,"Known member category was not cleared");}
    for(const auto& h:{a.hash,b}){auto primary=s::EncodedDocument::parse(read(a.root/"active"/(h.hex()+".dat"))),backup=s::EncodedDocument::parse(read(a.root/"active"/(h.hex()+".dat.bak")));
      check(primary&&backup&&primary->compressed()==(h==a.hash&&compressed)&&backup->compressed()==primary->compressed(),"Existing primary/backup compression changed");
      const auto* attrs=backup->document().find("attributes");const auto* resume=backup->document().find("resume");const auto* data=resume?resume->find("data"):nullptr;
      check(attrs&&!attrs->find("category")&&attrs->find("resumecomplete")&&attrs->find("resumecomplete")->integer==1&&data&&data->find("valid")&&data->find("valid")->integer==0&&data->find("blocks")&&data->find("blocks")->children.empty(),"Backup is not NEW cleared attributes with exact recheck fields");
      if(h==a.hash){check(data->find("resume data")&&data->find("resume data")->bytes==std::string(1,'\2'),"Backup did not recheck original piece flags");
        check(raw(primary->document(),primary->document().find("resume"))==raw(*original,original->find("resume"))&&raw(primary->document(),primary->document().find("info"))==a.info,"Primary info/resume changed during attribute deletion");
        for(const auto& pair:{std::pair{"azureus_properties",azp},std::pair{"azureus_private_properties",azpp}}){check(raw(primary->document(),primary->document().find(pair.first))==pair.second&&raw(backup->document(),backup->document().find(pair.first))==pair.second,"Full authoritative cached plugin properties were lost");}
        check(primary->document().find("attributes")->find("future")->bytes=="cache-is-authoritative","Unknown authoritative cached attribute lost");
      }
    }
    check(read(orphanFile)==orphanBefore&&read(a.payload)==payload&&read(a.root/"second.bin")==otherPayload&&read(a.root/"downloads.config")==inventory,"Deletion touched orphan/payload/inventory");
    auto cache=s::EncodedDocument::parse(read(a.root/"active/cache.dat"));check(cache&&cache->compressed(),"Cache encoding lost");const auto* retained=cache->document().find("state");check(retained&&retained->children.size()==1&&raw(cache->document(),&retained->children[0])==unprovided,"Unprovided cache row was cleared");
    check(!fs::exists(a.root/CdTestJournal)&&!fs::exists(a.root/(std::string(CdTestJournal)+".bak")),"Healthy journal cleanup missing");take(store->close());auto reopened=opened(a);check(!cdHas(snap(*reopened),"Alpha")&&!take(reopened->readTarget(snap(*reopened),b)).assignment()->declaredName,"Normal close/reopen changed committed category state");
  }
  {Area a;const auto leaf=dict({{"length","i3e"},{"pieces root",bytes(cdSha256("abc"))}});
    a.info=dict({{"file tree",dict({{"category.bin",dict({{"",leaf}})}})},{"meta version","i2e"},{"name",bytes("category.bin")},{"piece length","i16384e"}});
    a.hash=*c::TorrentHash::parse(cdSha256(a.info));a.target("Alpha");cdDefinitions(a);const auto payload=read(a.payload);auto store=opened(a);auto deleted=take(store->remove(snap(*store),"Alpha"));
    check(deleted.changed&&!cdHas(deleted.snapshot,"Alpha")&&!take(store->readTarget(deleted.snapshot,a.hash)).assignment()->declaredName&&read(a.payload)==payload,"Canonical 64-hex member was not decoded to its actual raw v2 identity");
  }
  for(const auto stage:{s::CategoryDeletionStage::JournalSynced,s::CategoryDeletionStage::MemberWritten,s::CategoryDeletionStage::MemberAcknowledged,s::CategoryDeletionStage::DefinitionWritten,s::CategoryDeletionStage::DefinitionAcknowledged}){
    Area a;a.target("Alpha");cdDefinitions(a);const auto b=cdAddTarget(a,"second.bin","Alpha");cdRunCrash(a,stage);auto store=opened(a);auto defs=snap(*store);check(store->recoveryRequired()&&!defs.writable(),"Pending crash prefix became writable");
    const bool unacknowledged=stage==s::CategoryDeletionStage::MemberWritten||stage==s::CategoryDeletionStage::DefinitionWritten;const auto before=files(a);
    auto resumed=store->resumeRemove(defs,"Alpha");
    if(unacknowledged){error(resumed,s::ErrorCode::Conflict);check(files(a)==before,"Unacknowledged write was replayed/cleaned");}
    else{check(bool(resumed)&&resumed->changed&&!cdHas(resumed->snapshot,"Alpha")&&!store->recoveryRequired(),"Exact acknowledged prefix did not recover");check(!take(store->readTarget(resumed->snapshot,b)).assignment()->declaredName,"Recovered member not cleared");}
  }
}
std::stop_source* cdStopSource=nullptr;
void cdCancelAfterMember(s::CategoryDeletionStage stage,std::size_t){if(stage==s::CategoryDeletionStage::MemberAcknowledged){cdStopSource->request_stop();}}
void categoryDeletionExternalChangesAndCancellation(){
  {Area a;const auto before=files(a);error(s::ConfigFile::createMissing(*a.lease,".BiglyBTC.category-delete","de"),s::ErrorCode::UnsafePath);check(files(a)==before,"Generic safe configuration filename guard changed");}
  for(const auto* suffix:{"",".bak",".saving"}){Area a;a.target("Alpha");cdDefinitions(a);write(a.root/(std::string(".BiglyBTC.category-delete")+suffix),"legacy preserved review");const auto before=files(a);auto store=opened(a);auto defs=snap(*store);check(store->recoveryRequired()&&!defs.writable(),"Legacy journal prefix became writable");error(store->remove(defs,"Alpha"),s::ErrorCode::Conflict);error(store->resumeRemove(defs,"Alpha"),s::ErrorCode::Conflict);check(files(a)==before&&!fs::exists(a.root/CdTestJournal),"Legacy journal was migrated/replayed/removed automatically");}
  for(unsigned kind=0;kind<4;++kind){Area a;a.target("Alpha");cdDefinitions(a);cdRunCrash(a,s::CategoryDeletionStage::JournalSynced);
    if(kind==0){auto d=c::Document::parse(read(a.root/"downloads.config"));check(d&&d->replaceValue("future-external","i9e"),"External inventory edit");write(a.root/"downloads.config",d->serialize());}
    else if(kind==1){fs::rename(a.state(),a.root/"held-old-state");write(a.state(),read(a.root/"held-old-state"));}
    else if(kind==2){write(a.root/"active/cache.dat","external cache bytes");}
    else{write(a.root/"categories.config.new","external pending definition");}
    const auto before=files(a);auto store=opened(a);auto defs=snap(*store);check(!defs.writable(),"External pending journal became writable");error(store->create(defs,"no-replay"),s::ErrorCode::Conflict);auto resumed=store->resumeRemove(defs,"Alpha");check(!resumed,"External changes were silently adopted");check(files(a)==before,"External edits were overwritten during recovery");
  }
  {Area a;a.target("Alpha");cdDefinitions(a);const auto b=cdAddTarget(a,"second.bin","Alpha");auto store=opened(a);std::stop_source stop;cdStopSource=&stop;auto result=store->remove(snap(*store),"Alpha",stop.get_token(),s::CategoryStore::Deadline::max(),cdCancelAfterMember);cdStopSource=nullptr;error(result,s::ErrorCode::Cancelled);check(store->recoveryRequired(),"Partial cancellation lost journal");take(store->close());auto reopened=opened(a);check(cdHas(snap(*reopened),"Alpha"),"Cancellation published definition early");auto done=take(reopened->resumeRemove(snap(*reopened),"Alpha"));check(done.changed&&!take(reopened->readTarget(done.snapshot,b)).assignment()->declaredName,"Explicit cancellation recovery failed");}
  {Area a;a.target("Alpha");cdDefinitions(a);auto d=c::Document::parse(read(a.root/"downloads.config"));const auto* rows=d->find("downloads");check(rows,"Duplicate inventory fixture");const auto duplicate=raw(*d,&rows->children[0]);check(d->replaceValue("downloads",list({duplicate,duplicate})).has_value(),"Duplicate inventory edit");write(a.root/"downloads.config",d->serialize());auto store=opened(a);const auto before=files(a);check(!store->remove(snap(*store),"Alpha"),"Ambiguous inventory accepted");check(files(a)==before,"Ambiguous inventory modified");}
}

void categoryRecoveryInspectionOwnsNoContinuationAuthority(){
  using State=s::CategoryDeletionRecovery::State;
  {Area a;a.target("Alpha");cdDefinitions(a);auto store=opened(a);auto defs=snap(*store);
    check(take(store->inspectRemove(defs)).state==State::None&&!s::hasPendingCategoryDeletion(*a.lease),"No-journal inspection invented recovery");
    auto newer=take(store->reload());error(store->inspectRemove(defs),s::ErrorCode::Conflict);
    std::stop_source stop;stop.request_stop();error(store->inspectRemove(newer,stop.get_token()),s::ErrorCode::Cancelled);
  }
  for(const auto stage:{s::CategoryDeletionStage::JournalSynced,s::CategoryDeletionStage::MemberWritten,s::CategoryDeletionStage::MemberAcknowledged,s::CategoryDeletionStage::DefinitionWritten,s::CategoryDeletionStage::DefinitionAcknowledged}){
    Area a;a.target("Alpha");cdDefinitions(a);const auto second=cdAddTarget(a,"second.bin","Alpha");static_cast<void>(second);cdRunCrash(a,stage);
    const auto before=files(a);auto store=opened(a);auto defs=snap(*store);auto info=take(store->inspectRemove(defs));
    const bool unack=stage==s::CategoryDeletionStage::MemberWritten||stage==s::CategoryDeletionStage::DefinitionWritten;
    check(info.name=="Alpha"&&info.totalMembers==2&&info.acknowledgedMembers<=2&&info.state==(unack?State::Review:State::Resumable),"Actual prefix diagnosis differs from exact recovery boundary");
    check(s::hasPendingCategoryDeletion(*a.lease)&&files(a)==before,"Passive inspection mutated recovery or its guarded files");
    if(!unack){
      // An owning value summary may stay alive after a foreign change; it may
      // not authorize replay using bytes or the old diagnosis.
      auto d=c::Document::parse(read(a.state()));check(d&&d->replaceValue("external-after-inspection","i1e"),"External change fixture");write(a.state(),d->serialize());
      const auto external=files(a);check(!store->resumeRemove(defs,info.name),"Old diagnosis blessed a later external edit");check(files(a)==external,"Rejected stale recovery changed files");
    }
    take(store->close());a.lease.reset();auto acquired=s::ProfileLease::acquire(a.root);check(bool(acquired)&&info.name=="Alpha","Owning recovery summary retained a lease/capability");
  }
}
