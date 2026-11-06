// Included inside biglybtc::ui; one async request slot for manager and menus.
void LibraryWindow::openManualTags(){
  if(closing_||offlinePreview_||!columnSession_){return;}
  QPointer<LibraryWindow> guard(this);
  if(!manualTagDialog_){
    auto* dialog=new ManualTagManagerDialog(columnSession_,this);manualTagDialog_=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::NonModal);
    const QPointer<ManualTagManagerDialog> expected(dialog);
    connect(dialog,&ManualTagManagerDialog::commandRequested,this,[this,expected](ManualTagCommand value){
      if(!expected||expected!=manualTagDialog_){return;}if(!sendManualTagCommand(std::move(value))&&expected){expected->showFailure(tr("The command was not sent. Reload after the current operation finishes."));}
    });
  }
  const auto dialog=manualTagDialog_;dialog->setAvailable(!busy_&&!closing_&&!offlinePreview_);if(!guard||!dialog){return;}
  if(latestManualTagEditor_){dialog->setPresentation(latestManualTagEditor_);if(!guard||!dialog){return;}}
  if(pendingManualTagCommand_){dialog->setPending(*pendingManualTagCommand_);if(!guard||!dialog){return;}}
  dialog->show();if(!guard||!dialog){return;}dialog->raise();if(!guard||!dialog){return;}dialog->activateWindow();if(!guard){return;}
  if(!pendingManualTagCommand_){ManualTagCommand reload;reload.kind=ManualTagCommandKind::Reload;sendManualTagCommand(std::move(reload));}
}
bool LibraryWindow::sendManualTagCommand(ManualTagCommand value){
  if(closing_||offlinePreview_||busy_||!columnSession_||pendingManualTagCommand_||manualTagRequest_==std::numeric_limits<quint64>::max()){return false;}
  if(value.session&&value.session!=columnSession_){return false;}
  if(value.kind!=ManualTagCommandKind::Reload){
    if(!latestManualTagEditor_||!latestManualTagEditor_->writable||!latestManualTagEditor_->generation){return false;}
    if(value.generation&&value.generation!=latestManualTagEditor_->generation){return false;}
    value.generation=latestManualTagEditor_->generation;
    if(value.kind==ManualTagCommandKind::AddMember||value.kind==ManualTagCommandKind::RemoveMember){
      if(latestManualTagEditor_->libraryRevision!=model_->revision()||!model_->accepts(value.member)||selected()!=std::optional<Target>{value.member}){return false;}
    }
  }else{value.generation=0;}
  value.session=columnSession_;value.request=++manualTagRequest_;pendingManualTagCommand_=value;
  const QPointer<LibraryWindow> guard(this);if(manualTagDialog_){manualTagDialog_->setPending(value);if(!guard){return true;}}
  emit manualTagCommandRequested(std::move(value));return true;
}
void LibraryWindow::setManualTagCommandFinished(ManualTagCommandPresentation packet){
  if(!packet||closing_||!pendingManualTagCommand_||packet->session!=columnSession_||packet->session!=pendingManualTagCommand_->session||packet->request!=pendingManualTagCommand_->request||packet->kind!=pendingManualTagCommand_->kind||packet->failure.size()>8192||packet->constraintStatus.size()>8192){return;}
  const auto command=*pendingManualTagCommand_;
  const bool success=packet->status==ManualTagCommandStatus::Loaded||packet->status==ManualTagCommandStatus::Saved||packet->status==ManualTagCommandStatus::Unchanged;
  if(success){
    if(!packet->failure.isEmpty()||!manualTagEditorValues(packet)||!packet->libraryRevision){return;}
    if((packet->status==ManualTagCommandStatus::Loaded)!=(packet->kind==ManualTagCommandKind::Reload)){return;}
    if(packet->status==ManualTagCommandStatus::Saved&&(!packet->changed||packet->generation<=command.generation)){return;}
    if(packet->status==ManualTagCommandStatus::Unchanged&&(packet->changed||packet->generation<command.generation)){return;}
    if(latestManualTagEditor_&&packet->generation<latestManualTagEditor_->generation){return;}
    if(latestManualTagEditor_&&packet->generation==latestManualTagEditor_->generation
        &&packet->tags->source->originalBytes()!=latestManualTagEditor_->tags->source->originalBytes()){return;}
    latestManualTagEditor_=packet;
  }else if(packet->status==ManualTagCommandStatus::Rejected||packet->status==ManualTagCommandStatus::Cancelled){
    if(packet->failure.isEmpty()){return;}latestManualTagEditor_.reset();
  }else{return;}
  pendingManualTagCommand_.reset();const QPointer<LibraryWindow> guard(this);
  if(manualTagDialog_){manualTagDialog_->setResult(packet);if(!guard){return;}}
  setMessage(packet->status==ManualTagCommandStatus::Saved?tr("Tags saved"):packet->status==ManualTagCommandStatus::Unchanged?tr("Tags unchanged"):packet->status==ManualTagCommandStatus::Loaded?tr("Tags loaded"):packet->status==ManualTagCommandStatus::Cancelled?tr("Tag command cancelled"):tr("Tag command rejected: %1").arg(packet->failure.left(512)));if(!guard){return;}
  if(!packet->constraintStatus.isEmpty()&&success){setMessage(packet->constraintStatus.left(512));if(!guard){return;}}
  // Host publishes the shared Stats join before this command result.
}
void LibraryWindow::rejectManualTagCommand(const ManualTagCommand& command){
  auto packet=std::make_shared<ManualTagCommandPacket>();packet->session=command.session;packet->request=command.request;packet->kind=command.kind;packet->status=ManualTagCommandStatus::Rejected;packet->failure=tr("The profile worker did not accept the tag command; reload before retrying.");setManualTagCommandFinished(std::move(packet));
}
void LibraryWindow::rebuildManualTagMenu(QMenu* menu){
  menu->clear();const auto target=actionTarget();const auto revision=model_->revision();const auto session=columnSession_;
  auto* manage=menu->addAction(tr("Manage tags…"));connect(manage,&QAction::triggered,this,&LibraryWindow::openManualTags);menu->addSeparator();
  auto values=manualTagEditorValues(latestManualTagEditor_);
  if(!target||!model_->accepts(*target)||!values||!latestManualTagEditor_||latestManualTagEditor_->session!=session){auto* unavailable=menu->addAction(target?tr("Open tag manager to load tags"):tr("Select one torrent"));unavailable->setEnabled(false);return;}
  const auto generation=latestManualTagEditor_->generation;const auto captured=*target;
  if(values->rows.empty()){auto* empty=menu->addAction(tr("No manual tags"));empty->setEnabled(false);}
  for(std::size_t i=0;i<values->rows.size();++i){
    const auto& row=values->rows[i];auto* action=menu->addAction(row.label);action->setObjectName(QStringLiteral("manual-tag-member-%1").arg(row.id.value_or(-1)));action->setCheckable(true);
    const auto& source=values->tags->tags[i];const bool present=std::any_of(source.uniqueHashes.begin(),source.uniqueHashes.end(),[&](const auto& hash){return QString::fromStdString(hash.hex()).compare(captured.hash,Qt::CaseInsensitive)==0;});action->setChecked(present);
    action->setEnabled((present?row.canRemoveMember:row.canAddMember)&&latestManualTagEditor_->libraryRevision==revision&&!pendingManualTagCommand_&&!busy_&&!closing_&&!offlinePreview_);action->setToolTip(row.detail);
    const auto id=row.id;const QPointer<QAction> item(action);
    connect(action,&QAction::triggered,this,[this,item,id,captured,generation,revision,session,present](bool checked){
      if(item){const QSignalBlocker restore(item);item->setChecked(present);} // Display worker state until actual result.
      if(!id||session!=columnSession_||revision!=model_->revision()||!model_->accepts(captured)||actionTarget()!=std::optional<Target>{captured}||!latestManualTagEditor_||latestManualTagEditor_->generation!=generation){return;}
      ManualTagCommand command;command.kind=checked?ManualTagCommandKind::AddMember:ManualTagCommandKind::RemoveMember;command.tagId=*id;command.member=captured;command.session=session;command.generation=generation;
      sendManualTagCommand(std::move(command));
    });
  }
}
