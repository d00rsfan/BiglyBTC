// Included in biglybtc::ui. One async slot shared by manager and torrent menu.
void LibraryWindow::openCategories(){
  if(closing_||offlinePreview_||!columnSession_){return;}
  const QPointer<LibraryWindow> guard(this);
  if(!categoryDialog_){
    auto* dialog=new CategoryManagerDialog(columnSession_,this);categoryDialog_=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::NonModal);
    const QPointer<CategoryManagerDialog> expected(dialog);
    connect(dialog,&CategoryManagerDialog::commandRequested,this,[this,expected](CategoryCommand value){
      if(!expected||expected!=categoryDialog_){return;}if(!sendCategoryCommand(std::move(value))&&expected){expected->showFailure(tr("The command was not sent. Reload after the current operation finishes."));}
    });
  }
  const auto dialog=categoryDialog_;dialog->setAvailable(!busy_&&!closing_&&!offlinePreview_);if(!guard||!dialog){return;}
  if(latestCategoryEditor_){dialog->setPresentation(latestCategoryEditor_);if(!guard||!dialog){return;}}
  if(pendingCategoryCommand_){dialog->setPending(*pendingCategoryCommand_);if(!guard||!dialog){return;}}
  dialog->show();if(!guard||!dialog){return;}dialog->raise();if(!guard||!dialog){return;}dialog->activateWindow();if(!guard){return;}
  if(!pendingCategoryCommand_){CategoryCommand request;request.kind=CategoryCommandKind::Reload;sendCategoryCommand(std::move(request));}
}
bool LibraryWindow::sendCategoryCommand(CategoryCommand request){
  if(closing_||offlinePreview_||busy_||!columnSession_||pendingCategoryCommand_||categoryRequest_==std::numeric_limits<quint64>::max()){return false;}
  if(request.session&&request.session!=columnSession_){return false;}
  if(request.kind!=CategoryCommandKind::Reload){
    if(!latestCategoryEditor_||!latestCategoryEditor_->writable||latestCategoryEditor_->recoveryRequired||!latestCategoryEditor_->generation){return false;}
    if(request.generation&&request.generation!=latestCategoryEditor_->generation){return false;}
    request.generation=latestCategoryEditor_->generation;
    if(request.kind==CategoryCommandKind::Assign){
      if(latestCategoryEditor_->libraryRevision!=model_->revision()||!latestCategoryEditor_->target
          ||*latestCategoryEditor_->target!=request.target||!model_->accepts(request.target)||actionTarget()!=std::optional{request.target}){return false;}
    }
    if(request.kind==CategoryCommandKind::Remove&&!latestCategoryEditor_->deletionSupported){return false;}
  }else{request.generation=0;}
  // Non-membership commands may also refresh the actual selected torrent.
  if(request.kind!=CategoryCommandKind::Assign){if(const auto target=actionTarget();target&&model_->accepts(*target)){request.target=*target;}}
  request.session=columnSession_;request.request=++categoryRequest_;pendingCategoryCommand_=request;
  const QPointer<LibraryWindow> guard(this);if(categoryDialog_){categoryDialog_->setPending(request);if(!guard){return true;}}
  emit categoryCommandRequested(std::move(request));return true;
}
void LibraryWindow::setCategoryCommandFinished(CategoryCommandPresentation packet){
  if(!packet||!pendingCategoryCommand_||packet->session!=columnSession_||packet->session!=pendingCategoryCommand_->session
      ||packet->request!=pendingCategoryCommand_->request||packet->kind!=pendingCategoryCommand_->kind
      ||packet->requestedTarget!=pendingCategoryCommand_->target||packet->failure.size()>8192||packet->refreshFailure.size()>8192){return;}
  const auto command=*pendingCategoryCommand_;
  const bool success=packet->status==CategoryCommandStatus::Loaded||packet->status==CategoryCommandStatus::Saved||packet->status==CategoryCommandStatus::Unchanged;
  if(success){
    if(!packet->failure.isEmpty()||!packet->libraryRevision||!packet->generation){return;}
    if((packet->status==CategoryCommandStatus::Loaded)!=(packet->kind==CategoryCommandKind::Reload)){return;}
    if(packet->status==CategoryCommandStatus::Saved&&(!packet->changed||packet->generation<=command.generation)){return;}
    if(packet->status==CategoryCommandStatus::Unchanged&&(packet->changed||packet->generation<command.generation)){return;}
    if(!packet->refreshFailure.isEmpty()){
      if(packet->status!=CategoryCommandStatus::Saved){return;}latestCategoryEditor_.reset();
    }else{
      if(!categoryEditorValues(packet)||(latestCategoryEditor_&&packet->generation<latestCategoryEditor_->generation)){return;}
      if(packet->target&&(!packet->assignmentLoaded||packet->target->hash!=packet->requestedTarget.hash||packet->target->generation!=packet->libraryRevision)){return;}
      latestCategoryEditor_=packet;
    }
  }else if(packet->status==CategoryCommandStatus::Rejected||packet->status==CategoryCommandStatus::Cancelled){
    if(packet->failure.isEmpty()||packet->changed){return;}latestCategoryEditor_.reset();
  }else{return;}
  pendingCategoryCommand_.reset();if(closing_){latestCategoryEditor_.reset();return;}
  const QPointer<LibraryWindow> guard(this);
  if(categoryDialog_){categoryDialog_->setResult(packet);if(!guard){return;}}
  setMessage(!packet->refreshFailure.isEmpty()?tr("Categories saved; reload required: %1").arg(packet->refreshFailure.left(512)):
      packet->status==CategoryCommandStatus::Saved?tr("Categories saved"):packet->status==CategoryCommandStatus::Unchanged?tr("Categories unchanged"):
      packet->status==CategoryCommandStatus::Loaded?tr("Categories loaded"):packet->status==CategoryCommandStatus::Cancelled?tr("Category command cancelled"):tr("Category command rejected: %1").arg(packet->failure.left(512)));
}
void LibraryWindow::rejectCategoryCommand(const CategoryCommand& request){
  auto packet=std::make_shared<CategoryCommandPacket>();packet->session=request.session;packet->request=request.request;packet->kind=request.kind;packet->requestedTarget=request.target;
  packet->status=CategoryCommandStatus::Rejected;packet->failure=tr("The profile worker did not accept the category command; reload before retrying.");setCategoryCommandFinished(std::move(packet));
}
void LibraryWindow::rebuildCategoryMenu(QMenu* menu){
  menu->clear();const auto target=actionTarget();const auto revision=model_->revision();const auto session=columnSession_;
  auto* manage=menu->addAction(tr("Manage categories…"));connect(manage,&QAction::triggered,this,&LibraryWindow::openCategories);
  auto* reload=menu->addAction(tr("Reload for selected torrent"));reload->setObjectName(QStringLiteral("category-load-selection"));reload->setEnabled(target&&!pendingCategoryCommand_&&!busy_&&!closing_&&!offlinePreview_&&session);
  connect(reload,&QAction::triggered,this,[this]{CategoryCommand request;request.kind=CategoryCommandKind::Reload;sendCategoryCommand(std::move(request));});menu->addSeparator();
  const auto values=categoryEditorValues(latestCategoryEditor_);
  if(!target||!values||!latestCategoryEditor_||latestCategoryEditor_->session!=session||latestCategoryEditor_->target!=target
      ||!latestCategoryEditor_->assignmentLoaded||latestCategoryEditor_->libraryRevision!=revision){
    auto* hint=menu->addAction(tr("Reload categories for the current selection"));hint->setEnabled(false);return;
  }
  const auto generation=latestCategoryEditor_->generation;const auto captured=*target;
  const bool enabled=latestCategoryEditor_->writable&&!latestCategoryEditor_->recoveryRequired&&!pendingCategoryCommand_&&!busy_&&!closing_&&!offlinePreview_;
  const auto add=[&](QString label,std::optional<QString> name){
    auto* action=menu->addAction(label);action->setObjectName(name?QStringLiteral("category-assign-%1").arg(*name):QStringLiteral("category-uncategorise"));action->setCheckable(true);
    const bool present=name?latestCategoryEditor_->assignmentResolved&&latestCategoryEditor_->assignedName==name:!latestCategoryEditor_->assignmentResolved;
    action->setChecked(present);action->setEnabled(enabled);const QPointer<QAction> item(action);
    connect(action,&QAction::triggered,this,[this,item,name,captured,generation,revision,session,present]{
      if(item){const QSignalBlocker restore(item);item->setChecked(present);} // No optimistic saved membership.
      if(session!=columnSession_||revision!=model_->revision()||!model_->accepts(captured)||actionTarget()!=std::optional{captured}
          ||!latestCategoryEditor_||latestCategoryEditor_->generation!=generation){return;}
      CategoryCommand request;request.kind=CategoryCommandKind::Assign;request.name=name;request.target=captured;request.session=session;request.generation=generation;sendCategoryCommand(std::move(request));
    });
  };
  add(tr("Uncategorized"),{});
  for(const auto& value:values->categories){if(value.kind==core::SavedCategoryKind::User){const auto name=QString::fromStdString(value.name);add(name.isEmpty()?tr("(empty name)"):name,name);}}
  if(latestCategoryEditor_->assignedName&&!latestCategoryEditor_->assignmentResolved){auto* hint=menu->addAction(tr("Unresolved saved category: %1").arg(*latestCategoryEditor_->assignedName));hint->setEnabled(false);}
}
