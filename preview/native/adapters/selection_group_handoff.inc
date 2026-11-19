namespace detail {
std::expected<void,Error> handoffSelectionFileOperation(const ProfileLease& lease,
    const FileOperationRequest& request,const fileop::Raw& group,
    void(*observer)(OriginalSelectionGroupStage)){
  return guarded([&]()->std::expected<void,Error>{
    constexpr std::string_view magic="BIGLYBTC-UNSKIP-GROUP-1\n";
    const auto path=fs::path(profilePath(lease))/".BiglyBTC.linear-to-compact";
    check(path,group,MaximumDocument);require(group.bytes&&group.bytes->starts_with(magic)&&group.stamp,ErrorCode::Conflict,"Missing held selection group handoff");
    auto outer=parse(group.bytes->substr(magic.size()));auto payload=text(outer.find("payload"));require(digest(payload)==text(outer.find("sha256")),ErrorCode::InvalidFormat,"Invalid selection handoff checksum");auto record=parse(std::move(payload));
    require(integer(record.find("version"))==1&&text(record.find("profile"))==profilePath(lease)&&text(record.find("hash"))==request.hash.hex()&&request.kind==FileOperationKind::SetFilePolicies&&!request.selectionIntentEncoding&&request.expectedSelectionEntry==text(record.find("row"))&&request.selectionPolicyEncoding==text(record.find("policy"))&&request.postSkipIntentEncoding==text(record.find("mask")),ErrorCode::InvalidFormat,"Selection handoff differs from its converted group");
    Plan plan;
    if(hasPendingFileOperation(lease)){
      require(!inspect(fs::path(profilePath(lease))/(std::string(JournalName)+".BiglyBTC.fileop-saving")),ErrorCode::Conflict,"Selection handoff journal publication is uncertain");
      const auto raw=read(fs::path(profilePath(lease))/JournalName,MaximumJournal);require(raw.bytes.has_value(),ErrorCode::Conflict,"Incomplete selection handoff journal requires review");plan=decode(raw);profileIdentity(lease,plan);validateLegacyNaming(lease,plan);validateCurrentIndependence(lease,plan);
      const auto scope=parse('d'+plan.scopeSelection+'e',MaximumJournal);
      require(plan.postSkip&&plan.preview.kind==FileOperationKind::SetFilePolicies&&plan.preview.hash==request.hash&&text(scope.find("BiglyBTC.selection.policy"))==*request.selectionPolicyEncoding&&scope.find("BiglyBTC.postSkip.intent")&&encoded(*scope.find("BiglyBTC.postSkip.intent"))==*request.postSkipIntentEncoding&&!scope.find(SelectionIntentKey)&&!scope.find(SelectionChangedIntentKey),ErrorCode::Conflict,"Another operation occupies the selection handoff");
      const auto inv=std::ranges::find(plan.edits,fs::path(profilePath(lease))/"downloads.config",&Edit::path);require(inv!=plan.edits.end()&&inv->before.bytes,ErrorCode::InvalidFormat,"Handoff lacks inventory beforeimage");auto document=parse(*inv->before.bytes);require(inventorySelection(document,request.hash)=='l'+*request.expectedSelectionEntry+'e',ErrorCode::Conflict,"Handoff selected row differs from converted group");
    }else{
      plan=prepare(lease,request,{});profileIdentity(lease,plan);targetIdentity(plan);checkControls(plan);for(const auto& edit:plan.edits){check(edit.path,edit.before,MaximumDocument);}
      for(auto& file:plan.files){const auto current=inspect(file.view.source);require(bool(current)==bool(file.source)&&(!current||unchanged(*current,*file.source)),ErrorCode::Conflict,"Selection source changed before handoff");if(file.source&&(effectMoves(file)||file.selectionRemove)){file.sha256=plan.payloadDigests.digest(file.view.source,*file.source);}if(file.selectionAllocate||file.selectionRemove){checkSelectionAnchor(file);}}
      saveJournal(plan);
    }
    const auto state=std::ranges::find(plan.edits,fs::path(profilePath(lease))/"active"/(request.hash.hex()+".dat"),&Edit::path);require(state!=plan.edits.end(),ErrorCode::InvalidFormat,"Handoff lacks original full info");auto before=envelope(state->before);require(digest(expectedValue(originalTorrentInfoSubject(before.document())))==text(record.find("info")),ErrorCode::Conflict,"Selection handoff full info changed");
    if(observer){observer(OriginalSelectionGroupStage::HandoffJournalSynced);}
    check(path,group,MaximumDocument);erase(path,*group.stamp);
    if(observer){observer(OriginalSelectionGroupStage::HandoffReleased);}
    (void)execute(lease,plan,true,nullptr);return {};
  });
}
}
