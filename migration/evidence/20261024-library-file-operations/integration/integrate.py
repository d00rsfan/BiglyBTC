from pathlib import Path
import hashlib,json
n=Path('/home/yurii/Java/BiglyBTC');w=Path(__file__).resolve().parent;sdir=w/'staging';before={}
def read(rel):
 p=n/('preview/native/'+rel);before[str(p.relative_to(n))]=hashlib.sha256(p.read_bytes()).hexdigest();return p.read_text()
def save(rel,s):
 p=sdir/'preview/native'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s)
def replace(s,a,b):assert s.count(a)==1,a[:100];return s.replace(a,b)
s=read('app/session_bridge.hpp');s=replace(s,'#pragma once','#pragma once\n#include "file_operation_presentation.hpp"')
s=replace(s,'  bool refresh();','''  bool prepareFileOperation(ui::FileOperationRequest);
  bool executeFileOperation(ui::FileOperationSubmission);
  void cancelFileOperation(ui::FileOperationRequest);
  bool createTorrent(ui::TorrentCreationRequest);
  void cancelTorrentCreation(ui::TorrentCreationRequest);
  bool refresh();''')
s=replace(s,'signals:\n','''signals:
  void fileOperationPrepared(biglybtc::ui::FileOperationPlanPresentation);
  void fileOperationFinished(biglybtc::ui::FileOperationResultPresentation);
  void torrentCreationProgress(biglybtc::ui::TorrentCreationProgressPresentation);
  void torrentCreated(biglybtc::ui::TorrentCreationResultPresentation);
''')
s=replace(s,'  void ready(ui::Presentation snapshot, QString message = {});','  void creationProgress(ui::TorrentCreationProgressPresentation);\n  void ready(ui::Presentation snapshot, QString message = {});')
s=replace(s,'  bool busy_ = false;','''  std::optional<ui::FileOperationRequest> guiFileOperation_;
  std::optional<ui::TorrentCreationRequest> guiCreation_;
  ui::TorrentCreationProgressPresentation latestCreationProgress_;
  bool creationProgressQueued_ = false;
  bool busy_ = false;''');save('app/session_bridge.hpp',s)
s=read('app/session_bridge.cpp');s=replace(s,'#include "torrent_intake.hpp"','#include "torrent_intake.hpp"\n#include "file_operations.hpp"\n#include "torrent_creator.hpp"\n#include <thread>')
s=replace(s,'  std::unique_ptr<storage::SchedulerService> scheduler;','''  std::optional<storage::PreparedFileOperation> filePlan;
  ui::FileOperationPlanPresentation filePlanPacket;
  bool fileRecoveryRequired = false;
  std::unique_ptr<storage::SchedulerService> scheduler;''')
# Recovery only while no live owner exists; a journal is an already-confirmed,
# identity-bound intent. Conflicts stop startup without overwriting anything.
s=replace(s,'    auto lease = take(storage::ProfileLease::acquire(profile));\n    static_cast<void>(take(storage::recoverTorrentIntake(lease)));','    auto lease = take(storage::ProfileLease::acquire(profile));\n    static_cast<void>(take(storage::recoverFileOperation(lease)));\n    static_cast<void>(take(storage::recoverTorrentIntake(lease)));')
s=replace(s,'    host_->normalNetwork = true;\n    static_cast<void>(take(storage::recoverTorrentIntake(host_->lease)));','    host_->normalNetwork = true;\n    static_cast<void>(take(storage::recoverFileOperation(host_->lease)));\n    static_cast<void>(take(storage::recoverTorrentIntake(host_->lease)));')
# Every existing profile mutator shares the same pending-operation gate.
for signature in ['bool SessionBridge::downloadAction(', 'bool SessionBridge::torrentImport(', 'bool SessionBridge::magnetImport(', 'bool SessionBridge::filePolicy(']:
 start=s.index(signature);point=s.index('    if (take(storage::recoverTorrentIntake(host_->lease)))',start) if signature.startswith(('bool SessionBridge::downloadAction','bool SessionBridge::filePolicy')) else s.index('    static_cast<void>(take(storage::recoverTorrentIntake(host_->lease)));',start)
 s=s[:point]+'''    if (host_->fileRecoveryRequired) { throw Failure{{storage::ErrorCode::Conflict, "Restart to recover the interrupted file operation before changing the profile"}}; }
'''+s[point:]
s=replace(s,'        if (host_->engine) {\n          const auto before', '        if (host_->engine) {\n          const auto before') if False else s
# Do not let scheduler mutate pause/config intent across an unresolved file journal.
s=replace(s,'    ensureScheduler();if(!host_->scheduler){return;}','    if(host_->fileRecoveryRequired){return;}\n    ensureScheduler();if(!host_->scheduler){return;}')
last=s.rfind('} // namespace biglybtc::app');assert last>=0;s=s[:last]+'#include "file_operations_bridge.inc"\n'+s[last:];save('app/session_bridge.cpp',s)
s=(w/'file_operations_bridge.inc').read_text();save('app/file_operations_bridge.inc',s)
s=read('ui/library_window.hpp');s=replace(s,'#pragma once','#pragma once\n#include "file_operation_presentation.hpp"');s=replace(s,'class OptionsDialog;','class FileOperationDialog;\nclass TorrentCreationDialog;\nclass OptionsDialog;')
s=replace(s,'  void setIntakeDefaults(quint64 session, QString directory);','''  void setFileOperationPlan(FileOperationPlanPresentation);
  void setFileOperationResult(FileOperationResultPresentation);
  void setTorrentCreationProgress(TorrentCreationProgressPresentation);
  void setTorrentCreationResult(TorrentCreationResultPresentation);
  void rejectFileOperationRequest(FileOperationRequest);
  void rejectFileOperationSubmission(const FileOperationSubmission&);
  void rejectTorrentCreationRequest(const TorrentCreationRequest&);
  void setIntakeDefaults(quint64 session, QString directory);''')
s=replace(s,'signals:\n','''signals:
  void fileOperationPlanRequested(biglybtc::ui::FileOperationRequest);
  void fileOperationExecuteRequested(biglybtc::ui::FileOperationSubmission);
  void fileOperationCancelRequested(biglybtc::ui::FileOperationRequest);
  void torrentCreateRequested(biglybtc::ui::TorrentCreationRequest);
  void torrentCreateCancelRequested(biglybtc::ui::TorrentCreationRequest);
''')
s=replace(s,'  void openOptions();','''  void openFileOperation(FileOperation);
  void openTorrentCreation();
  QPointer<FileOperationDialog> fileOperationDialog_;
  QPointer<TorrentCreationDialog> torrentCreationDialog_;
  std::array<QPointer<QAction>,4> fileOperationActions_;
  QPointer<QAction> createTorrentAction_;
  std::optional<FileOperationRequest> activeFileOperation_;
  bool fileOperationExecuting_ = false;
  std::optional<TorrentCreationRequest> activeTorrentCreation_;
  quint64 creationDialogRevision_ = 0;
  void openOptions();''');save('ui/library_window.hpp',s)
s=read('ui/library_window.cpp');s='#include "file_operation_dialog.hpp"\n#include "torrent_creation_dialog.hpp"\n'+s
s=replace(s,'  toolbar->addAction(unavailable(torrent, tr("Delete"), QStringLiteral("remove"), QStyle::SP_TrashIcon));','''  const std::array<QString,4> operationNames{tr("Remove Torrent…"),tr("Remove and Delete Data…"),tr("Move Data…"),tr("Set Data Location…")};
  const std::array<QString,4> operationIds{QStringLiteral("classic-remove"),QStringLiteral("classic-delete-data"),QStringLiteral("classic-move-data"),QStringLiteral("classic-set-data-location")};
  for(std::size_t index=0;index<operationNames.size();++index){
    auto* item=torrent->addAction(operationNames[index]);item->setObjectName(operationIds[index]);item->setEnabled(false);
    fileOperationActions_[index]=item;connect(item,&QAction::triggered,this,[this,index]{openFileOperation(static_cast<FileOperation>(index));});
  }
  fileOperationActions_[0]->setShortcut(QKeySequence(QStringLiteral("Delete")));
  toolbar->addAction(fileOperationActions_[0]);
  createTorrentAction_=file->addAction(tr("Create Torrent…"));createTorrentAction_->setObjectName(QStringLiteral("classic-create-torrent"));
  createTorrentAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));createTorrentAction_->setEnabled(false);
  connect(createTorrentAction_,&QAction::triggered,this,&LibraryWindow::openTorrentCreation);''')
s=replace(s,'  menu->addAction(findChild<QAction*>(QStringLiteral("classic-remove")));','  for(const auto& action:fileOperationActions_){if(action){menu->addAction(action);}}')
s=replace(s,'  if (schedulerAction_) { schedulerAction_->setEnabled(!closing_ && !busy_ && columnSession_ != 0); }','''  const auto fileTargets=selectedDownloadTargets();
  const bool fileActionsAllowed=!closing_&&!busy_&&!offlinePreview_&&columnSession_&&fileTargets&&fileTargets->size()==1;
  for(const auto& action:fileOperationActions_){if(action){action->setEnabled(fileActionsAllowed);}}
  if(createTorrentAction_){createTorrentAction_->setEnabled(!closing_&&!busy_&&!offlinePreview_&&columnSession_);}
  if (schedulerAction_) { schedulerAction_->setEnabled(!closing_ && !busy_ && columnSession_ != 0); }''')
s=replace(s,'  QPointer<LibraryWindow> optionsGuard(this);\n  if (schedulerDialog_) { schedulerDialog_->setBusy(value); }','''  QPointer<LibraryWindow> optionsGuard(this);
  if(fileOperationDialog_){fileOperationDialog_->setBusy(value);}if(!optionsGuard){return;}
  if(torrentCreationDialog_){torrentCreationDialog_->setBusy(value);}if(!optionsGuard){return;}
  if(!value&&activeFileOperation_&&fileOperationDialog_&&fileOperationDialog_->pending()){
    const auto request=*activeFileOperation_;
    if(fileOperationExecuting_){FileOperationSubmission v{fileOperationDialog_->plan(),false};rejectFileOperationSubmission(v);}
    else{rejectFileOperationRequest(request);}
    if(!optionsGuard){return;}
  }
  if(!value&&activeTorrentCreation_&&torrentCreationDialog_&&torrentCreationDialog_->pending()){rejectTorrentCreationRequest(*activeTorrentCreation_);if(!optionsGuard){return;}}
  if (schedulerDialog_) { schedulerDialog_->setBusy(value); }''')
s=replace(s,'  columnSession_ = session;','''  columnSession_ = session;
  activeFileOperation_.reset();activeTorrentCreation_.reset();
  if(fileOperationDialog_){const auto dialog=fileOperationDialog_;dialog->invalidateSession();if(!guard){return;}if(dialog){dialog->close();}if(!guard){return;}}
  if(torrentCreationDialog_){const auto dialog=torrentCreationDialog_;dialog->invalidateSession();if(!guard){return;}if(dialog){dialog->close();}if(!guard){return;}}''')
s=replace(s,'void LibraryWindow::closeEvent(QCloseEvent* event) {\n  QPointer<LibraryWindow> optionsGuard(this);','''void LibraryWindow::closeEvent(QCloseEvent* event) {
  QPointer<LibraryWindow> optionsGuard(this);
  if(fileOperationDialog_&&fileOperationDialog_->isVisible()&&!fileOperationDialog_->close()){event->ignore();return;}if(!optionsGuard){return;}
  if(torrentCreationDialog_&&torrentCreationDialog_->isVisible()&&!torrentCreationDialog_->close()){event->ignore();return;}if(!optionsGuard){return;}''')
last=s.rfind('} // namespace biglybtc::ui');assert last>=0;s=s[:last]+'#include "file_operations_window.inc"\n'+s[last:];save('ui/library_window.cpp',s);save('ui/file_operations_window.inc',(w/'file_operations_window.inc').read_text())
s=read('app/main_desktop.cpp');needle='    QObject::connect(&window, &Window::filePolicyRequested, &bridge, &Bridge::filePolicy);';s=replace(s,needle,needle+'''
    QObject::connect(&window,&Window::fileOperationPlanRequested,&bridge,[&](biglybtc::ui::FileOperationRequest r){if(!bridge.prepareFileOperation(r)){window.rejectFileOperationRequest(r);}});
    QObject::connect(&window,&Window::fileOperationExecuteRequested,&bridge,[&](biglybtc::ui::FileOperationSubmission r){if(!bridge.executeFileOperation(r)){window.rejectFileOperationSubmission(r);}});
    QObject::connect(&window,&Window::fileOperationCancelRequested,&bridge,&Bridge::cancelFileOperation);
    QObject::connect(&window,&Window::torrentCreateRequested,&bridge,[&](biglybtc::ui::TorrentCreationRequest r){if(!bridge.createTorrent(r)){window.rejectTorrentCreationRequest(r);}});
    QObject::connect(&window,&Window::torrentCreateCancelRequested,&bridge,&Bridge::cancelTorrentCreation);
    QObject::connect(&bridge,&Bridge::fileOperationPrepared,&window,&Window::setFileOperationPlan);
    QObject::connect(&bridge,&Bridge::fileOperationFinished,&window,&Window::setFileOperationResult);
    QObject::connect(&bridge,&Bridge::torrentCreationProgress,&window,&Window::setTorrentCreationProgress);
    QObject::connect(&bridge,&Bridge::torrentCreated,&window,&Window::setTorrentCreationResult);''');save('app/main_desktop.cpp',s)
(w/'integration-inputs.json').write_text(json.dumps(before,indent=2)+'\n');print('Root Host/Window/main staged; production unchanged, no compilation')
