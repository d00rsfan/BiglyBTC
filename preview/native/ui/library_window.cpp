#include "classic_sort_header.hpp"
#include <QProgressBar>
#include "recheck_dialog.hpp"
#include "publication_controls.hpp"
#include "publication_review_dialog.hpp"
#include "file_operation_dialog.hpp"
#include "torrent_creation_dialog.hpp"
#include "torrent_intake_dialog.hpp"
#include "library_window.hpp"
#include "classic_status_bar.hpp"
#include "table_viewport_guard.hpp"
#include <QSet>
#include "metasearch_page.hpp"
#include "metasearch_magnet_dialog.hpp"
#include "download_history_page.hpp"
#include "rss_sources_page.hpp"
#include "subscriptions_page.hpp"
#include <atomic>
#include "options_dialog.hpp"
#include "scheduler_dialog.hpp"
#include "local_rating_dialog.hpp"
#include "rating_delegate.hpp"
#include "rating_stars.hpp"
#include <QMouseEvent>
#include "manual_tag_editor.hpp"
#include "category_editor.hpp"
#include "classic_filter.hpp"
#include "classic_columns.hpp"
#include "statistics_page.hpp"
#include "all_peers_page.hpp"
#include "engine_activity_history.hpp"
#include "native_activity_history.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QApplication>
#include <QVBoxLayout>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <set>
#include <functional>
#include <limits>

namespace biglybtc::ui {
namespace {
// A snapshot revision changes rates and status, not the user's menu intent.
// Compare unique torrent identities; commands still use fresh model targets.
bool sameTorrentSet(const DownloadTargets& left, const DownloadTargets& right) {
  if(left.size()!=right.size()){return false;}
  QSet<QString> remaining;
  for(const auto& target:left){remaining.insert(target.hash);}
  if(remaining.size()!=static_cast<qsizetype>(left.size())){return false;}
  for(const auto& target:right){if(!remaining.remove(target.hash)){return false;}}
  return remaining.empty();
}

bool importPath(const QString& value) {
  return !value.trimmed().isEmpty() && value.size() <= 4096 && !value.contains(QChar(u'\0'))
      && QDir::isAbsolutePath(value);
}
bool torrentImportFields(const QString& metadata, const QString& destination) {
  return importPath(metadata) && metadata.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive)
      && importPath(destination);
}
bool magnetImportFields(const QString& uri, const QString& destination) {
  if (uri.size() > 16384 || uri.contains(QChar(u'\0')) || uri.contains(QChar(u'\n'))
      || uri.contains(QChar(u'\r')) || !importPath(destination)) { return false; }
  const QUrl parsed(uri, QUrl::StrictMode);
  if (!parsed.isValid() || parsed.scheme().compare(QStringLiteral("magnet"), Qt::CaseInsensitive) != 0
      || !parsed.host().isEmpty() || !parsed.path().isEmpty()) { return false; }
  const QUrlQuery query(parsed);
  return query.hasQueryItem(QStringLiteral("xt")) && !query.queryItemValue(QStringLiteral("xt")).isEmpty();
}
QSortFilterProxyModel* proxy(QTableView* table) {
  return static_cast<QSortFilterProxyModel*>(table->model());
}
QLabel* field(QWidget* parent) {
  auto* value = new QLabel(QString::fromUtf8("—"), parent);
  value->setTextFormat(Qt::PlainText);
  value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  value->setWordWrap(true);
  return value;
}
template<typename Page, typename Result>
void applySavedResult(Page& page, quint64 request, const Result& result) {
  if (page.generation() != request || page.state() != SavedPageState::Loading) { return; }
  switch (result.state) {
    case SavedPageState::Ready:
      if (result.source && result.snapshot) { page.setReady(request, result.snapshot, *result.source); }
      else { page.setFailed(request, QObject::tr("Saved configuration result is incomplete")); }
      break;
    case SavedPageState::Missing: page.setMissing(request); break;
    case SavedPageState::Failed: page.setFailed(request, result.failure); break;
    default: page.setFailed(request, QObject::tr("Saved configuration result has an invalid state")); break;
  }
}
}

LibraryWindow::~LibraryWindow(){
  closing_=true;
  if(activeRecheck_&&activeRecheck_->receiver){activeRecheck_->receiver->request_stop();}activeRecheck_.reset();
  const auto recheck=recheckDialog_;recheckDialog_.clear();if(recheck){QObject::disconnect(recheck.data(),nullptr,this,nullptr);delete recheck.data();}
  const auto publication=publicationReview_;publicationReview_.clear();
  if(publication){QObject::disconnect(publication.data(),nullptr,this,nullptr);delete publication.data();}
  // Retire owned signal sources while the derived Window and its members are
  // still alive. QObject's later base teardown is too late for child-destroyed
  // lambdas which access those members; QPointer alone does not fix that gap.
  if(mainTabs_){mainTabs_->blockSignals(true);}
  const auto page=metasearchPage_;metasearchPage_.clear();
  if(page){QObject::disconnect(page.data(),nullptr,this,nullptr);delete page.data();}
  pendingMetasearchReview_.reset();metasearchReview_.reset();metasearchMagnetReview_.reset();
  const auto magnet=metasearchMagnetDialog_;metasearchMagnetDialog_.clear();
  if(magnet){QObject::disconnect(magnet.data(),nullptr,this,nullptr);delete magnet.data();}
  activeIntakePreview_.reset();activeIntakeSubmission_.reset();
  const auto dialog=intakeDialog_;intakeDialog_.clear();
  if(dialog){QObject::disconnect(dialog.data(),nullptr,this,nullptr);delete dialog.data();}
}

LibraryWindow::LibraryWindow() : model_(new LibraryModel(this)),
    incompleteTable_(new QTableView(this)), completeTable_(new QTableView(this)), activeTable_(incompleteTable_),
    mainTabs_(new QTabWidget(this)), torrentsPage_(new QWidget(mainTabs_)), detailTabs_(new QTabWidget(this)), filter_(new QLineEdit(this)),
    splitter_(new QSplitter(Qt::Vertical, this)), incompleteCount_(new QLabel(this)), completeCount_(new QLabel(this)),
    generalName_(field(this)), generalState_(field(this)), generalSize_(field(this)), generalProgress_(field(this)),
    generalReceived_(field(this)), generalHash_(field(this)), previewStatus_(new QLabel(tr("Preview"), this)),
    message_(new QLabel(this)), filesModel_(new FileModel(this)), filesProxy_(new QSortFilterProxyModel(this)),
    filesTable_(new QTableView(this)) {
  setWindowTitle(QStringLiteral("BiglyBTC"));
  setAccessibleName(tr("BiglyBTC classic torrent library"));
  resize(1250, 800);
  mainTabs_->setObjectName(QStringLiteral("classic-main-tabs"));
  mainTabs_->setAccessibleName(tr("Main views"));
  detailTabs_->setObjectName(QStringLiteral("torrent-details"));
  detailTabs_->setAccessibleName(tr("Selected torrent details"));
  splitter_->setObjectName(QStringLiteral("classic-torrent-split"));
  splitter_->setChildrenCollapsible(false);
  splitter_->setHandleWidth(6);
  for (auto* table : {incompleteTable_, completeTable_}) {
    table->setHorizontalHeader(new ClassicSortHeader(table));
    auto* grouped = new ClassicFilter(table == completeTable_, table);
    grouped->setSourceModel(model_);
    table->setModel(grouped);
    table->setObjectName(table == completeTable_ ? QStringLiteral("complete-torrents") : QStringLiteral("incomplete-torrents"));
    table->setAccessibleName(table == completeTable_ ? tr("Complete Torrents") : tr("Incomplete Torrents"));
    table->setSortingEnabled(true);
    table->sortByColumn(0, Qt::AscendingOrder);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setAlternatingRowColors(true);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->setMinimumHeight(90);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionsMovable(true);
    table->horizontalHeader()->setMinimumSectionSize(16);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setColumnWidth(1, 100);
    table->setColumnWidth(3, 85);
    table->setColumnWidth(9, 180);
    table->setColumnWidth(10, 220);
    table->setColumnWidth(11, 250);
    table->setColumnWidth(12, 180);
    table->setColumnWidth(13, 300);
    table->setColumnWidth(14, 120);
    table->setItemDelegateForColumn(33,new RatingDelegate(table));
    table->setColumnWidth(33,95);
    for (int column=10;column<LibraryModel::Columns;++column) { table->setColumnHidden(column, true); }
    table->setColumnHidden(table == completeTable_ ? 4 : 5, true);
    table->setColumnHidden(8, true);
    table->installEventFilter(this);
    table->viewport()->installEventFilter(this);
    connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this, table] {
      if (!updating_) { activate(table); selectionChanged(); }
    });
    connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this, table] {
      if (!updating_) { activate(table); selectionChanged(); }
    });
    connect(table, &QTableView::customContextMenuRequested, this, [this, table](const QPoint& point) { popup(table, point); });
    auto* panel = new QWidget(splitter_);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(table == completeTable_ ? completeCount_ : incompleteCount_);
    layout->addWidget(table);
    splitter_->addWidget(panel);
  }
  incompleteColumns_ = new ClassicColumns(*incompleteTable_, false);
  completeColumns_ = new ClassicColumns(*completeTable_, true);
  splitter_->setStretchFactor(0, 3);
  splitter_->setStretchFactor(1, 7);
  splitter_->setSizes({300, 700});
  filesProxy_->setSourceModel(filesModel_);
  filesProxy_->setSortRole(Qt::UserRole);
  filesProxy_->setFilterRole(Qt::UserRole + 2);
  filesProxy_->setFilterFixedString(QStringLiteral("file"));
  filesTable_->setModel(filesProxy_);
  filesTable_->setAccessibleName(tr("Torrent files"));
  filesTable_->setSortingEnabled(true);
  filesTable_->sortByColumn(0, Qt::AscendingOrder);
  filesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  filesTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  filesTable_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(filesTable_, &QTableView::customContextMenuRequested, this, &LibraryWindow::filePopup);
  connect(filesTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
      [this] { queueDownloadActions(); });
  connect(filesModel_, &QAbstractItemModel::modelReset, this, [this] { queueDownloadActions(); });
  connect(filesModel_, &QAbstractItemModel::dataChanged, this, [this] { queueDownloadActions(); });
  const std::array<QString, 4> policyNames{tr("Normal"), tr("High"), tr("Low"), tr("Do not download")};
  const std::array<QString, 4> policyIds{QStringLiteral("files-priority-normal"), QStringLiteral("files-priority-high"),
      QStringLiteral("files-priority-low"), QStringLiteral("files-priority-skip")};
  const std::array<int, 4> priorities{0, 1, -1, 0};
  for (std::size_t i = 0; i < filePolicyActions_.size(); ++i) {
    auto* item = new QAction(policyNames[i], this);
    item->setObjectName(policyIds[i]);
    item->setEnabled(false);
    item->setToolTip(tr("Change selected files after the torrent has stopped."));
    filePolicyActions_[i] = item;
    connect(item, &QAction::triggered, this, [this, priority = priorities[i], skipped = i == 3] {
      dispatchFilePolicy(priority, skipped);
    });
  }
  fileCompaction_ = new QAction(tr("Do not download and reclaim space…"), this);
  fileCompaction_->setObjectName(QStringLiteral("files-reclaim-space"));
  fileCompaction_->setEnabled(false);
  fileCompaction_->setToolTip(tr("Select one linear file. Stop all torrents and metadata downloads, then refresh file details. Boundary data needed by other files is kept."));
  connect(fileCompaction_, &QAction::triggered, this, &LibraryWindow::dispatchFileCompaction);
  const std::array<QString,4> filePathNames{tr("Rename or move file…"),tr("Use an existing file and recheck…"),tr("Move back to original file path…"),tr("Replace destination and retain prior file…")};
  const std::array<QString,4> filePathIds{QStringLiteral("files-rename"),QStringLiteral("files-retarget"),QStringLiteral("files-revert"),QStringLiteral("files-replace")};
  for(std::size_t n=0;n<filePathActions_.size();++n){auto* action=new QAction(filePathNames[n],this);action->setObjectName(filePathIds[n]);action->setEnabled(false);filePathActions_[n]=action;
    connect(action,&QAction::triggered,this,[this,n]{openFileMapping(static_cast<FileOperation>(static_cast<unsigned>(FileOperation::MoveFile)+n));});}
  filesTable_->setAlternatingRowColors(true);
  filesTable_->verticalHeader()->hide();
  filesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  auto* general = new QWidget(detailTabs_);
  auto* form = new QFormLayout(general);
  form->addRow(tr("Name:"), generalName_);
  form->addRow(tr("State:"), generalState_);
  form->addRow(tr("Size:"), generalSize_);
  form->addRow(tr("Progress:"), generalProgress_);
  form->addRow(tr("Received / sent:"), generalReceived_);
  form->addRow(tr("Hash:"), generalHash_);
  detailTabs_->addTab(general, tr("General"));
  detailTabs_->addTab(filesTable_, tr("Files"));
  torrentDetailPages_=new TorrentDetailPages(detailTabs_);
  connect(torrentDetailPages_,&TorrentDetailPages::interestChanged,this,&LibraryWindow::updateTorrentDetailInterest);
  connect(torrentDetailPages_,&TorrentDetailPages::downloadChatRequested,this,&LibraryWindow::downloadChatRequested);connect(torrentDetailPages_,&TorrentDetailPages::downloadChatObservationChanged,this,&LibraryWindow::downloadChatObservationChanged);auto* downloadChat=torrentDetailPages_->chatPage();connect(downloadChat,&ChatPage::sendRequested,this,&LibraryWindow::chatSendRequested);connect(downloadChat,&ChatPage::optionsRequested,this,&LibraryWindow::chatOptionsRequested);connect(downloadChat,&ChatPage::nicknameModeRequested,this,&LibraryWindow::chatNicknameModeRequested);connect(downloadChat,&ChatPage::nicknameRequested,this,&LibraryWindow::chatNicknameRequested);

  connect(torrentDetailPages_,&TorrentDetailPages::trackerRequested,this,[this](bool scrape){const auto target=selected();if(!target||!model_->accepts(*target)||busy_||closing_||torrentTrackerPending_||!latestTorrentDetail_||latestTorrentDetail_->interest!=torrentDetailInterest_||!latestTorrentDetail_->actual.retained||torrentTrackerRequest_==UINT64_MAX){return;}TorrentTrackerCommand command;command.session=columnSession_;command.request=++torrentTrackerRequest_;command.target=*target;command.admission=latestTorrentDetail_->actual.admission;command.scrape=scrape;torrentTrackerPending_=command.request;emit torrentTrackerRequested(command);});
  connect(torrentDetailPages_,&TorrentDetailPages::optionsRequested,this,[this](bool rates,int first,int second){
    const auto target=selected();if(!target||!model_->accepts(*target)||busy_||closing_||torrentOptionsPending_||!latestTorrentDetail_||latestTorrentDetail_->interest!=torrentDetailInterest_||!latestTorrentDetail_->optionsKnown||!latestTorrentDetail_->optionsWritable||torrentOptionsRequest_==UINT64_MAX){return;}
    TorrentOptionsCommand command;command.session=columnSession_;command.request=++torrentOptionsRequest_;command.target=*target;command.rates=rates;const auto offset=rates?0:2;command.expected={latestTorrentDetail_->savedOptions[std::size_t(offset)],latestTorrentDetail_->savedOptions[std::size_t(offset+1)]};command.desired={first,second};torrentOptionsPending_=command.request;torrentDetailPages_->setOptionsWritable(false);emit torrentOptionsRequested(command);
  });
  connect(torrentDetailPages_,&TorrentDetailPages::tagsRequested,this,[this]{QMenu menu(this);rebuildManualTagMenu(&menu);menu.exec(QCursor::pos());});
  connect(detailTabs_, &QTabWidget::currentChanged, this, [this](int index) {
    QPointer<LibraryWindow> guard(this);
    if (index == 1) { suppressAutomaticDetails_ = false; requestDetails(); }
    if (guard) { queueDownloadActions(); }
  });
  auto* library = torrentsPage_;
  library->setObjectName(QStringLiteral("classic-my-torrents-page"));
  auto* layout = new QVBoxLayout(library);
  layout->setContentsMargins(6, 4, 6, 4);
  auto* header = new QHBoxLayout;
  auto* heading = new QLabel(tr("My Torrents"), library);
  heading->setAlignment(Qt::AlignCenter);
  header->addWidget(heading, 1);
  auto* filterLabel = new QLabel(tr("&Filter:"), library);
  filterLabel->setBuddy(filter_);
  filter_->setObjectName(QStringLiteral("classic-name-filter"));
  filter_->setAccessibleName(tr("Filter torrents by name"));
  filter_->setPlaceholderText(tr("Filter by name"));
  filter_->setClearButtonEnabled(true);
  filter_->setMaximumWidth(340);
  header->addWidget(filterLabel);
  header->addWidget(filter_);
  startupProgress_=new QProgressBar(library);startupProgress_->setObjectName(QStringLiteral("classic-startup-progress"));startupProgress_->setAccessibleName(tr("Restoring transfers"));
  startupProgress_->setMaximumWidth(220);startupProgress_->setFormat(tr("Restoring transfers: %v / %m"));startupProgress_->hide();header->addWidget(startupProgress_);
  layout->addLayout(header);
  auto* content = new QSplitter(Qt::Vertical, library);detailsHome_=content;
  content->setChildrenCollapsible(false);
  content->addWidget(splitter_);
  content->addWidget(detailTabs_);detailTabs_->hide();
  content->setStretchFactor(0, 3);
  content->setStretchFactor(1, 1);
  content->setSizes({540, 200});
  layout->addWidget(content);
  mainTabs_->addTab(library, tr("My Torrents"));
  mainTabs_->setTabsClosable(true);
  setCentralWidget(mainTabs_);
  connect(filter_, &QLineEdit::textChanged, this, [this](const QString& value) {
    const auto hashes = selectedHashes();
    updating_ = true;
    static_cast<ClassicFilter*>(incompleteTable_->model())->setQuery(value);
    static_cast<ClassicFilter*>(completeTable_->model())->setQuery(value);
    restoreSelection(hashes);
    updating_ = false;
    QPointer<LibraryWindow> guard(this);
    selectionChanged();
    if (guard) { updateCounts(); }
  });
  message_->setTextFormat(Qt::PlainText);
  message_->setAccessibleName(tr("Client status"));
  classicStatus_=new ClassicStatusBar(this);setStatusBar(classicStatus_);previewStatus_->hide();
  statusBar()->addWidget(message_, 1);
  connect(classicStatus_,&ClassicStatusBar::statisticsRequested,this,[this](const QString& id){showStatistics();if(!statisticsPage_){return;}auto& tabs=statisticsPage_->tabs();for(int i=0;i<tabs.count();++i){if(tabs.widget(i)->objectName()==id){tabs.setCurrentIndex(i);break;}}});
  azureusIndicator_=new QToolButton(this);azureusIndicator_->setObjectName(QStringLiteral("azureus-dht-indicator"));azureusIndicator_->setAutoRaise(true);azureusIndicator_->setText(tr("Azureus DHT"));azureusIndicator_->setAccessibleName(tr("Azureus DHT status"));azureusIndicator_->setToolTip(tr("Open Statistics to inspect Azureus DHT"));statusBar()->insertPermanentWidget(3,azureusIndicator_);connect(azureusIndicator_,&QToolButton::clicked,this,&LibraryWindow::showStatistics);
  chatIndicator_=new QToolButton(this);chatIndicator_->setObjectName(QStringLiteral("chat-unread-indicator"));chatIndicator_->setAutoRaise(true);chatIndicator_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);chatIndicator_->setAccessibleName(tr("Public chat messages"));statusBar()->insertPermanentWidget(4,chatIndicator_);updateChatIndicator();installEventFilter(this);
  connect(chatIndicator_,&QToolButton::clicked,this,[this]{
    const auto unread=std::ranges::find_if(chatSnapshot_.rooms,[](const auto& room){return room.unread>0;});
    if(unread!=chatSnapshot_.rooms.end()){pendingChatSelection_=unread->key;const auto key=unread->key;QPointer<LibraryWindow> guard(this);emit chatSelectRequested(key);if(!guard){return;}}
    else{const auto download=std::ranges::find_if(chatSnapshot_.notifications,[](const auto& room){return room.unread>0;});if(download!=chatSnapshot_.notifications.end()){const auto target=model_->targetAt(model_->rowFor(download->downloadHash));if(target&&torrentDetailPages_){openTorrentDetails(*target,torrentDetailPages_->chatIndex());return;}const auto key=download->key;pendingChatSelection_=key;QPointer<LibraryWindow> guard(this);emit chatJoinRequested(key);if(!guard){return;}}}
    showChats();
  });
  auto* file = menuBar()->addMenu(tr("&File"));
  auto* transfers = menuBar()->addMenu(tr("&Transfers"));
  auto* torrent = menuBar()->addMenu(tr("T&orrent"));
  auto* view = menuBar()->addMenu(tr("&View"));
  auto* tools = menuBar()->addMenu(tr("&Tools"));
  auto* plugins = menuBar()->addMenu(tr("&Plugins"));
  auto* community = menuBar()->addMenu(tr("&Community"));
  auto* help = menuBar()->addMenu(tr("&Help"));
  auto* discoveries=tools->addAction(tr("Swarm Discoveries"));discoveries->setObjectName("classic-swarm-discoveries");view->addAction(discoveries);connect(discoveries,&QAction::triggered,this,&LibraryWindow::showSwarmDiscoveries);
  auto* chats=community->addAction(tr("Public Chat Rooms"));chats->setObjectName(QStringLiteral("classic-general-chats"));connect(chats,&QAction::triggered,this,&LibraryWindow::showChats);
  // Original standard keys: retain BiglyBT's room names for interoperability.
  for(const auto& item:{std::pair{"BiglyBT: General: Help","classic-chat-help"},std::pair{"BiglyBT: Beta: Chat","classic-chat-beta"}}){auto* action=community->addAction(QString::fromLatin1(item.first));action->setObjectName(QString::fromLatin1(item.second));connect(action,&QAction::triggered,this,[this,key=QString::fromLatin1(item.first)]{showChats();emit chatJoinRequested(key);});}
  auto* viewChats=view->addAction(tr("Public Chat Rooms"));viewChats->setObjectName(QStringLiteral("classic-view-general-chats"));connect(viewChats,&QAction::triggered,this,&LibraryWindow::showChats);

  auto* toolbar = addToolBar(tr("Classic controls"));
  toolbar->setObjectName(QStringLiteral("classic-toolbar"));
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->setIconSize(QSize(18, 18));
  auto unavailable = [this](QMenu* menu, const QString& label, const QString& id, QStyle::StandardPixmap icon) {
    auto* action = menu->addAction(style()->standardIcon(icon), label);
    action->setObjectName(QStringLiteral("classic-") + id);
    action->setEnabled(false);
    action->setToolTip(tr("%1 is not available in this preview yet.").arg(label));
    action->setStatusTip(action->toolTip());
    return action;
  };
  file->setObjectName(QStringLiteral("classic-file-menu"));
  openTorrent_ = file->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open Torrent File…"));
  openTorrent_->setObjectName(QStringLiteral("classic-open"));
  openTorrent_->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  openTorrent_->setShortcutContext(Qt::WindowShortcut);
  openTorrent_->setToolTip(tr("Import a torrent file into the current profile."));
  openTorrent_->setEnabled(false);
  connect(openTorrent_.data(), &QAction::triggered, this, [this] { openImportDialog(false); });
  toolbar->addAction(openTorrent_.data());
  openMagnet_ = file->addAction(tr("Open Magnet Link…"));
  openMagnet_->setObjectName(QStringLiteral("classic-open-magnet"));
  openMagnet_->setToolTip(tr("Import a magnet link into the current profile."));
  openMagnet_->setEnabled(false);
  connect(openMagnet_.data(), &QAction::triggered, this, [this] { openImportDialog(true); });
  metasearchAction_=tools->addAction(style()->standardIcon(QStyle::SP_FileDialogContentsView),tr("Search"));metasearchAction_->setObjectName(QStringLiteral("classic-search"));metasearchAction_->setEnabled(false);metasearchAction_->setToolTip(tr("Search the enabled engines saved in this profile."));connect(metasearchAction_,&QAction::triggered,this,&LibraryWindow::showMetasearch);toolbar->addAction(metasearchAction_);view->addAction(metasearchAction_);
  toolbar->addSeparator();
  toolbar->addAction(unavailable(torrent, tr("Launch"), QStringLiteral("launch"), QStyle::SP_MediaPlay));
  torrent->setObjectName(QStringLiteral("classic-torrent-menu"));
  transfers->setObjectName(QStringLiteral("classic-transfers-menu"));
  auto command = [this, torrent](core::DownloadAction action, const QString& label,
      const QString& id, QStyle::StandardPixmap icon) {
    auto* item = torrent->addAction(style()->standardIcon(icon), label);
    item->setObjectName(QStringLiteral("classic-") + id);
    item->setEnabled(false);
    item->setToolTip(tr("Apply %1 to every selected torrent when permitted by the current session.").arg(label));
    downloadActions_[static_cast<std::size_t>(action)] = item;
    connect(item, &QAction::triggered, this, [this, action] { dispatchDownload(action); });
    return item;
  };
  toolbar->addAction(command(core::DownloadAction::MoveTop, tr("Top"), QStringLiteral("top"), QStyle::SP_ArrowUp));
  toolbar->addAction(command(core::DownloadAction::MoveUp, tr("Up"), QStringLiteral("up"), QStyle::SP_ArrowUp));
  toolbar->addAction(command(core::DownloadAction::MoveDown, tr("Down"), QStringLiteral("down"), QStyle::SP_ArrowDown));
  toolbar->addAction(command(core::DownloadAction::MoveBottom, tr("Bottom"), QStringLiteral("bottom"), QStyle::SP_ArrowDown));
  torrent->addSeparator();
  command(core::DownloadAction::Start, tr("Start"), QStringLiteral("start"), QStyle::SP_MediaPlay);
  command(core::DownloadAction::Stop, tr("Stop"), QStringLiteral("stop"), QStyle::SP_MediaStop);
  command(core::DownloadAction::Pause, tr("Pause"), QStringLiteral("pause"), QStyle::SP_MediaPause);
  command(core::DownloadAction::Resume, tr("Resume"), QStringLiteral("resume"), QStyle::SP_MediaPlay);
  command(core::DownloadAction::Queue, tr("Queue"), QStringLiteral("queue"), QStyle::SP_ArrowDown);
  command(core::DownloadAction::ForceStart, tr("Force Start"), QStringLiteral("force-start"), QStyle::SP_MediaPlay);
  command(core::DownloadAction::CancelForceStart, tr("Cancel Force Start"), QStringLiteral("cancel-force-start"), QStyle::SP_MediaStop);
  forceRecheck_=torrent->addAction(style()->standardIcon(QStyle::SP_BrowserReload),tr("Force Recheck"));forceRecheck_->setObjectName("classic-force-recheck");forceRecheck_->setEnabled(false);connect(forceRecheck_,&QAction::triggered,this,&LibraryWindow::requestRecheck);
  toolbar->addSeparator();
  startStop_ = torrent->addAction(style()->standardIcon(QStyle::SP_MediaPlay), tr("Start / Stop"));
  startStop_->setObjectName(QStringLiteral("classic-start-stop"));
  startStop_->setEnabled(false);
  startStop_->setToolTip(tr("Start or stop every selected torrent when all permit the same action."));
  connect(startStop_.data(), &QAction::triggered, this, [this] { dispatchDownloadAlias(false); });
  toolbar->addAction(startStop_.data());
  pauseResume_ = torrent->addAction(style()->standardIcon(QStyle::SP_MediaPause), tr("Pause / Resume"));
  pauseResume_->setObjectName(QStringLiteral("classic-pause-resume"));
  pauseResume_->setEnabled(false);
  pauseResume_->setToolTip(tr("Pause or resume every selected torrent when all permit the same action."));
  connect(pauseResume_.data(), &QAction::triggered, this, [this] { dispatchDownloadAlias(true); });
  toolbar->addAction(pauseResume_.data());
  const std::array<QString,4> operationNames{tr("Remove Torrent…"),tr("Remove and Delete Data…"),tr("Move Data…"),tr("Set Data Location…")};
  const std::array<QString,4> operationIds{QStringLiteral("classic-remove"),QStringLiteral("classic-delete-data"),QStringLiteral("classic-move-data"),QStringLiteral("classic-set-data-location")};
  for(std::size_t index=0;index<operationNames.size();++index){
    auto* item=torrent->addAction(operationNames[index]);item->setObjectName(operationIds[index]);item->setEnabled(false);
    fileOperationActions_[index]=item;connect(item,&QAction::triggered,this,[this,index]{openFileOperation(static_cast<FileOperation>(index));});
  }
  reviewReplacement_=torrent->addAction(tr("Review interrupted file replacement…"));
  reviewReplacement_->setObjectName(QStringLiteral("classic-review-replacement"));reviewReplacement_->setEnabled(false);
  connect(reviewReplacement_,&QAction::triggered,this,[this]{openFileOperation(FileOperation::ReviewReplacement);});
  localRatingAction_=torrent->addAction(tr("My rating…"));
  localRatingAction_->setObjectName(QStringLiteral("classic-my-rating"));
  localRatingAction_->setEnabled(false);
  connect(localRatingAction_,&QAction::triggered,this,&LibraryWindow::openLocalRating);
  auto* categoryMenu=torrent->addMenu(tr("Category"));categoryMenu->setObjectName(QStringLiteral("classic-torrent-category"));
  connect(categoryMenu,&QMenu::aboutToShow,this,[this,categoryMenu]{rebuildCategoryMenu(categoryMenu);});
  auto* tagMenu=torrent->addMenu(tr("Tags"));tagMenu->setObjectName(QStringLiteral("classic-torrent-tags"));
  connect(tagMenu,&QMenu::aboutToShow,this,[this,tagMenu]{rebuildManualTagMenu(tagMenu);});
  auto* hybridMenu=torrent->addMenu(tr("Add hybrid companion (stopped)"));
  hybridMenu->setObjectName(QStringLiteral("classic-hybrid-companion"));
  for(int index=0;index<2;++index){
    auto* action=hybridMenu->addAction(index?tr("Use v2 identity"):tr("Use v1 identity"));
    action->setObjectName(index?QStringLiteral("classic-hybrid-v2"):QStringLiteral("classic-hybrid-v1"));
    action->setToolTip(tr("For a stopped, unlinked linear hybrid. The worker validates full identities and releases only certified retired storage. Adds a stopped companion; data is preserved."));
    action->setEnabled(false);hybridCompanionActions_[static_cast<std::size_t>(index)]=action;
    connect(action,&QAction::triggered,this,[this,index]{requestHybridCompanion(index!=0);});
  }
  fileOperationActions_[0]->setShortcut(QKeySequence(QStringLiteral("Delete")));
  toolbar->addAction(fileOperationActions_[0]);
  createTorrentAction_=file->addAction(tr("Create Torrent…"));createTorrentAction_->setObjectName(QStringLiteral("classic-create-torrent"));
  createTorrentAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));createTorrentAction_->setEnabled(false);
  connect(createTorrentAction_,&QAction::triggered,this,&LibraryWindow::openTorrentCreation);
  auto allCommand = [this, transfers](core::DownloadAction action, const QString& label,
      const QString& id, QStyle::StandardPixmap icon) {
    auto* item = transfers->addAction(style()->standardIcon(icon), label);
    item->setObjectName(QStringLiteral("classic-") + id);
    item->setEnabled(false);
    item->setToolTip(tr("Apply to all eligible torrents, including those hidden by the filter."));
    connect(item, &QAction::triggered, this, [this, action] { dispatchDownload(action, true); });
    return item;
  };
  startAll_ = allCommand(core::DownloadAction::Start, tr("Start All"), QStringLiteral("start-all"), QStyle::SP_MediaPlay);
  stopAll_ = allCommand(core::DownloadAction::Stop, tr("Stop All"), QStringLiteral("stop-all"), QStyle::SP_MediaStop);
  bindDownloadMenu(torrent);
  bindDownloadMenu(transfers);
  auto* statistics = tools->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("&Statistics"));
  statistics->setObjectName(QStringLiteral("classic-statistics"));
  statistics->setShortcut(QKeySequence(QStringLiteral("Ctrl+5")));
  statistics->setShortcutContext(Qt::WindowShortcut);
  connect(statistics, &QAction::triggered, this, &LibraryWindow::showStatistics);
  categoryAction_=tools->addAction(tr("Categories…"));categoryAction_->setObjectName(QStringLiteral("classic-categories"));categoryAction_->setEnabled(false);
  connect(categoryAction_,&QAction::triggered,this,&LibraryWindow::openCategories);
  manualTagAction_=tools->addAction(tr("Tags…"));manualTagAction_->setObjectName(QStringLiteral("classic-manual-tags"));manualTagAction_->setEnabled(false);
  connect(manualTagAction_,&QAction::triggered,this,&LibraryWindow::openManualTags);
  schedulerAction_=tools->addAction(tr("Speed Scheduler…"));
  schedulerAction_->setObjectName(QStringLiteral("classic-speed-scheduler"));
  connect(schedulerAction_,&QAction::triggered,this,&LibraryWindow::openScheduler);
  optionsAction_ = tools->addAction(tr("Options…"));
  optionsAction_->setObjectName(QStringLiteral("classic-options"));
  connect(optionsAction_, &QAction::triggered, this, &LibraryWindow::openOptions);
  unavailable(plugins, tr("Plugin management…"), QStringLiteral("plugins"), QStyle::SP_FileDialogDetailedView);
  unavailable(community, tr("Forums"), QStringLiteral("forums"), QStyle::SP_DialogHelpButton);
  auto* about = help->addAction(tr("About BiglyBTC"));
  connect(about, &QAction::triggered, this, [this] {
    QMessageBox::about(this, tr("About BiglyBTC"), tr("BiglyBTC\nClassic desktop preview\n\nLibrary and file details reflect the selected profile.\nThe full client is under development."));
  });
  file->addSeparator();
  closeTab_ = file->addAction(tr("Close &Tab"));
  closeTab_->setObjectName(QStringLiteral("classic-close-tab"));
  closeTab_->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
  closeTab_->setShortcutContext(Qt::WindowShortcut);
  connect(closeTab_, &QAction::triggered, this, [this] { closePage(mainTabs_->currentIndex()); });
  file->addSeparator();
  auto* quit = file->addAction(tr("&Quit"));
  quit->setShortcut(QKeySequence::Quit);
  connect(quit, &QAction::triggered, this, &QWidget::close);
  auto* myTorrents = view->addAction(tr("My &Torrents"));
  myTorrents->setObjectName(QStringLiteral("classic-my-torrents"));
  myTorrents->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
  myTorrents->setShortcutContext(Qt::WindowShortcut);
  connect(myTorrents, &QAction::triggered, this, &LibraryWindow::showMyTorrents);
  subscriptionsAction_=view->addAction(tr("Subscriptions"));subscriptionsAction_->setObjectName(QStringLiteral("classic-subscriptions"));connect(subscriptionsAction_,&QAction::triggered,this,&LibraryWindow::showSubscriptions);
  rssAction_=view->addAction(tr("RSS Sources"));rssAction_->setObjectName(QStringLiteral("classic-rss-sources"));
  connect(rssAction_,&QAction::triggered,this,&LibraryWindow::showRssSources);
  downloadHistoryAction_=view->addAction(tr("Download &History"));
  downloadHistoryAction_->setObjectName(QStringLiteral("classic-download-history"));
  connect(downloadHistoryAction_,&QAction::triggered,this,&LibraryWindow::showDownloadHistory);
  auto* myTracker = view->addAction(tr("My &Tracker"));
  myTracker->setObjectName(QStringLiteral("classic-my-tracker"));
  myTracker->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
  myTracker->setShortcutContext(Qt::WindowShortcut);
  connect(myTracker, &QAction::triggered, this, &LibraryWindow::showMyTracker);
  auto* myShares = view->addAction(tr("My &Shares"));
  myShares->setObjectName(QStringLiteral("classic-my-shares"));
  myShares->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
  myShares->setShortcutContext(Qt::WindowShortcut);
  connect(myShares, &QAction::triggered, this, &LibraryWindow::showMyShares);
  auto* toolbarVisibility = toolbar->toggleViewAction();
  toolbarVisibility->setText(tr("Toolbar"));
  toolbarVisibility->setObjectName(QStringLiteral("classic-show-toolbar"));
  view->addAction(toolbarVisibility);
  unavailable(view, tr("Transfers Bar"), QStringLiteral("transfer-bar"), QStyle::SP_ArrowUp);
  unavailable(view, tr("Client Stats"), QStringLiteral("client-statistics"), QStyle::SP_FileDialogDetailedView);
  allPeersAction_=view->addAction(tr("All Peers"));allPeersAction_->setObjectName(QStringLiteral("classic-all-peers"));
  connect(allPeersAction_,&QAction::triggered,this,&LibraryWindow::showAllPeers);
  auto* peerVisibility=new QTimer(this);peerVisibility->setInterval(500);connect(peerVisibility,&QTimer::timeout,this,&LibraryWindow::updatePeersVisibility);peerVisibility->start();
  unavailable(view, tr("Remote Pairing…"), QStringLiteral("remote-pairing"), QStyle::SP_DriveNetIcon);
  view->addSeparator();
  for(auto* columns:{incompleteColumns_.data(),completeColumns_.data()}){columns->onRatingSort([this](bool personal){if(!columnSession_||closing_||offlinePreview_){return;}emit ratingSortRequested(columnSession_,personal);});}
  view->addMenu(incompleteColumns_->menu());
  view->addMenu(completeColumns_->menu());
  reloadColumns_ = view->addAction(tr("Reload saved column layout"));
  reloadColumns_->setObjectName(QStringLiteral("classic-reload-column-layout"));
  connect(reloadColumns_, &QAction::triggered, this, [this] { refreshColumnLayouts(); });
  saveColumns_ = view->addAction(tr("Save current column layout"));
  saveColumns_->setObjectName(QStringLiteral("classic-save-column-layout"));
  connect(saveColumns_, &QAction::triggered, this, &LibraryWindow::saveColumnLayout);
  view->addSeparator();
  refresh_ = view->addAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("&Refresh"));
  refresh_->setObjectName(QStringLiteral("classic-refresh"));
  refresh_->setShortcut(QKeySequence(Qt::Key_F5));
  connect(refresh_, &QAction::triggered, this, [this] {
    if (closing_) { return; }
    if(onMetasearchPage()){metasearchPage_->refresh();return;}
    if(onSubscriptionsPage()){subscriptionsPage_->refresh();return;}
    if(onRssSourcesPage()){rssPage_->refreshSources();return;}
    if(onDownloadHistoryPage()){downloadHistoryPage_->reload();return;}
    if(peersPage_&&mainTabs_->currentWidget()==peersPage_){peersPage_->refresh();return;}
    if (onStatisticsPage()) {
      if (statisticsPage_->countrySelected()) { pendingCountryRead_ = statisticsPage_; queueCountryStatistics(); }
      else if (statisticsPage_->manualTagsSelected()) { pendingManualTagRead_ = statisticsPage_; queueManualTagStatistics(); }
      else if (statisticsPage_->historyRangeSelected()) { statisticsPage_->refreshHistoryRange(); }
      else if (statisticsPage_->historyMonthSelected()) { refreshHistoryMonth(statisticsPage_->selectedHistoryMonthDate()); }
      else if (statisticsPage_->historyWeekSelected()) { refreshHistoryWeek(statisticsPage_->selectedHistoryWeekDate()); }
      else if (statisticsPage_->historySelected()) { refreshHistory(statisticsPage_->selectedHistoryDate()); }
      else { refreshStatistics(); }
      return;
    }
    if (onSavedServicePage()) { if(columnSession_){refreshPublicationViews();}else{refreshSavedServices();} return; }
    if (!torrentContextActive() || busy_) { return; }
    suppressAutomaticDetails_ = false;
    detailsRequestedFor_.reset();
    emit refreshRequested();
  });
  padding_ = view->addAction(tr("Show &padding files"));
  padding_->setCheckable(true);
  connect(padding_, &QAction::toggled, this, [this](bool show) {
    filesProxy_->setFilterFixedString(show ? QString{} : QStringLiteral("file"));
  });
  auto* focusFilter = view->addAction(tr("Filter torrents"));
  focusFilter->setShortcut(QKeySequence::Find);
  connect(focusFilter, &QAction::triggered, this, [this] {
    if (closing_) { return; }
    QPointer<LibraryWindow> guard(this);
    showMyTorrents();
    if (!guard) { return; }
    filter_->setFocus();
    if (guard) { filter_->selectAll(); }
  });
  torrent->addSeparator();
  continue_ = torrent->addAction(tr("&Continue"));
  continue_->setObjectName(QStringLiteral("classic-continue"));
  continue_->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
  continue_->setToolTip(tr("Continue a generated transfer using its local test peer."));
  cancel_ = torrent->addAction(tr("&Cancel"));
  cancel_->setObjectName(QStringLiteral("classic-cancel"));
  cancel_->setShortcut(QKeySequence(Qt::Key_Escape));
  files_ = view->addAction(tr("&Files"));
  files_->setObjectName(QStringLiteral("classic-files"));
  files_->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
  toolbar->addSeparator();
  toolbar->addAction(continue_);
  toolbar->addAction(cancel_);
  toolbar->addAction(files_);
  toolbar->addAction(refresh_);
  connect(files_, &QAction::triggered, this, [this] {
    if (!actionTarget()) { return; }
    const auto target=actionTarget();if(!target){return;}
    if(inlineDetails_&&mainTabs_->currentWidget()==torrentsPage_){
      const bool showing=detailTabs_->currentIndex()==1;suppressAutomaticDetails_=false;if(!showing){detailsRequestedFor_.reset();}
      QPointer<LibraryWindow> guard(this);detailTabs_->setCurrentIndex(1);if(guard){requestDetails(showing);}return;
    }
    suppressAutomaticDetails_=false;detailsRequestedFor_.reset();openTorrentDetails(*target,1);
  });
  connect(continue_, &QAction::triggered, this, [this] {
    const auto target = actionTarget();
    if (!busy_ && !closing_ && target && model_->canContinue(*target)) { emit continueRequested(*target); }
  });
  connect(cancel_, &QAction::triggered, this, [this] {
    suppressAutomaticDetails_ = true;QPointer<LibraryWindow> alive(this);cancelRecheckView();if(!alive){return;}
    emit cancelRequested();
  });
  connect(model_, &QAbstractItemModel::dataChanged, this, [this] {
    if (!updating_) { updateActions(); updateGeneral(); updateCounts(); }
  });
  connect(mainTabs_, &QTabWidget::tabCloseRequested, this, &LibraryWindow::closePage);
  connect(mainTabs_, &QTabWidget::currentChanged, this, [this] {
    QPointer<LibraryWindow> guard(this);detailsRequestedFor_.reset();suppressAutomaticDetails_=false;placeDetailTabs();if(!guard){return;}
    updateActions();
    if (guard && torrentContextActive()) { selectionChanged(); }
    if(guard){updateChatReadContext();}
  });
  for(auto* table:{incompleteTable_,completeTable_}){
    connect(table,&QTableView::doubleClicked,this,[this,table](const QModelIndex& index){
      if(busy_||closing_){return;}if(index.column()==33&&index.data(RatingGraphicRole).toMap().value(QStringLiteral("enabled"),true).toBool()){activate(table);const auto source=proxy(table)->mapToSource(index);if(const auto target=source.isValid()?model_->targetAt(source.row()):std::nullopt){openLocalRatingTarget(*target);}return;}if(!detailsOnDoubleClick_){setMessage(tr("The configured double-click action is not available yet"));return;}
      activate(table);const auto source=proxy(table)->mapToSource(index);const auto target=source.isValid()?model_->targetAt(source.row()):std::nullopt;
      if(target){openTorrentDetails(*target);}
    });
  }
  updateActions();
  updateGeneral();
  updateCounts();
  setMessage(tr("Opening library…"));
}

void LibraryWindow::showMyTorrents() {
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  if (mainTabs_->indexOf(torrentsPage_) < 0) {
    mainTabs_->insertTab(0, torrentsPage_, tr("My Torrents"));
    if (!guard) { return; }
  }
  mainTabs_->setCurrentWidget(torrentsPage_);
  if (!guard) { return; }
  activeTable_->setFocus(Qt::ShortcutFocusReason);
  if (guard) { selectionChanged(); }
}
void LibraryWindow::showMyTracker() {
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  const bool fresh = !hostingPage_;
  if (fresh) {
    hostingPage_ = new SavedHostingPage(mainTabs_);installPublicationControls(PublicationKind::Tracker,hostingPage_);
    mainTabs_->addTab(hostingPage_.data(), tr("My Tracker"));
    if (!guard) { return; }
  }
  mainTabs_->setCurrentWidget(hostingPage_.data());
  if (!guard) { return; }
  hostingPage_->table().setFocus(Qt::ShortcutFocusReason);
  if (guard && fresh) { if(columnSession_){refreshPublicationViews();}else{refreshSavedServices();} }
}
void LibraryWindow::showMyShares() {
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  const bool fresh = !sharesPage_;
  if (fresh) {
    sharesPage_ = new SavedSharesPage(mainTabs_);installPublicationControls(PublicationKind::Shares,sharesPage_);
    mainTabs_->addTab(sharesPage_.data(), tr("My Shares"));
    if (!guard) { return; }
  }
  mainTabs_->setCurrentWidget(sharesPage_.data());
  if (!guard) { return; }
  sharesPage_->table().setFocus(Qt::ShortcutFocusReason);
  if (guard && fresh) { if(columnSession_){refreshPublicationViews();}else{refreshSavedServices();} }
}
#include "download_history_window.inc"
#include "rss_sources_window.inc"
#include "publication_window.inc"
#include "recheck_window.inc"
#include "metasearch_window.inc"
#include "metasearch_magnet_window.inc"
#include "subscriptions_window.inc"
StatisticsPage* LibraryWindow::statisticsPage() const { return statisticsPage_.data(); }
bool LibraryWindow::onStatisticsPage() const {
  return statisticsPage_ && mainTabs_->currentWidget() == statisticsPage_.data();
}
void LibraryWindow::showStatistics() {
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  const bool fresh = !statisticsPage_;
  if (fresh) {
    const auto navigation = columnSession_ != 0 && rememberedStatisticsNavigation_
        && rememberedStatisticsNavigation_->session == columnSession_
        ? std::optional<StatisticsNavigation>{rememberedStatisticsNavigation_->navigation} : std::nullopt;
    statisticsPage_ = new StatisticsPage(mainTabs_, navigation);
    requestedHistoryRevisions_.fill(0);
    connect(statisticsPage_.data(), &StatisticsPage::historyVisible, this, &LibraryWindow::queueLiveHistoryRefresh);
    connect(statisticsPage_.data(), &StatisticsPage::refreshRequested, this, &LibraryWindow::refreshStatistics);
    const QPointer<StatisticsPage> markPage = statisticsPage_;
    connect(markPage, &StatisticsPage::manualTagsVisible, this, &LibraryWindow::queueManualTagStatistics);
    connect(markPage, &StatisticsPage::countryVisible, this, [this, markPage](bool activate) {
      if (closing_ || !markPage || markPage != statisticsPage_ || !columnSession_) { return; }
      if (activate) { pendingCountryRead_ = markPage; }
      queueCountryStatistics();
    });
    connect(markPage, &StatisticsPage::manualTagStatisticsRequested, this, [this, markPage](quint64 session) {
      if (closing_ || !markPage || markPage != statisticsPage_ || !session || session != columnSession_) { return; }
      pendingManualTagRead_ = markPage;
      queueManualTagStatistics();
    });
    connect(markPage, &StatisticsPage::markRequested, this, [this, markPage](bool clear) {
      if (markPage && markPage == statisticsPage_ && !busy_ && !closing_ && columnSession_) {
        emit markRequested(columnSession_, clear);
      }
    });
    connect(statisticsPage_.data(), &StatisticsPage::historyRequested, this, &LibraryWindow::refreshHistory);
    connect(statisticsPage_.data(), &StatisticsPage::historyWeekRequested, this, &LibraryWindow::refreshHistoryWeek);
    connect(statisticsPage_.data(), &StatisticsPage::historyMonthRequested, this, &LibraryWindow::refreshHistoryMonth);
    connect(statisticsPage_.data(), &StatisticsPage::historyRangeRequested, this, &LibraryWindow::refreshHistoryRange);
    const QPointer<StatisticsPage> page = statisticsPage_;
    const auto session = columnSession_;
    page->beginNativeSession(session);
    if (!guard || !page || statisticsPage_ != page || columnSession_ != session) { return; }
    if(latestI2p_){page->setI2p(latestI2p_);}
    if (!guard || !page || statisticsPage_ != page || columnSession_ != session) { return; }
    page->resetHistorySession(columnSession_);
    if (!guard || !page || statisticsPage_ != page) { return; }
    mainTabs_->addTab(statisticsPage_.data(), tr("Statistics"));
    if (!guard) { return; }
  }
  const QPointer<StatisticsPage> page = statisticsPage_;
  mainTabs_->setCurrentWidget(page.data());
  if (!guard || !page || statisticsPage_ != page) { return; }
  page->tabs().setFocus(Qt::ShortcutFocusReason);
  if (guard && page && statisticsPage_ == page && fresh) {
    refreshStatistics();
    if (guard && page && statisticsPage_ == page) {
      page->setAccountingBusy(busy_);
      if (guard && page && statisticsPage_ == page && latestOverall_) { page->setOverall(latestOverall_); }
      if (guard && page && statisticsPage_ == page && latestRuntime_) { page->setRuntimeMetrics(latestRuntime_); }
      if (guard && page && statisticsPage_ == page && latestFileMerge_&&latestFileMerge_->session==columnSession_) { page->setFileMerge(latestFileMerge_); }
      if (guard && page && statisticsPage_ == page && latestAzureus_) { page->setAzureusStatistics(latestAzureus_); }
      if (guard && page && statisticsPage_ == page && latestLiveSpeed_) { page->setLiveSpeedManager(latestLiveSpeed_); }
      if (guard && page && statisticsPage_ == page && latestManualTags_) { page->setManualTagStatistics(latestManualTags_); }
      if (guard && page && statisticsPage_ == page && latestCountry_) { page->setCountryStatistics(latestCountry_); }
    }
    if (guard && page && statisticsPage_ == page && latestNativeActivity_) {
      page->setNativeActivity(latestNativeActivity_);
    }
    if (guard && page && statisticsPage_ == page && engineActivity_) {
      page->setEngineActivity(engineActivity_->snapshot());
    }
  }
}
void LibraryWindow::refreshStatistics() {
  if (closing_ || !statisticsPage_ || columnSession_ == 0) { return; }
  if (statisticsRequest_ == std::numeric_limits<quint64>::max()) {
    setMessage(tr("Statistics request counter exhausted"));
    return;
  }
  const StatisticsRequest request{columnSession_, ++statisticsRequest_, statisticsPage_};
  QPointer<LibraryWindow> guard(this);
  request.page->beginRequest(request.session, request.request);
  if (!guard || !request.page || statisticsPage_ != request.page
      || statisticsRequest_ != request.request || columnSession_ != request.session) { return; }
  pendingStatisticsRequest_ = request;
  queueStatistics();
}
void LibraryWindow::refreshHistory(core::HistoryDate date) {
  if (closing_ || !statisticsPage_ || columnSession_ == 0 || !core::validHistoryDate(date)) { return; }
  if (historyRequest_ == std::numeric_limits<quint64>::max()) {
    setMessage(tr("History request counter exhausted"));
    return;
  }
  requestedHistoryRevisions_[0] = savedHistoryRevision_;
  const StatisticsRequest request{columnSession_, ++historyRequest_, statisticsPage_, date};
  QPointer<LibraryWindow> guard(this);
  request.page->beginHistoryRequest(request.session, request.request, date);
  if (!guard || !request.page || statisticsPage_ != request.page
      || historyRequest_ != request.request || columnSession_ != request.session) { return; }
  pendingHistoryRequest_ = request;
  queueStatistics();
}
void LibraryWindow::refreshHistoryWeek(core::HistoryDate date) {
  if (closing_ || !statisticsPage_ || columnSession_ == 0 || !core::savedHistoryWeekDates(date)) { return; }
  if (historyRequest_ == std::numeric_limits<quint64>::max()) {
    setMessage(tr("History request counter exhausted"));
    return;
  }
  requestedHistoryRevisions_[1] = savedHistoryRevision_;
  const StatisticsRequest request{columnSession_, ++historyRequest_, statisticsPage_, date, true};
  QPointer<LibraryWindow> guard(this);
  request.page->beginHistoryWeekRequest(request.session, request.request, date);
  if (!guard || !request.page || statisticsPage_ != request.page
      || historyRequest_ != request.request || columnSession_ != request.session) { return; }
  pendingHistoryRequest_ = request;
  queueStatistics();
}
void LibraryWindow::refreshHistoryMonth(core::HistoryDate date) {
  if (closing_ || !statisticsPage_ || columnSession_ == 0 || !core::savedHistoryMonthDates(date)) { return; }
  if (historyRequest_ == std::numeric_limits<quint64>::max()) {
    setMessage(tr("History request counter exhausted"));
    return;
  }
  requestedHistoryRevisions_[2] = savedHistoryRevision_;
  const StatisticsRequest request{columnSession_, ++historyRequest_, statisticsPage_, date, false, true};
  QPointer<LibraryWindow> guard(this);
  request.page->beginHistoryMonthRequest(request.session, request.request, date);
  if (!guard || !request.page || statisticsPage_ != request.page
      || historyRequest_ != request.request || columnSession_ != request.session) { return; }
  pendingHistoryRequest_ = request;
  queueStatistics();
}
void LibraryWindow::refreshHistoryRange(core::HistoryRangeRequest query) {
  if (closing_ || !statisticsPage_ || columnSession_ == 0 || !core::planSavedHistoryRange(query)) { return; }
  if (historyRequest_ == std::numeric_limits<quint64>::max()) {
    setMessage(tr("History request counter exhausted")); return;
  }
  requestedHistoryRevisions_[3] = savedHistoryRevision_;
  const StatisticsRequest request{columnSession_, ++historyRequest_, statisticsPage_, {}, false, false, query};
  QPointer<LibraryWindow> guard(this);
  request.page->beginHistoryRangeRequest(request.session, request.request, query);
  if (!guard || !request.page || statisticsPage_ != request.page
      || historyRequest_ != request.request || columnSession_ != request.session) { return; }
  pendingHistoryRequest_ = request; queueStatistics();
}
void LibraryWindow::setHistoryRevision(quint64 session, quint64 revision) {
  if (closing_ || !session || session != columnSession_ || !revision || revision <= savedHistoryRevision_) { return; }
  savedHistoryRevision_ = revision;
  queueLiveHistoryRefresh();
}
void LibraryWindow::queueLiveHistoryRefresh() {
  if (liveHistoryRefreshQueued_ || closing_ || !savedHistoryRevision_ || !statisticsPage_) { return; }
  liveHistoryRefreshQueued_ = true;
  QTimer::singleShot(0, this, [this] {
    liveHistoryRefreshQueued_ = false;
    if (closing_ || busy_ || activeSavedRequest_ || activeColumnRequest_ || activeColumnSave_
        || activeStatisticsRequest_ || !statisticsPage_ || !statisticsPage_->isVisible() || isMinimized()
        || (latestNativeSession_ && latestNativeSession_->state == NativeSessionState::Closed)) { return; }
    const QPointer<StatisticsPage> page = statisticsPage_;
    const auto kind = page->historyRangeSelected() ? 3 : page->historyMonthSelected() ? 2
        : page->historyWeekSelected() ? 1 : page->historySelected() ? 0 : -1;
    if (kind < 0 || requestedHistoryRevisions_[static_cast<std::size_t>(kind)] >= savedHistoryRevision_) { return; }
    // Mark before callbacks. The existing request channel retains only its latest
    // pending query and rejects stale generations; failures do not create a poll.
    requestedHistoryRevisions_[static_cast<std::size_t>(kind)] = savedHistoryRevision_;
    if (kind == 3) { page->refreshHistoryRange(); }
    else if (kind == 2) { refreshHistoryMonth(page->selectedHistoryMonthDate()); }
    else if (kind == 1) { refreshHistoryWeek(page->selectedHistoryWeekDate()); }
    else { refreshHistory(page->selectedHistoryDate()); }
  });
}
void LibraryWindow::queueStatistics() {
  if (statisticsDispatchQueued_ || closing_ || busy_ || activeSavedRequest_
      || activeColumnRequest_ || activeColumnSave_ || activeStatisticsRequest_
      || (!pendingStatisticsRequest_ && !pendingHistoryRequest_)) { return; }
  statisticsDispatchQueued_ = true;
  QTimer::singleShot(0, this, [this] {
    statisticsDispatchQueued_ = false;
    if (closing_ || busy_ || activeSavedRequest_ || activeColumnRequest_ || activeColumnSave_
        || activeStatisticsRequest_ || (!pendingStatisticsRequest_ && !pendingHistoryRequest_)) { return; }
    // Saved totals have independent pending work. History modes coalesce by
    // latest query, sharing the same active optional worker slot.
    const bool history = !pendingStatisticsRequest_;
    const auto request = history ? *pendingHistoryRequest_ : *pendingStatisticsRequest_;
    if (history) { pendingHistoryRequest_.reset(); }
    else { pendingStatisticsRequest_.reset(); }
    if (!request.page || request.page != statisticsPage_ || request.session != columnSession_
        || request.request != (history ? historyRequest_ : statisticsRequest_)) {
      queueOptionalViews();
      return;
    }
    activeStatisticsRequest_ = request;
    QPointer<LibraryWindow> guard(this);
    if (request.range) { emit historyRangeRequested(request.session, request.request, *request.range); }
    else if (request.historyMonth) { emit historyMonthRequested(request.session, request.request, *request.historyDate); }
    else if (request.historyWeek) { emit historyWeekRequested(request.session, request.request, *request.historyDate); }
    else if (request.historyDate) { emit historyRequested(request.session, request.request, *request.historyDate); }
    else { emit statisticsRequested(request.session, request.request); }
    if (!guard) { return; }
    if (!busy_ && activeStatisticsRequest_ && activeStatisticsRequest_->session == request.session
        && activeStatisticsRequest_->request == request.request
        && activeStatisticsRequest_->page == request.page
        && activeStatisticsRequest_->historyDate == request.historyDate
        && activeStatisticsRequest_->historyWeek == request.historyWeek
        && activeStatisticsRequest_->historyMonth == request.historyMonth
        && activeStatisticsRequest_->range == request.range) {
      activeStatisticsRequest_.reset();
      if (request.page && request.page == statisticsPage_) {
        if (request.range) {
          request.page->setHistoryRangeFailed(request.session, request.request, *request.range, tr("Rolling history request was not accepted"));
        } else if (request.historyMonth) {
          request.page->setHistoryMonthFailed(request.session, request.request, *request.historyDate, tr("30-day history request was not accepted"));
        } else if (request.historyWeek) {
          request.page->setHistoryWeekFailed(request.session, request.request, *request.historyDate, tr("Seven-day history request was not accepted"));
        } else if (request.historyDate) {
          request.page->setHistoryFailed(request.session, request.request, *request.historyDate, tr("History request was not accepted"));
        } else {
          request.page->setFailed(request.session, request.request, tr("Statistics request was not accepted"));
        }
      }
      if (guard) { queueOptionalViews(); }
    }
  });
}
void LibraryWindow::setStatistics(StatisticsPresentation packet) {
  if (!packet || !activeStatisticsRequest_ || activeStatisticsRequest_->range || activeStatisticsRequest_->historyDate
      || activeStatisticsRequest_->historyWeek || activeStatisticsRequest_->historyMonth
      || packet->session != activeStatisticsRequest_->session
      || packet->request != activeStatisticsRequest_->request) { return; }
  const auto request = *activeStatisticsRequest_;
  activeStatisticsRequest_.reset();
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  if (request.page && request.page == statisticsPage_ && packet->session == columnSession_) {
    request.page->setPresentation(std::move(packet));
  }
  if (guard) { queueOptionalViews(); }
}
void LibraryWindow::setHistory(HistoryPresentation packet) {
  if (!packet || !activeStatisticsRequest_ || activeStatisticsRequest_->range || !activeStatisticsRequest_->historyDate
      || activeStatisticsRequest_->historyWeek || activeStatisticsRequest_->historyMonth
      || packet->session != activeStatisticsRequest_->session
      || packet->request != activeStatisticsRequest_->request
      || packet->date != *activeStatisticsRequest_->historyDate) { return; }
  const auto request = *activeStatisticsRequest_;
  activeStatisticsRequest_.reset();
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  if (request.page && request.page == statisticsPage_ && packet->session == columnSession_) {
    request.page->setHistory(std::move(packet));
  }
  if (guard) { queueOptionalViews(); }
}
void LibraryWindow::setHistoryWeek(HistoryWeekPresentation packet) {
  if (!packet || !activeStatisticsRequest_ || activeStatisticsRequest_->range || !activeStatisticsRequest_->historyWeek
      || activeStatisticsRequest_->historyMonth
      || !activeStatisticsRequest_->historyDate || packet->session != activeStatisticsRequest_->session
      || packet->request != activeStatisticsRequest_->request
      || packet->endingDate != *activeStatisticsRequest_->historyDate) { return; }
  const auto request = *activeStatisticsRequest_;
  activeStatisticsRequest_.reset();
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  if (request.page && request.page == statisticsPage_ && packet->session == columnSession_) {
    request.page->setHistoryWeek(std::move(packet));
  }
  if (guard) { queueOptionalViews(); }
}
void LibraryWindow::setHistoryMonth(HistoryMonthPresentation packet) {
  if (!packet || !activeStatisticsRequest_ || activeStatisticsRequest_->range || !activeStatisticsRequest_->historyMonth
      || activeStatisticsRequest_->historyWeek
      || !activeStatisticsRequest_->historyDate || packet->session != activeStatisticsRequest_->session
      || packet->request != activeStatisticsRequest_->request
      || packet->endingDate != *activeStatisticsRequest_->historyDate) { return; }
  const auto request = *activeStatisticsRequest_;
  activeStatisticsRequest_.reset();
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  if (request.page && request.page == statisticsPage_ && packet->session == columnSession_) {
    request.page->setHistoryMonth(std::move(packet));
  }
  if (guard) { queueOptionalViews(); }
}
void LibraryWindow::setHistoryRange(HistoryRangePresentation packet) {
  if (!packet || !activeStatisticsRequest_ || !activeStatisticsRequest_->range
      || activeStatisticsRequest_->historyDate || activeStatisticsRequest_->historyWeek || activeStatisticsRequest_->historyMonth
      || packet->session != activeStatisticsRequest_->session || packet->request != activeStatisticsRequest_->request
      || packet->query != *activeStatisticsRequest_->range) { return; }
  const auto request = *activeStatisticsRequest_; activeStatisticsRequest_.reset();
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  if (request.page && request.page == statisticsPage_ && packet->session == columnSession_) {
    request.page->setHistoryRange(std::move(packet));
  }
  if (guard) { queueOptionalViews(); }
}
void LibraryWindow::setLiveSpeedManager(LiveSpeedManagerPresentation packet) {
  if (closing_ || !acceptLiveSpeedManager(latestLiveSpeed_, packet, columnSession_)) { return; }
  latestLiveSpeed_ = std::move(packet);
  if (statisticsPage_) { statisticsPage_->setLiveSpeedManager(latestLiveSpeed_); }
}
LiveSpeedManagerPresentation LibraryWindow::liveSpeedManagerSnapshot() const { return latestLiveSpeed_; }
void LibraryWindow::setAzureusStatistics(AzureusStatisticsPresentation packet) {
  if (closing_ || !acceptAzureusStatistics(latestAzureus_, packet, columnSession_)) { return; }
  latestAzureus_ = std::move(packet);
  if (statisticsPage_) { statisticsPage_->setAzureusStatistics(latestAzureus_); }
}
void LibraryWindow::setRuntimeMetrics(RuntimeMetricsPresentation packet) {
  if (!packet || packet->session != columnSession_ || !packet->epoch || !packet->sequence
      || (latestRuntime_ && (packet->epoch < latestRuntime_->epoch
          || (packet->epoch == latestRuntime_->epoch && packet->sequence <= latestRuntime_->sequence)))) { return; }
  latestRuntime_ = std::move(packet);
  if(classicStatus_){classicStatus_->setRuntimeSnapshot(latestRuntime_);}
  if (statisticsPage_) { statisticsPage_->setRuntimeMetrics(latestRuntime_); }
}
void LibraryWindow::setManualTagStatistics(ManualTagStatisticsPresentation packet) {
  if (closing_ || !acceptManualTagStatistics(latestManualTags_, packet, columnSession_)) { return; }
  latestManualTags_ = std::move(packet);
  if (statisticsPage_) { statisticsPage_->setManualTagStatistics(latestManualTags_); }
}
void LibraryWindow::setCountryStatistics(CountryStatisticsPresentation packet) {
  if (closing_ || !acceptCountryStatistics(latestCountry_, packet, columnSession_)) { return; }
  latestCountry_ = std::move(packet);
  if (statisticsPage_) { statisticsPage_->setCountryStatistics(latestCountry_); }
}
void LibraryWindow::setOverall(OverallPresentation packet) {
  if (!packet || packet->session != columnSession_ || !packet->sequence
      || (latestOverall_ && (packet->sequence <= latestOverall_->sequence || latestOverall_->closed))) { return; }
  latestOverall_ = std::move(packet);
  if(classicStatus_){classicStatus_->setOverall(latestOverall_->values[static_cast<std::size_t>(core::SavedStatistic::Downloaded)],latestOverall_->values[static_cast<std::size_t>(core::SavedStatistic::Uploaded)],latestOverall_->available);}
  if (statisticsPage_) { statisticsPage_->setOverall(latestOverall_); }
}
void LibraryWindow::setI2p(I2pPresentation packet) {
  if(closing_||!acceptI2p(latestI2p_,packet,columnSession_)){return;}latestI2p_=std::move(packet);
  if(statisticsPage_){statisticsPage_->setI2p(latestI2p_);}
}
void LibraryWindow::setNativeSession(NativeSessionPresentation packet) {
  if (closing_ || !packet || packet->applicationRun < nativeApplicationRun_
      || !acceptNativeSession(latestNativeSession_, packet, columnSession_)) { return; }
  // One GUI-owned history across operations and optional widget lifetimes.
  // It consumes only the independent absolute native-session stream.
  if (!nativeActivity_) { nativeActivity_ = std::make_shared<NativeActivityHistory>(); }
  if (!nativeActivity_->observe(packet, columnSession_)) { return; }
  auto frame = nativeActivity_->snapshot();
  nativeApplicationRun_ = packet->applicationRun;
  latestNativeSession_ = std::move(packet); latestNativeActivity_ = std::move(frame);
  if(classicStatus_){classicStatus_->setSessionSnapshot(latestNativeSession_);}
  const QPointer<StatisticsPage> page = statisticsPage_;
  if (page) { page->setNativeActivity(latestNativeActivity_); }
}
void LibraryWindow::offerRecoveryClose(quint64 session){
  if(!session||session!=columnSession_||busy_||closing_||!sessionCloseEnabled_){return;}
  const QPointer<LibraryWindow> guard(this);
  const auto answer=QMessageBox::warning(this,tr("Close without saving progress"),
      tr("The pending profile journal prevents normal checkpointing. Close the retained transfers without saving their latest piece map and transfer counters? Saved profile and journal files will be preserved; later startup may recheck downloaded data."),
      QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
  if(!guard||answer!=QMessageBox::Yes||session!=columnSession_||busy_||closing_){return;}
  closing_=true;sessionCloseRequested_=true;updateActions();if(!guard){return;}
  emit sessionCloseWithoutCheckpointRequested(session);
}
void LibraryWindow::completeSessionClose() {
  if (!closing_ || !sessionCloseRequested_ || sessionCloseAcknowledged_) { return; }
  sessionCloseAcknowledged_ = true;
  if (schedulerDialog_) { schedulerDialog_->invalidateSession(); }
  if (optionsDialog_) { optionsDialog_->invalidateSession(); }
  QTimer::singleShot(0, this, &QWidget::close);
}
void LibraryWindow::setEngineStatistics(EngineStatisticsPresentation packet) {
  if (closing_ || !acceptEngineStatistics(latestEngineStatistics_, packet, columnSession_)) { return; }
  const bool fresh = !latestEngineStatistics_ || packet->operation > latestEngineStatistics_->operation;
  if (fresh && (packet->state != EngineStatisticsState::Preparing || !model_->accepts(packet->target))) { return; }
  if (packet->state == EngineStatisticsState::Ended && !packet->sample
      && latestEngineStatistics_ && latestEngineStatistics_->sample
      && latestEngineStatistics_->session == packet->session
      && latestEngineStatistics_->operation == packet->operation
      && latestEngineStatistics_->target == packet->target) {
    auto retained = std::make_shared<EngineStatisticsPacket>(*packet);
    retained->sample = latestEngineStatistics_->sample;
    packet = std::move(retained);
  }
  // Collect on the GUI owner independently of the optional page. The bounded
  // snapshot owns its points; painting never reads mutable Window state.
  if (!engineActivity_) { engineActivity_ = std::make_shared<EngineActivityHistory>(); }
  if (!engineActivity_->observe(packet, columnSession_)) { return; }
  latestEngineStatistics_ = packet;
  if (statisticsPage_) { statisticsPage_->setEngineActivity(engineActivity_->snapshot()); }
}
void LibraryWindow::queueOptionalViews() {
  queueSavedServices();
  queueColumnLayouts();
  queueStatistics();
  queueLiveHistoryRefresh();
  queueManualTagStatistics();
  queueCountryStatistics();
}
void LibraryWindow::queueManualTagStatistics() {
  if (manualTagDispatchQueued_ || !pendingManualTagRead_) { return; }
  manualTagDispatchQueued_ = true;
  QMetaObject::invokeMethod(this, [this] {
    manualTagDispatchQueued_ = false;
    const QPointer<StatisticsPage> page = pendingManualTagRead_;
    if (!page || page != statisticsPage_ || !columnSession_ || closing_) {
      pendingManualTagRead_.clear(); return;
    }
    // One explicit read intent survives busy/hidden deferral, never a poll.
    if (busy_ || !onStatisticsPage() || !page->isVisible() || isMinimized() || !page->manualTagsSelected()) { return; }
    pendingManualTagRead_.clear();
    emit manualTagStatisticsRequested(columnSession_); // Final access; receiver may delete/rebind this window.
  }, Qt::QueuedConnection);
}
void LibraryWindow::queueCountryStatistics() {
  if (countryDispatchQueued_ || !pendingCountryRead_) { return; }
  countryDispatchQueued_ = true;
  QMetaObject::invokeMethod(this, [this] {
    countryDispatchQueued_ = false;
    const QPointer<StatisticsPage> page = pendingCountryRead_;
    if (!page || page != statisticsPage_ || !columnSession_ || closing_) {
      pendingCountryRead_.clear(); return;
    }
    if (busy_ || !onStatisticsPage() || !page->isVisible() || isMinimized() || !page->countrySelected()) { return; }
    pendingCountryRead_.clear();
    emit countryStatisticsRequested(columnSession_); // Final access: receiver may delete/rebind this window.
  }, Qt::QueuedConnection);
}
void LibraryWindow::closePage(int index) {
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  QPointer<QWidget> page(mainTabs_->widget(index));
  if (!page) { return; }
  const bool retainedLibrary = page == torrentsPage_;
  const bool detailPage=!page->property("BiglyBTC.torrentDetailsHash").toString().isEmpty();
  if(detailPage&&detailTabs_->parentWidget()==page){
    detailTabs_->hide();if(!guard||!page){return;}
    detailTabs_->setParent(torrentsPage_);if(!guard||!page){return;}
  }
  if (!retainedLibrary) {
    if (page == hostingPage_.data()) { if(trackerControls_){trackerControls_->invalidate();if(!guard||!page){return;}}trackerControls_.clear();hostingPage_.clear(); }
    else if (page == sharesPage_.data()) { if(sharingControls_){sharingControls_->invalidate();if(!guard||!page){return;}}sharingControls_.clear();sharesPage_.clear(); }
    else if(page==subscriptionsPage_.data()){const auto old=subscriptionsPage_;subscriptionsPage_.clear();old->invalidateSession();if(!guard||!page){return;}}
    else if(page==metasearchPage_.data()){pendingMetasearchReview_.reset();const auto old=metasearchPage_;metasearchPage_.clear();old->invalidateSession();if(!guard||!page){return;}if(metasearchReview_){metasearchReview_.reset();if(intakeDialog_){intakeDialog_->invalidateSession();if(!guard||!page){return;}}}}
    else if(page==rssPage_.data()){const auto old=rssPage_;rssPage_.clear();old->invalidateSession();if(!guard||!page){return;}}
    else if(page==downloadHistoryPage_.data()){downloadHistoryPage_->invalidateSession();downloadHistoryPage_.clear();pendingDownloadHistoryCommand_.reset();}
    else if (page == statisticsPage_.data()) {
      const QPointer<StatisticsPage> statisticsPage = statisticsPage_;
      const auto session = columnSession_;
      const auto navigation = statisticsPage->navigation();
      if (!guard || !page || statisticsPage_ != statisticsPage || columnSession_ != session) { return; }
      if (session != 0 && navigation) {
        rememberedStatisticsNavigation_ = StatisticsNavigationRecord{session, *navigation};
      } else { rememberedStatisticsNavigation_.reset(); }
      statisticsPage_.clear(); pendingStatisticsRequest_.reset(); pendingHistoryRequest_.reset(); pendingManualTagRead_.clear(); pendingCountryRead_.clear();
    }
    else if(page==peersPage_.data()){peersPage_->invalidateSession();peersPage_.clear();updatePeersVisibility();}
    else if(page==chatPage_.data()){chatPage_.clear();}
    else if(!detailPage) { return; }
  }
  mainTabs_->removeTab(mainTabs_->indexOf(page));
  if (!guard) { return; }
  // Keep the library widgets and selection owned for View > My Torrents.
  if (!retainedLibrary && page) { page->deleteLater(); }
  if (pendingSavedRequest_ && !hasLoadingServices(*pendingSavedRequest_)) { pendingSavedRequest_.reset(); }
  updateActions();
  if (guard) { queueOptionalViews(); }
}
bool LibraryWindow::onSavedServicePage() const {
  const auto* current = mainTabs_->currentWidget();
  return (hostingPage_ && current == hostingPage_.data()) || (sharesPage_ && current == sharesPage_.data());
}
bool LibraryWindow::hasLoadingServices(quint64 request) const {
  return (hostingPage_ && hostingPage_->generation() == request && hostingPage_->state() == SavedPageState::Loading)
      || (sharesPage_ && sharesPage_->generation() == request && sharesPage_->state() == SavedPageState::Loading);
}
void LibraryWindow::refreshSavedServices() {
  if (closing_ || (!hostingPage_ && !sharesPage_)) { return; }
  if (savedRequest_ == std::numeric_limits<quint64>::max()) {
    setMessage(tr("Saved-view request counter exhausted"));
    return;
  }
  const auto request = ++savedRequest_;
  QPointer<LibraryWindow> guard(this);
  if (hostingPage_) { hostingPage_->beginRequest(request); }
  if (!guard || savedRequest_ != request) { return; }
  if (sharesPage_) { sharesPage_->beginRequest(request); }
  if (!guard || savedRequest_ != request) { return; }
  pendingSavedRequest_ = request;
  queueSavedServices();
}
void LibraryWindow::queueSavedServices() {
  if (savedDispatchQueued_ || closing_ || busy_ || activeSavedRequest_ || activeColumnRequest_ || activeColumnSave_ || activeStatisticsRequest_ || !pendingSavedRequest_) { return; }
  savedDispatchQueued_ = true;
  QTimer::singleShot(0, this, [this] {
    savedDispatchQueued_ = false;
    if (closing_ || busy_ || activeSavedRequest_ || activeColumnRequest_ || activeColumnSave_ || activeStatisticsRequest_ || !pendingSavedRequest_) { return; }
    const auto request = *pendingSavedRequest_;
    pendingSavedRequest_.reset();
    if (!hasLoadingServices(request)) { return; }
    activeSavedRequest_ = request;
    QPointer<LibraryWindow> guard(this);
    emit savedServicesRequested(request);
    if (!guard) { return; }
    if (!busy_ && activeSavedRequest_ == request) {
      activeSavedRequest_.reset();
      failSavedServices(request, tr("Saved configuration request was not accepted"));
      if (guard) { queueOptionalViews(); }
    }
  });
}
void LibraryWindow::failSavedServices(quint64 request, const QString& message) {
  QPointer<LibraryWindow> guard(this);
  if (hostingPage_) { hostingPage_->setFailed(request, message); }
  if (!guard) { return; }
  if (sharesPage_) { sharesPage_->setFailed(request, message); }
}
void LibraryWindow::setSavedServices(SavedServicesPresentation packet) {
  if (!packet || !activeSavedRequest_ || packet->request != *activeSavedRequest_) { return; }
  activeSavedRequest_.reset();
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  if (hostingPage_&&(!trackerControls_||!columnSession_)) { applySavedResult(*hostingPage_, packet->request, packet->hosting); }
  if (!guard) { return; }
  if (sharesPage_&&(!sharingControls_||!columnSession_)) { applySavedResult(*sharesPage_, packet->request, packet->sharing); }
  if (!guard) { return; }
  queueOptionalViews();
}

void LibraryWindow::beginColumnLayoutSession(quint64 session) {
  if (closing_ || session == 0 || session <= columnSession_) { return; }
  QPointer<LibraryWindow> guard(this);
  if(peersPage_){closePage(mainTabs_->indexOf(peersPage_));if(!guard){return;}}
  const auto sessionBeforeDetails=columnSession_;
  for(int i=mainTabs_->count()-1;i>=0;--i){auto* page=mainTabs_->widget(i);if(page&&!page->property("BiglyBTC.torrentDetailsHash").toString().isEmpty()){closePage(i);if(!guard||columnSession_!=sessionBeforeDetails){return;}}}
  const auto previousSession=columnSession_;cancelRecheckView();if(!guard||columnSession_!=previousSession){return;}if(recheckDialog_){recheckDialog_->invalidate();if(!guard||columnSession_!=previousSession){return;}}activeRecheck_.reset();
  columnSession_ = session;
  torrentOptionsPending_=0;torrentTrackerPending_=0;latestTorrentDetail_.reset();detailFilesLayoutSession_=0;
  if(classicStatus_){classicStatus_->beginSession(session);}pendingMetasearchReview_.reset();invalidateMetasearchMagnetReview();if(!guard){return;}
  for(const auto& controls:{sharingControls_,trackerControls_}){if(controls){controls->beginSession(session,controls->page()?controls->page():reserveRssUiIdentity());if(!guard||columnSession_!=session){return;}}}
  if(publicationReview_){publicationReview_->invalidate();if(!guard){return;}}
  if(subscriptionsPage_){const auto id=subscriptionsPage_->page()?subscriptionsPage_->page():reserveRssUiIdentity();subscriptionsPage_->beginSession(session,id);if(!guard||columnSession_!=session){return;}}
  if(rssPage_){const auto id=rssPage_->page()?rssPage_->page():reserveRssUiIdentity();rssPage_->beginSession(session,id);if(!guard||columnSession_!=session){return;}}
  if(metasearchPage_){const auto id=metasearchPage_->page()?metasearchPage_->page():reserveRssUiIdentity();metasearchPage_->beginSession(session,id);if(!guard||columnSession_!=session){return;}}
  pendingDownloadHistoryCommand_.reset();if(downloadHistoryPage_){downloadHistoryPage_->beginSession(session);if(!guard){return;}}
  latestI2p_.reset();
  latestManualTagEditor_.reset();pendingManualTagCommand_.reset();
  latestCategoryEditor_.reset();pendingCategoryCommand_.reset();
  if(categoryDialog_){const auto previous=categoryDialog_;categoryDialog_.clear();previous->invalidateSession();if(!guard){return;}}
  if(manualTagDialog_){const auto previous=manualTagDialog_;manualTagDialog_.clear();previous->invalidateSession();if(!guard){return;}}
  localRatingIdentity_.reset();localRatingLoading_=false;localRatingSaving_=false;
  if(localRatingDialog_){localRatingDialog_->reject();if(!guard){return;}}
  if(fileOperationSequence_){fileOperationSequence_->invalidateSession();if(!guard){return;}}
  activeFileOperation_.reset();activeTorrentCreation_.reset();
  if(fileOperationDialog_){const auto dialog=fileOperationDialog_;dialog->invalidateSession();if(!guard){return;}if(dialog){dialog->close();}if(!guard){return;}}
  if(torrentCreationDialog_){const auto dialog=torrentCreationDialog_;dialog->invalidateSession();if(!guard){return;}if(dialog){dialog->close();}if(!guard){return;}}
  activeIntakePreview_.reset();activeIntakeSubmission_.reset();
  if(intakeDialog_){intakeDialog_->invalidateSession();if(!guard){return;}}
  defaultSaveDirectory_.clear();
  pendingSchedulerLoad_.reset(); activeSchedulerLoad_.reset(); activeSchedulerSave_.reset();
  if (schedulerDialog_) { schedulerDialog_->invalidateSession(); schedulerDialog_->close(); }
  pendingOptionsLoad_.reset(); activeOptionsLoad_.reset(); activeOptionsSave_.reset();
  if (optionsDialog_) { optionsDialog_->invalidateSession(); optionsDialog_->close(); }
  savedHistoryRevision_ = 0; requestedHistoryRevisions_.fill(0);
  latestAzureus_.reset(); latestLiveSpeed_.reset();
  latestOverall_.reset(); latestRuntime_.reset(); latestManualTags_.reset(); pendingManualTagRead_.clear(); latestCountry_.reset(); pendingCountryRead_.clear();
  latestNativeSession_.reset(); latestNativeActivity_.reset(); nativeActivity_.reset();
  rememberedStatisticsNavigation_.reset();
  latestEngineStatistics_.reset();
  engineActivity_.reset();
  activeStatisticsRequest_.reset();
  pendingStatisticsRequest_.reset();
  pendingHistoryRequest_.reset();
  columnSaveAvailable_ = false;
  activeColumnSave_.reset();
  activeColumnRequest_.reset();
  pendingColumnRequest_.reset();
  refreshColumnLayouts(true);
  if (!guard) { return; }
  const QPointer<StatisticsPage> page = statisticsPage_;
  if (page) {
    page->beginNativeSession(session);
    if (!guard || !page || statisticsPage_ != page || columnSession_ != session) { return; }
    // A binding callback may already have supplied the new session's service packet.
    if (latestAzureus_) { page->setAzureusStatistics(latestAzureus_); }
    if (!guard || !page || statisticsPage_ != page || columnSession_ != session) { return; }
    if (latestLiveSpeed_) { page->setLiveSpeedManager(latestLiveSpeed_); }
    if (!guard || !page || statisticsPage_ != page || columnSession_ != session) { return; }
    page->resetHistorySession(session);
    if (!guard || !page || statisticsPage_ != page || columnSession_ != session) { return; }
    refreshStatistics();
    if (!guard || !page || statisticsPage_ != page || columnSession_ != session) { return; }
    if (latestNativeActivity_) { page->setNativeActivity(latestNativeActivity_); }
    if (guard && page && statisticsPage_ == page && columnSession_ == session) {
      if (page->countrySelected()) { pendingCountryRead_ = page; queueCountryStatistics(); }
      else if (page->historyRangeSelected()) { page->refreshHistoryRange(); }
      else if (page->historyMonthSelected()) { refreshHistoryMonth(page->selectedHistoryMonthDate()); }
      else if (page->historyWeekSelected()) { refreshHistoryWeek(page->selectedHistoryWeekDate()); }
      else if (page->historySelected()) { refreshHistory(page->selectedHistoryDate()); }
    }
  }
}
void LibraryWindow::refreshColumnLayouts(bool initial) {
  if (closing_ || columnSession_ == 0) { return; }
  if (columnRequest_ == std::numeric_limits<quint64>::max()) {
    setMessage(tr("Column-layout request counter exhausted"));
    updateActions();
    return;
  }
  ColumnRequest request;
  request.session = columnSession_;
  request.request = ++columnRequest_;
  if (incompleteColumns_) {
    const auto revision = incompleteColumns_->editRevision();
    if (revision && (!initial || *revision == 0)) { request.incompleteRevision = revision; }
  }
  if (completeColumns_) {
    const auto revision = completeColumns_->editRevision();
    if (revision && (!initial || *revision == 0)) { request.completeRevision = revision; }
  }
  columnSaveAvailable_ = false;
  pendingColumnRequest_ = request;
  queueColumnLayouts();
  updateActions();
}
void LibraryWindow::queueColumnLayouts() {
  if (columnDispatchQueued_ || closing_ || busy_ || activeSavedRequest_
      || activeColumnRequest_ || activeColumnSave_ || activeStatisticsRequest_ || !pendingColumnRequest_) { return; }
  columnDispatchQueued_ = true;
  QTimer::singleShot(0, this, [this] {
    columnDispatchQueued_ = false;
    if (closing_ || busy_ || activeSavedRequest_ || activeColumnRequest_ || activeColumnSave_ || activeStatisticsRequest_ || !pendingColumnRequest_) { return; }
    const auto request = *pendingColumnRequest_;
    pendingColumnRequest_.reset();
    if (request.session != columnSession_ || request.request != columnRequest_) { return; }
    activeColumnRequest_ = request; // Bound before any direct signal/reentry.
    QPointer<LibraryWindow> guard(this);
    emit columnLayoutsRequested(request.session, request.request);
    if (!guard) { return; }
    // Accepted Bridge commands publish busy=true synchronously. A rejected or
    // unconnected request must not latch the slot or consume a newer request.
    if (!busy_ && activeColumnRequest_ && activeColumnRequest_->session == request.session
        && activeColumnRequest_->request == request.request) {
      activeColumnRequest_.reset();
      setMessage(tr("Saved column-layout request was not accepted"));
      if (!guard) { return; }
      queueOptionalViews();
    }
  });
}
void LibraryWindow::setColumnLayouts(ColumnLayoutsPresentation packet) {
  if (!packet || !activeColumnRequest_ || packet->session != activeColumnRequest_->session
      || packet->request != activeColumnRequest_->request) { return; }
  const auto request = *activeColumnRequest_;
  activeColumnRequest_.reset();
  if (closing_) { return; }
  if (packet->session != columnSession_ || packet->request != columnRequest_) {
    queueOptionalViews();
    return;
  }
  QPointer<LibraryWindow> guard(this);
  columnSaveAvailable_ = false;
  if (packet->state == ColumnLayoutReadState::Ready) {
    QString source;
    if (packet->source) {
      switch (*packet->source) {
        case ColumnLayoutReadSource::Primary: source = tr("primary"); break;
        case ColumnLayoutReadSource::Backup: source = tr("backup"); break;
        case ColumnLayoutReadSource::Saving: source = tr("saving"); break;
      }
    }
    if (source.isEmpty() || !packet->snapshot || !packet->snapshot->source
        || (packet->nativePreferences && !core::validateClassicPreferences(*packet->nativePreferences))) {
      setMessage(tr("Saved column-layout result is incomplete"));
    } else {
      columnSaveAvailable_ = packet->source == ColumnLayoutReadSource::Primary;
      if (packet->nativePreferences) { source = tr("BiglyBTC, %1").arg(source); }
      const auto current = [&] {
        return guard && !guard->closing_ && guard->columnSession_ == request.session
            && guard->columnRequest_ == request.request;
      };
      const bool previous = updating_;
      updating_ = true; // Header/proxy changes must not request hidden details.
      bool applied = false;
      if (incompleteColumns_ && request.incompleteRevision) {
        applied = packet->nativePreferences
            ? incompleteColumns_->applyPreferences(packet->nativePreferences->incomplete, *request.incompleteRevision, std::cref(current))
            : incompleteColumns_->applyLayout(packet->snapshot->incomplete, *request.incompleteRevision, std::cref(current));
      }
      if (!guard) { return; }
      if (!current()) {
        updating_ = previous;
        queueOptionalViews();
        return;
      }
      if (completeColumns_ && request.completeRevision) {
        applied = (packet->nativePreferences
            ? completeColumns_->applyPreferences(packet->nativePreferences->complete, *request.completeRevision, std::cref(current))
            : completeColumns_->applyLayout(packet->snapshot->complete, *request.completeRevision, std::cref(current))) || applied;
      }
      if (!guard) { return; }
      updating_ = previous;
      if (!current()) {
        queueOptionalViews();
        return;
      }
      setMessage(applied ? tr("Saved column layout applied (%1)").arg(source)
                         : tr("Saved column layout read (%1); current columns kept").arg(source));
    }
  } else if (packet->state == ColumnLayoutReadState::Missing) {
    // Only a complete absence result can admit first creation. A malformed
    // or recovery result must not be treated as an empty configuration.
    columnSaveAvailable_ = !packet->source && !packet->snapshot && !packet->nativePreferences && packet->failure.isEmpty();
    setMessage(columnSaveAvailable_ ? tr("No saved column layout found; Save creates one")
                                   : tr("Saved column-layout result is incomplete"));
  } else if (packet->state == ColumnLayoutReadState::Failed) {
    setMessage(packet->failure.isEmpty() ? tr("Saved column layout could not be read") : packet->failure);
  } else { setMessage(tr("Saved column-layout result is incomplete")); }
  if (!guard) { return; }
  queueOptionalViews();
}

void LibraryWindow::saveColumnLayout() {
  if (closing_ || busy_ || !columnSaveAvailable_ || columnSession_ == 0
      || activeColumnRequest_ || pendingColumnRequest_ || activeColumnSave_ || activeStatisticsRequest_
      || columnSaveRequest_ == std::numeric_limits<quint64>::max()
      || !incompleteColumns_ || !completeColumns_) { return; }
  const auto incompleteRevision = incompleteColumns_->editRevision();
  const auto completeRevision = completeColumns_->editRevision();
  const auto incomplete = incompleteColumns_->capturePreferences();
  const auto complete = completeColumns_->capturePreferences();
  if (!incompleteRevision || !completeRevision || !incomplete || !complete) {
    setMessage(tr("Current columns cannot be saved within the supported limits"));
    return;
  }
  auto preferences = std::make_shared<const core::ClassicPreferences>(core::ClassicPreferences{*incomplete, *complete});
  if (!core::validateClassicPreferences(*preferences)) {
    setMessage(tr("Current columns cannot be saved within the supported limits"));
    return;
  }
  const ColumnRequest request{columnSession_, ++columnSaveRequest_, incompleteRevision, completeRevision};
  auto capture = std::make_shared<ColumnLayoutSaveRequest>();
  capture->session = request.session;
  capture->request = request.request;
  capture->preferences = std::move(preferences);
  activeColumnSave_ = request;
  QPointer<LibraryWindow> guard(this);
  emit columnLayoutSaveRequested(std::move(capture));
  if (!guard) { return; }
  if (!busy_ && activeColumnSave_ && activeColumnSave_->session == request.session
      && activeColumnSave_->request == request.request) {
    activeColumnSave_.reset();
    setMessage(tr("Column-layout save was not accepted"));
  }
  if (guard) { queueOptionalViews(); updateActions(); }
}
void LibraryWindow::setColumnLayoutSaved(ColumnLayoutSavePresentation packet) {
  if (!packet || !activeColumnSave_ || packet->session != activeColumnSave_->session
      || packet->request != activeColumnSave_->request) { return; }
  const auto request = *activeColumnSave_;
  activeColumnSave_.reset();
  if (packet->session != columnSession_) { return; }
  columnSaveAvailable_ = packet->saved;
  QPointer<LibraryWindow> guard(this);
  if (packet->saved) {
    const bool same = incompleteColumns_ && completeColumns_
        && incompleteColumns_->editRevision() == request.incompleteRevision
        && completeColumns_->editRevision() == request.completeRevision;
    setMessage(same ? tr("BiglyBTC column layout saved")
                    : tr("Earlier column layout saved; newer column edits remain unsaved"));
  } else {
    setMessage(packet->failure.isEmpty() ? tr("Column layout was not saved; reload before retrying") : packet->failure);
  }
  if (guard) { queueOptionalViews(); updateActions(); }
}

void LibraryWindow::setOfflinePreview(bool offline) {
  offlinePreview_ = offline;
  if(classicStatus_){classicStatus_->setOffline(offline);}
  QPointer<LibraryWindow> guard(this);if(offline){cancelRecheckView();if(!guard){return;}}
  if(subscriptionsPage_){subscriptionsPage_->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard){return;}}
  if(rssPage_){rssPage_->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard){return;}}
  if(metasearchPage_){metasearchPage_->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard){return;}}
  for(const auto& controls:{sharingControls_,trackerControls_}){if(controls){controls->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard){return;}}}
  previewStatus_->setText(offline ? tr("Offline preview") : tr("Local test"));
  if (guard) { queueDownloadActions(); }
}
bool LibraryWindow::eventFilter(QObject* watched, QEvent* event) {
  if(event->type()==QEvent::MouseButtonPress){const auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::MiddleButton){for(auto* table:{incompleteTable_,completeTable_}){if(watched==table->viewport()){const auto point=mouse->position().toPoint();if(quickLocalRating(table,table->indexAt(point),point.x())){return true;}}}}}

  if(watched==this){
    if(event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Hide){updateChatReadContext(true);}
    else if(event->type()==QEvent::WindowActivate||event->type()==QEvent::Show||event->type()==QEvent::WindowStateChange){QTimer::singleShot(0,this,[this]{updateChatReadContext();});}
  }
  if (!updating_ && event->type() == QEvent::FocusIn) {
    for (auto* table : {incompleteTable_, completeTable_}) {
      if (watched == table || watched == table->viewport()) { activate(table); selectionChanged(); break; }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}
void LibraryWindow::activate(QTableView* table) {
  if (activeTable_ == table) { return; }
  activeTable_ = table;
  auto* other = table == incompleteTable_ ? completeTable_ : incompleteTable_;
  const QSignalBlocker guard(other->selectionModel());
  other->selectionModel()->clear();
}
std::optional<Target> LibraryWindow::selected() const {
  if(const auto* page=mainTabs_->currentWidget()){
    const auto hash=page->property("BiglyBTC.torrentDetailsHash").toString();
    if(!hash.isEmpty()){const auto row=model_->rowFor(hash);return row>=0?model_->targetAt(row):std::nullopt;}
  }
  const auto rows = activeTable_->selectionModel()->selectedRows(0);
  const auto current = activeTable_->currentIndex();
  if (rows.size() != 1 || !current.isValid() || current.row() != rows.front().row()) { return std::nullopt; }
  const auto source = proxy(activeTable_)->mapToSource(rows.front());
  return source.isValid() ? model_->targetAt(source.row()) : std::nullopt;
}
std::optional<Target> LibraryWindow::actionTarget() const {
  if (!torrentContextActive()) { return std::nullopt; }
  const auto target = selected();
  if(contextOpen_&&(!target||!contextTarget_||target->hash!=contextTarget_->hash
      ||contextSession_!=columnSession_)){return std::nullopt;}
  return target;
}
QStringList LibraryWindow::selectedHashes() const {
  QStringList hashes;
  for (const auto& row : activeTable_->selectionModel()->selectedRows(0)) {
    const auto target = model_->targetAt(proxy(activeTable_)->mapToSource(row).row());
    if (target) { hashes.push_back(target->hash); }
  }
  return hashes;
}
void LibraryWindow::restoreSelection(const QStringList& hashes, bool initial) {
  const int selectedColumn=activeTable_->currentIndex().isValid()?activeTable_->currentIndex().column():0;
  if (hashes.size() == 1) {
    const auto source = model_->index(model_->rowFor(hashes.front()), 0);
    for (auto* table : {incompleteTable_, completeTable_}) {
      if (proxy(table)->mapFromSource(source).isValid()) { activate(table); break; }
    }
  }
  for (auto* table : {incompleteTable_, completeTable_}) { if(table!=activeTable_){table->selectionModel()->clear();} }
  QModelIndex first;
  QItemSelection selection;
  for (const auto& hash : hashes) {
    const auto index = proxy(activeTable_)->mapFromSource(model_->index(model_->rowFor(hash), selectedColumn));
    if (!index.isValid()) { continue; }
    selection.select(index, index);
    if (!first.isValid()) { first = index; }
  }
  if (!first.isValid() && initial) {
    if (incompleteTable_->model()->rowCount() == 0) { activate(completeTable_); }
    first = activeTable_->model()->index(0, 0);
    if (first.isValid()) { selection.select(first, first); }
  }
  const auto current = activeTable_->currentIndex();
  if (current.isValid() && selection.contains(current)) { first = current; }
  activeTable_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  if (activeTable_->currentIndex() != first) {
    activeTable_->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
  }
}
void LibraryWindow::updateCounts() {
  incompleteCount_->setText(tr("Incomplete Torrents (%1)").arg(incompleteTable_->model()->rowCount()));
  completeCount_->setText(tr("Complete Torrents (%1)").arg(completeTable_->model()->rowCount()));
}
void LibraryWindow::updateGeneral() {
  const auto target = selected();
  const auto row = target ? model_->rowFor(target->hash) : -1;
  auto value = [this, row](int column) {
    return row < 0 ? QString::fromUtf8("—") : model_->data(model_->index(row, column), Qt::DisplayRole).toString();
  };
  generalName_->setText(value(0));
  generalState_->setText(value(1));
  generalSize_->setText(value(2));
  generalProgress_->setText(value(3));
  generalReceived_->setText(value(6) + QStringLiteral(" / ") + value(7));
  generalHash_->setText(value(8));
}
void LibraryWindow::updateActions() {
  const auto target = actionTarget();
  continue_->setEnabled(!busy_ && !closing_ && target && model_->canContinue(*target));
  cancel_->setEnabled((busy_||activeRecheck_.has_value()) && !closing_);
  refresh_->setEnabled(!closing_ && (onSavedServicePage() || onStatisticsPage() || (peersPage_&&mainTabs_->currentWidget()==peersPage_) || (!busy_&&(onDownloadHistoryPage()||onRssSourcesPage()||onSubscriptionsPage()||onMetasearchPage()||mainTabs_->currentWidget()==torrentsPage_))));
  files_->setEnabled(!busy_ && !closing_ && target.has_value());
  padding_->setEnabled(!closing_ && mainTabs_->currentWidget() == torrentsPage_);
  closeTab_->setEnabled(!closing_ && (mainTabs_->currentWidget() == torrentsPage_ || onSavedServicePage() || onStatisticsPage() || (peersPage_&&mainTabs_->currentWidget()==peersPage_) || onDownloadHistoryPage() || onRssSourcesPage() || onSubscriptionsPage() || onMetasearchPage()));
  saveColumns_->setEnabled(!closing_ && !busy_ && columnSaveAvailable_ && columnSession_ != 0
      && !activeColumnRequest_ && !pendingColumnRequest_ && !activeColumnSave_ && !activeStatisticsRequest_
      && columnSaveRequest_ != std::numeric_limits<quint64>::max());
  reloadColumns_->setEnabled(!closing_ && columnSession_ != 0 && columnRequest_ != std::numeric_limits<quint64>::max());
  const auto fileTargets=selectedDownloadTargets();
  const bool fileActionsAllowed=!closing_&&!busy_&&!offlinePreview_&&columnSession_&&fileTargets&&!fileTargets->empty()&&fileTargets->size()<=MaximumFileOperationSequence;
  for(const auto& action:fileOperationActions_){if(action){action->setEnabled(fileActionsAllowed);}}
  if(reviewReplacement_){reviewReplacement_->setEnabled(fileActionsAllowed&&fileTargets->size()==1);}
  if(createTorrentAction_){createTorrentAction_->setEnabled(!closing_&&!busy_&&!offlinePreview_&&columnSession_);}
  if (schedulerAction_) { schedulerAction_->setEnabled(!closing_ && !busy_ && columnSession_ != 0); }
  if (optionsAction_) { optionsAction_->setEnabled(!closing_ && !busy_ && columnSession_ != 0); }
  queueDownloadActions();
  QPointer<LibraryWindow> guard(this);
  if(subscriptionsAction_){subscriptionsAction_->setEnabled(!closing_&&!busy_&&columnSession_);if(!guard){return;}}
  if(rssAction_){rssAction_->setEnabled(!closing_&&!busy_&&columnSession_);if(!guard){return;}}
  if(metasearchAction_){metasearchAction_->setEnabled(!closing_&&!busy_&&columnSession_);if(!guard){return;}}
  if(downloadHistoryAction_){downloadHistoryAction_->setEnabled(!closing_&&!busy_&&columnSession_);if(!guard){return;}}
  if(downloadHistoryPage_){downloadHistoryPage_->setAvailable(!closing_&&!busy_);if(!guard){return;}}
  if(categoryAction_){categoryAction_->setEnabled(!closing_&&!offlinePreview_&&columnSession_);if(!guard){return;}}
  if(categoryDialog_){categoryDialog_->setAvailable(!busy_&&!closing_&&!offlinePreview_);if(!guard){return;}}
  if(manualTagAction_){manualTagAction_->setEnabled(!closing_&&!offlinePreview_&&columnSession_);if(!guard){return;}}
  if(manualTagDialog_){manualTagDialog_->setAvailable(!busy_&&!closing_&&!offlinePreview_);if(!guard){return;}}
  if(localRatingAction_){localRatingAction_->setEnabled(fileActionsAllowed&&fileTargets->size()==1);}
  if(!guard){return;}
  for(const auto& action:hybridCompanionActions_){if(action){
    action->setEnabled(fileActionsAllowed&&fileTargets->size()==1);
    if(!guard){return;}
  }}
}
std::optional<DownloadTargets> LibraryWindow::selectedDownloadTargets() const {
  if (!torrentContextActive()) { return std::nullopt; }
  if(mainTabs_->currentWidget()!=torrentsPage_){const auto target=selected();return target?std::optional<DownloadTargets>{DownloadTargets{*target}}:std::nullopt;}
  const auto selectedRows = activeTable_->selectionModel()->selectedRows(0);
  if (selectedRows.isEmpty()) { return std::nullopt; }
  std::vector<int> rows;
  rows.reserve(static_cast<std::size_t>(selectedRows.size()));
  for (const auto& selectedRow : selectedRows) {
    const auto source = proxy(activeTable_)->mapToSource(selectedRow);
    if (!source.isValid()) { return std::nullopt; }
    rows.push_back(source.row());
  }
  std::sort(rows.begin(), rows.end());
  if (std::adjacent_find(rows.begin(), rows.end()) != rows.end()) { return std::nullopt; }
  DownloadTargets result;
  result.reserve(rows.size());
  for (const int row : rows) {
    const auto target = model_->targetAt(row);
    if (!target) { return std::nullopt; }
    result.push_back(*target);
  }
  return result;
}

std::optional<DownloadTargets> LibraryWindow::downloadTargets(core::DownloadAction action, bool all) const {
  const auto bit = downloadActionBit(action);
  if (bit == 0 || busy_ || closing_ || offlinePreview_ || updating_
      || downloadActionRevision_ == std::numeric_limits<quint64>::max()
      || downloadMenuToken_ == std::numeric_limits<quint64>::max()) { return std::nullopt; }
  if(downloadMenuOpen_&&downloadMenuSession_!=columnSession_){return std::nullopt;}
  // Global commands retain their original broad-scope revision fence.
  if(all&&downloadMenuOpen_&&downloadMenuRevision_!=model_->revision()){return std::nullopt;}
  if (all) {
    DownloadTargets result;
    for (int row = 0; row < model_->rowCount(); ++row) {
      if ((model_->permittedActionsAt(row) & bit) == 0) { continue; }
      const auto target = model_->targetAt(row);
      // An eligible but ambiguous identity invalidates the whole submission.
      if (!target) { return std::nullopt; }
      result.push_back(*target);
    }
    return result.empty() ? std::nullopt : std::optional{std::move(result)};
  }
  auto targets = selectedDownloadTargets();
  if(!targets||(downloadMenuOpen_&&(!downloadMenuTargets_
      ||!sameTorrentSet(*targets,*downloadMenuTargets_)))){return std::nullopt;}
  for (const auto& target : *targets) {
    if ((model_->permittedActions(target) & bit) == 0) { return std::nullopt; }
  }
  return targets;
}

void LibraryWindow::dispatchDownload(core::DownloadAction action, bool all) {
  // Never dispatch from stale visual state or from a reentrant action callback.
  if (downloadDispatching_ || (!all && renderedDownloadRevision_ != downloadActionRevision_)) { return; }
  auto targets = downloadTargets(action, all);
  if (!targets) { return; }
  downloadDispatching_ = true;
  QPointer<LibraryWindow> guard(this);
  emit downloadActionRequested(std::move(*targets), action);
  if (guard) { guard->downloadDispatching_ = false; }
}

void LibraryWindow::dispatchDownloadAlias(bool pauseResume) {
  const auto choice = pauseResume ? pauseResumeChoice_ : startStopChoice_;
  if (choice) { dispatchDownload(*choice); }
}

void LibraryWindow::bindDownloadMenu(QMenu* menu) {
  connect(menu, &QMenu::aboutToShow, this, [this] {
    if (downloadMenuToken_ == std::numeric_limits<quint64>::max()) {
      renderedDownloadRevision_ = 0;
      return;
    }
    ++downloadMenuToken_;
    downloadMenuOpen_ = true;
    downloadMenuRevision_ = model_->revision();
    downloadMenuSession_ = columnSession_;
    downloadMenuTargets_ = selectedDownloadTargets();
    queueDownloadActions();
  });
  connect(menu, &QMenu::aboutToHide, this, [this] {
    const auto token = downloadMenuToken_;
    // QAction::triggered may follow aboutToHide; keep its captured intent until
    // the menu's synchronous activation has returned to the event loop.
    QMetaObject::invokeMethod(this, [this, token] {
      if (token != downloadMenuToken_) { return; }
      downloadMenuOpen_ = false;
      downloadMenuTargets_.reset();
      queueDownloadActions();
    }, Qt::QueuedConnection);
  });
}

void LibraryWindow::queueDownloadActions() {
  if (downloadActionRevision_ == std::numeric_limits<quint64>::max()) {
    renderedDownloadRevision_ = 0;
    return;
  }
  ++downloadActionRevision_;
  if (downloadActionsQueued_) { return; }
  downloadActionsQueued_ = true;
  QMetaObject::invokeMethod(this, &LibraryWindow::updateDownloadActions, Qt::QueuedConnection);
}

void LibraryWindow::updateDownloadActions() {
  downloadActionsQueued_ = false;
  const auto revision = downloadActionRevision_;
  const auto modelRevision = model_->revision();
  const auto fileSource = filesModel_->presentation();
  QPointer<LibraryWindow> guard(this);
  const auto current = [&] {
    return guard && guard->downloadActionRevision_ == revision
        && guard->model_->revision() == modelRevision && guard->filesModel_->presentation() == fileSource;
  };
  std::array<bool, core::DownloadActionCount> enabled{};
  for (std::size_t i = 0; i < enabled.size(); ++i) {
    enabled[i] = downloadTargets(static_cast<core::DownloadAction>(i), false).has_value();
  }
  const auto startStop = enabled[static_cast<std::size_t>(core::DownloadAction::Stop)]
      ? std::optional{core::DownloadAction::Stop}
      : enabled[static_cast<std::size_t>(core::DownloadAction::Start)]
          ? std::optional{core::DownloadAction::Start} : std::nullopt;
  const auto pauseResume = enabled[static_cast<std::size_t>(core::DownloadAction::Pause)]
      ? std::optional{core::DownloadAction::Pause}
      : enabled[static_cast<std::size_t>(core::DownloadAction::Resume)]
          ? std::optional{core::DownloadAction::Resume} : std::nullopt;
  const bool recheckAllowed=recheckTargets().has_value();
  const bool startAll = downloadTargets(core::DownloadAction::Start, true).has_value();
  const bool stopAll = downloadTargets(core::DownloadAction::Stop, true).has_value();
  const bool filesEditable = filePolicySelection().has_value();
  const bool canCompact = filePolicySelection(true).has_value();
  const auto filePathSelection=filePolicySelection();const bool canMapFile=columnSession_&&filePathSelection&&filePathSelection->indices.size()==1;
  const bool importsAllowed = canImport();
  // Publish only from this queued entry point. A QAction::changed observer may
  // delete/rebind the Window; no old selection/model caller resumes afterward.
  for (std::size_t i = 0; i < enabled.size(); ++i) {
    const auto item = downloadActions_[i];
    if (item) { item->setEnabled(enabled[i]); }
    if (!current()) { return; }
  }
  if(forceRecheck_){forceRecheck_->setEnabled(recheckAllowed);}if(!current()){return;}
  if (startAll_) { startAll_->setEnabled(startAll); }
  if (!current()) { return; }
  if (stopAll_) { stopAll_->setEnabled(stopAll); }
  if (!current()) { return; }
  if (startStop_) { startStop_->setEnabled(startStop.has_value()); }
  if (!current()) { return; }
  if (startStop_) { startStop_->setText(startStop == core::DownloadAction::Stop ? tr("Stop")
      : startStop == core::DownloadAction::Start ? tr("Start") : tr("Start / Stop")); }
  if (!current()) { return; }
  if (pauseResume_) { pauseResume_->setEnabled(pauseResume.has_value()); }
  if (!current()) { return; }
  if (pauseResume_) { pauseResume_->setText(pauseResume == core::DownloadAction::Pause ? tr("Pause")
      : pauseResume == core::DownloadAction::Resume ? tr("Resume") : tr("Pause / Resume")); }
  if (!current()) { return; }
  for (const auto& item : filePolicyActions_) {
    if (item) { item->setEnabled(filesEditable); }
    if (!current()) { return; }
  }
  for(const auto& action:filePathActions_){if(action){action->setEnabled(canMapFile);}if(!current()){return;}}
  if (fileCompaction_) { fileCompaction_->setEnabled(canCompact); }
  if (!current()) { return; }
  if (openTorrent_) { openTorrent_->setEnabled(importsAllowed); }
  if (!current()) { return; }
  if (openMagnet_) { openMagnet_->setEnabled(importsAllowed); }
  if (!current()) { return; }
  startStopChoice_ = startStop;
  pauseResumeChoice_ = pauseResume;
  renderedDownloadRevision_ = revision;
}

std::optional<LibraryWindow::FilePolicySelection> LibraryWindow::filePolicySelection(bool forCompaction) const {
  if (busy_ || closing_ || offlinePreview_ || updating_ || !torrentContextActive()
      || (mainTabs_->currentWidget()==torrentsPage_&&!inlineDetails_) || detailTabs_->currentIndex() != 1) { return std::nullopt; }
  const auto target = selected();
  const auto source = filesModel_->presentation();
  if (!target || !model_->accepts(*target) || !source || !(forCompaction ? source->canCompact : source->canEdit) || source->target != *target
      || filesModel_->target() != target) { return std::nullopt; }
  const auto rows = filesTable_->selectionModel()->selectedRows(0);
  if (rows.isEmpty() || (forCompaction && rows.size() != 1)) { return std::nullopt; }
  FileIndices indices;
  indices.reserve(static_cast<std::size_t>(rows.size()));
  for (const auto& row : rows) {
    const auto sourceRow = filesProxy_->mapToSource(row);
    if (!sourceRow.isValid() || sourceRow.row() < 0 || static_cast<std::size_t>(sourceRow.row()) >= source->items.size()) { return std::nullopt; }
    const auto& file = source->items[static_cast<std::size_t>(sourceRow.row())];
    if (file.padding || (forCompaction ? !file.canCompact : !filesModel_->editableAt(sourceRow.row()))) { return std::nullopt; }
    const auto original = filesModel_->originalIndex(sourceRow.row());
    if (!original) { return std::nullopt; }
    indices.push_back(*original);
  }
  std::sort(indices.begin(), indices.end());
  if (std::adjacent_find(indices.begin(), indices.end()) != indices.end()) { return std::nullopt; }
  FilePolicySelection result{*target, std::move(indices), source};
  if (fileMenuOpen_ && result != fileMenuSelection_) { return std::nullopt; }
  return result;
}

void LibraryWindow::dispatchFilePolicy(int priority, bool skipped) {
  if (downloadDispatching_ || renderedDownloadRevision_ != downloadActionRevision_) { return; }
  auto selectedFiles = filePolicySelection();
  if (!selectedFiles) { return; }
  downloadDispatching_ = true;
  QPointer<LibraryWindow> guard(this);
  emit filePolicyRequested(selectedFiles->target, std::move(selectedFiles->indices), priority, skipped);
  if (guard) { guard->downloadDispatching_ = false; }
}

void LibraryWindow::dispatchFileCompaction() {
  if (downloadDispatching_ || renderedDownloadRevision_ != downloadActionRevision_) { return; }
  const auto selection = filePolicySelection(true);
  if (!selection) { return; }
  const auto found = std::ranges::find(selection->source->items, selection->indices.front(), &FileItem::index);
  if (found == selection->source->items.end()) { return; }
  downloadDispatching_ = true;
  QPointer<LibraryWindow> guard(this);
  QPointer<QMessageBox> dialog = new QMessageBox(QMessageBox::Warning, tr("Reclaim file space"),
      tr("Stop downloading this file and discard its interior data?\n\n%1\n\nOnly boundary data needed by adjacent files is kept. Discarded data must be downloaded again if you select the file later.").arg(found->path),
      QMessageBox::Yes | QMessageBox::Cancel, this);
  dialog->setObjectName(QStringLiteral("files-reclaim-confirmation"));
  dialog->setTextFormat(Qt::PlainText);
  dialog->setDefaultButton(QMessageBox::Cancel);
  dialog->setEscapeButton(QMessageBox::Cancel);
  const auto answer = dialog->exec();
  if (!guard) { return; }
  if (dialog) { dialog->deleteLater(); }
  // A nested dialog event may replace the profile, selection or immutable file
  // frame. Confirmation authorizes only the exact original index shown above.
  if (answer == QMessageBox::Yes && filePolicySelection(true) == selection
      && renderedDownloadRevision_ == downloadActionRevision_) {
    emit fileCompactionRequested(selection->target, selection->indices.front());
  }
  if (guard) { guard->downloadDispatching_ = false; }
}

void LibraryWindow::filePopup(const QPoint& point) {
  if (fileMenuOpen_) { return; }
  const auto index = filesTable_->indexAt(point);
  QPointer<LibraryWindow> guard(this);
  if (!index.isValid()) { filesTable_->selectionModel()->clearSelection(); }
  else if (!filesTable_->selectionModel()->isSelected(index)) { filesTable_->setCurrentIndex(index); }
  if (!guard) { return; }
  // Retain only the already owned immutable file frame and exact original IDs.
  // New details, a new Target or selection changes invalidate this menu intent.
  fileMenuSelection_ = filePolicySelection();
  if (!fileMenuSelection_) { fileMenuSelection_ = filePolicySelection(true); }
  fileMenuOpen_ = true;
  queueDownloadActions();
  QPointer<QMenu> menu(new QMenu(this));
  menu->setObjectName(QStringLiteral("files-priority-context"));
  for (const auto& item : filePolicyActions_) { if (item) { menu->addAction(item.data()); } }
  menu->addSeparator();
  if (fileCompaction_) { menu->addAction(fileCompaction_.data()); }
  menu->addSeparator();for(const auto& action:filePathActions_){if(action){menu->addAction(action.data());}}
  menu->exec(filesTable_->viewport()->mapToGlobal(point));
  if (!guard) { return; }
  if (menu) { menu->deleteLater(); }
  fileMenuOpen_ = false;
  fileMenuSelection_.reset();
  queueDownloadActions();
}

bool LibraryWindow::canImport() const {
  return columnSession_ != 0 && !busy_ && !closing_ && !offlinePreview_;
}

bool LibraryWindow::submitTorrentImport(QString metadata, QString destination, bool start) {
  if (!canImport() || downloadDispatching_ || !torrentImportFields(metadata, destination)) { return false; }
  downloadDispatching_ = true;
  QPointer<LibraryWindow> guard(this);
  emit torrentImportRequested(std::move(metadata), std::move(destination), start);
  if (guard) { guard->downloadDispatching_ = false; }
  return true;
}

bool LibraryWindow::submitMagnetImport(QString uri, QString destination) {
  if (!canImport() || downloadDispatching_ || !magnetImportFields(uri, destination)) { return false; }
  downloadDispatching_ = true;
  QPointer<LibraryWindow> guard(this);
  emit magnetImportRequested(std::move(uri), std::move(destination));
  if (guard) { guard->downloadDispatching_ = false; }
  return true;
}

void LibraryWindow::openImportDialog(bool magnet) {
  if (!canImport() || downloadDispatching_) { return; }
  if(!magnet){openTorrentIntake();return;}
  const auto session = columnSession_;
  QPointer<LibraryWindow> guard(this);
  QPointer<QDialog> dialog = new QDialog(this);
  dialog->setObjectName(magnet ? QStringLiteral("magnet-import-dialog") : QStringLiteral("torrent-import-dialog"));
  dialog->setWindowTitle(magnet ? tr("Open Magnet Link") : tr("Open Torrent File"));
  auto* layout = new QVBoxLayout(dialog);
  auto* form = new QFormLayout;
  QPointer<QLineEdit> source = new QLineEdit(dialog);
  source->setObjectName(QStringLiteral("import-source"));
  source->setMaxLength(magnet ? 16384 : 4096);
  source->setAccessibleName(magnet ? tr("Magnet link") : tr("Torrent file"));
  if (magnet) {
    source->setPlaceholderText(QStringLiteral("magnet:?xt=urn:btih:…"));
    form->addRow(tr("Magnet link:"), source.data());
  } else {
    auto* row = new QWidget(dialog);
    auto* line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0);
    auto* browse = new QPushButton(tr("Browse…"), row);
    browse->setObjectName(QStringLiteral("import-browse-torrent"));
    line->addWidget(source.data(), 1); line->addWidget(browse);
    form->addRow(tr("Torrent file:"), row);
    connect(browse, &QPushButton::clicked, dialog.data(), [dialog, source] {
      if (!dialog || !source) { return; }
      const auto path = QFileDialog::getOpenFileName(dialog.data(), tr("Open Torrent File"), {}, tr("Torrent files (*.torrent)"));
      if (dialog && source && !path.isEmpty()) { source->setText(path); }
    });
  }
  auto* row = new QWidget(dialog);
  auto* line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0);
  QPointer<QLineEdit> destination = new QLineEdit(row);
  destination->setObjectName(QStringLiteral("import-destination"));
  destination->setMaxLength(4096); destination->setAccessibleName(tr("Download directory"));
  destination->setText(defaultSaveDirectory_);
  auto* browse = new QPushButton(tr("Browse…"), row);
  browse->setObjectName(QStringLiteral("import-browse-destination"));
  line->addWidget(destination.data(), 1); line->addWidget(browse);
  form->addRow(tr("Download directory:"), row);
  layout->addLayout(form);
  QPointer<QCheckBox> start;
  if (!magnet) {
    start = new QCheckBox(tr("Start download"), dialog);
    start->setObjectName(QStringLiteral("import-start")); start->setChecked(true);
    layout->addWidget(start.data());
  }
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, dialog);
  layout->addWidget(buttons);
  QPointer<QPushButton> open = buttons->button(QDialogButtonBox::Open);
  open->setEnabled(false);
  auto validate = [dialog, source, destination, open, magnet] {
    if (!dialog || !source || !destination || !open) { return; }
    const bool valid = magnet ? magnetImportFields(source->text(), destination->text())
        : torrentImportFields(source->text(), destination->text());
    open->setEnabled(valid);
  };
  // Button property callbacks occur after the line edit's own setter returns.
  connect(source.data(), &QLineEdit::textChanged, dialog.data(), validate, Qt::QueuedConnection);
  connect(destination.data(), &QLineEdit::textChanged, dialog.data(), validate, Qt::QueuedConnection);
  connect(browse, &QPushButton::clicked, dialog.data(), [dialog, destination] {
    if (!dialog || !destination) { return; }
    const auto path = QFileDialog::getExistingDirectory(dialog.data(), tr("Download directory"), destination->text());
    if (dialog && destination && !path.isEmpty()) { destination->setText(path); }
  });
  connect(buttons, &QDialogButtonBox::accepted, dialog.data(), &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dialog.data(), &QDialog::reject);
  dialog->resize(640, dialog->sizeHint().height());
  const int result = dialog->exec();
  QString sourceValue, destinationValue;
  bool startValue = false;
  if (dialog && result == QDialog::Accepted && source && destination) {
    sourceValue = source->text(); destinationValue = destination->text();
    startValue = start && start->isChecked();
  }
  if (dialog) { delete dialog.data(); }
  if (!guard || result != QDialog::Accepted || columnSession_ != session) { return; }
  if (magnet) { submitMagnetImport(std::move(sourceValue), std::move(destinationValue)); }
  else { submitTorrentImport(std::move(sourceValue), std::move(destinationValue), startValue); }
}

void LibraryWindow::ensureTorrentIntakeDialog() {
  if(!intakeDialog_){
    intakeDialog_=new TorrentIntakeDialog(this);intakeDialog_->setWindowModality(Qt::WindowModal);
    connect(intakeDialog_,&TorrentIntakeDialog::previewRequested,this,[this](TorrentIntakeRequest request){
      if(!canImport()||request.session!=columnSession_){if(intakeDialog_){intakeDialog_->rejectPreview(request);}return;}
      const auto oldSearch=metasearchReview_;metasearchReview_.reset();QPointer<LibraryWindow> guard(this);
      if(oldSearch){emit metasearchDownloadCancelRequested(*oldSearch);if(!guard){return;}}
      activeIntakePreview_=request;emit torrentIntakeRequested(std::move(request));
    });
    connect(intakeDialog_,&TorrentIntakeDialog::submitted,this,[this](TorrentIntakeSubmission submission){
      const auto preview=intakeDialog_?intakeDialog_->preview():TorrentIntakePresentation{};
      if(!canImport()||!intakeDialog_||!intakeDialog_->isVisible()||!preview||submission.session!=columnSession_
          ||preview->source.request!=submission.request||preview->revision!=submission.revision){
        if(intakeDialog_){intakeDialog_->rejectSubmission(submission);}return;
      }
      activeIntakeSubmission_=submission;emit torrentIntakeSubmitted(std::move(submission));
    });
    connect(intakeDialog_,&TorrentIntakeDialog::previewCancelled,this,[this](TorrentIntakeRequest request){
      if(activeIntakePreview_&&*activeIntakePreview_==request){activeIntakePreview_.reset();emit cancelRequested();}
    });
    connect(intakeDialog_,&QDialog::rejected,this,[this]{const auto search=metasearchReview_;metasearchReview_.reset();if(search){emit metasearchDownloadCancelRequested(*search);}});
    connect(intakeDialog_,&QObject::destroyed,this,[this]{activeIntakePreview_.reset();activeIntakeSubmission_.reset();const auto search=metasearchReview_;metasearchReview_.reset();if(search){emit metasearchDownloadCancelRequested(*search);}});
  }
}
void LibraryWindow::openTorrentIntake() {
  if(metasearchReview_&&intakeDialog_&&intakeDialog_->isVisible()){intakeDialog_->raise();return;}
  if(!canImport()||downloadDispatching_){return;}
  QPointer<LibraryWindow> constructing(this);ensureTorrentIntakeDialog();if(!constructing||!intakeDialog_){return;}
  QPointer<LibraryWindow> guard(this);const auto dialog=intakeDialog_;
  if(!dialog->beginSession(columnSession_,defaultSaveDirectory_)||!guard||!dialog){return;}
  dialog->setBusy(busy_);if(!guard||!dialog){return;}dialog->show();
}
void LibraryWindow::setTorrentIntake(TorrentIntakePresentation packet) {
  if(!packet||!activeIntakePreview_||packet->source!=*activeIntakePreview_||packet->source.session!=columnSession_){return;}
  activeIntakePreview_.reset();if(intakeDialog_){intakeDialog_->setPreview(std::move(packet));}
}
void LibraryWindow::setTorrentIntakeFinished(TorrentIntakeResultPresentation packet) {
  if(!packet||!activeIntakeSubmission_||packet->session!=columnSession_
      ||packet->session!=activeIntakeSubmission_->session||packet->request!=activeIntakeSubmission_->request
      ||packet->revision!=activeIntakeSubmission_->revision){return;}
  activeIntakeSubmission_.reset();if(intakeDialog_){intakeDialog_->setResult(std::move(packet));}
}
void LibraryWindow::rejectTorrentIntake(const TorrentIntakeRequest& request) {
  if(!activeIntakePreview_||*activeIntakePreview_!=request){return;}
  const auto copied=request;activeIntakePreview_.reset();if(intakeDialog_){intakeDialog_->rejectPreview(copied);}
}
void LibraryWindow::rejectTorrentIntakeSubmission(const TorrentIntakeSubmission& submission) {
  if(!activeIntakeSubmission_||activeIntakeSubmission_->session!=submission.session
      ||activeIntakeSubmission_->request!=submission.request||activeIntakeSubmission_->revision!=submission.revision){return;}
  const auto copied=submission;activeIntakeSubmission_.reset();if(intakeDialog_){intakeDialog_->rejectSubmission(copied);}
}

bool LibraryWindow::torrentContextActive() const {
  const auto* page=mainTabs_->currentWidget();return page&&(page==torrentsPage_||!page->property("BiglyBTC.torrentDetailsHash").toString().isEmpty());
}
void LibraryWindow::placeDetailTabs(){
  QPointer<LibraryWindow> guard(this);auto* old=detailTabs_->parentWidget();
  if(old&&!old->property("BiglyBTC.torrentDetailsHash").toString().isEmpty()){old->setProperty("BiglyBTC.detailsSubtab",detailTabs_->currentIndex());if(!guard){return;}}
  auto* page=mainTabs_->currentWidget();
  if(page&&!page->property("BiglyBTC.torrentDetailsHash").toString().isEmpty()){
    const int subtab=page->property("BiglyBTC.detailsSubtab").toInt();
    if(auto* layout=qobject_cast<QVBoxLayout*>(page->layout())){layout->addWidget(detailTabs_);if(!guard){return;}}
    detailTabs_->show();if(guard&&subtab>=0&&subtab<detailTabs_->count()){detailTabs_->setCurrentIndex(subtab);}
  }else{
    detailTabs_->hide();if(!guard){return;}
    if(inlineDetails_&&detailsHome_){detailsHome_->insertWidget(1,detailTabs_);if(!guard){return;}detailTabs_->show();}
    else{detailTabs_->setParent(torrentsPage_);}
  }
}
void LibraryWindow::openTorrentDetails(const Target& target,int subtab){
  if(closing_||!model_->accepts(target)){return;}
  QPointer<LibraryWindow> guard(this);QWidget* page=nullptr;
  for(int i=0;i<mainTabs_->count();++i){auto* candidate=mainTabs_->widget(i);if(candidate->property("BiglyBTC.torrentDetailsHash").toString()==target.hash){page=candidate;break;}}
  if(!page){
    page=new QWidget(mainTabs_);page->setObjectName(QStringLiteral("torrent-details-page-" )+target.hash);page->setProperty("BiglyBTC.torrentDetailsHash",target.hash);page->setProperty("BiglyBTC.detailsSubtab",0);
    auto* layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);
    auto name=model_->data(model_->index(model_->rowFor(target.hash),0),Qt::DisplayRole).toString();name.replace('&',QStringLiteral("&&"));
    mainTabs_->addTab(page,name);
  }
  if(subtab>=0){page->setProperty("BiglyBTC.detailsSubtab",subtab);}
  detailsRequestedFor_.reset();suppressAutomaticDetails_=false;
  mainTabs_->setCurrentWidget(page);if(!guard){return;}placeDetailTabs();if(!guard){return;}
  if(subtab>=0&&subtab<detailTabs_->count()){detailTabs_->setCurrentIndex(subtab);if(!guard){return;}}
  selectionChanged();
}
void LibraryWindow::selectionChanged() {
  if (updating_) { return; }
  updateActions();
  updateGeneral();
  updateTorrentDetailInterest();
  const auto target = selected();
  if (filesModel_->target() != target) {
    const auto previous = filesModel_->target();
    if (target && detailsRequestedFor_ && detailsRequestedFor_->hash != target->hash) { suppressAutomaticDetails_ = false; }
    const auto index = filesTable_->currentIndex();
    restoreFileIndex_ = previous && target && previous->hash == target->hash && index.isValid()
        ? filesModel_->originalIndex(filesProxy_->mapToSource(index).row()) : std::nullopt;
    filesModel_->select(target);
  }
  if (target && (detailTabs_->currentIndex() == 1 || !model_->hasDetails(*target))) { requestDetails(); }
}
void LibraryWindow::requestDetails(bool force) {
  if (!torrentContextActive() || (mainTabs_->currentWidget()==torrentsPage_&&!inlineDetails_) || busy_ || closing_ || (suppressAutomaticDetails_ && !force)) { return; }
  const auto target = selected();
  if (!target || (!force && detailsRequestedFor_ == target)) { return; }
  detailsRequestedFor_ = target;
  emit detailsRequested(*target);
}
void LibraryWindow::setPresentation(const Presentation& snapshot) {
  const TableViewportGuard viewport(incompleteTable_,completeTable_);
  const auto hashes = selectedHashes();
  const auto previousFile = filesTable_->currentIndex();
  const auto fileIndex = previousFile.isValid()
      ? filesModel_->originalIndex(filesProxy_->mapToSource(previousFile).row()) : std::nullopt;
  updating_ = true;
  const bool applied = model_->apply(snapshot);
  if (applied) { restoreSelection(hashes, !presented_); presented_ = true; }
  updating_ = false;
  if (!applied) { return; }
  if(classicStatus_){classicStatus_->setSettings(snapshot->statusBar,snapshot->units);}if(azureusIndicator_){azureusIndicator_->setVisible(snapshot->statusBar.dht);}
  ratingPersonalSort_=snapshot->ratingPersonalSort;const QPointer<LibraryWindow> ratingGuard(this);if(incompleteColumns_){incompleteColumns_->setRatingPersonalSort(ratingPersonalSort_);}if(!ratingGuard){return;}if(completeColumns_){completeColumns_->setRatingPersonalSort(ratingPersonalSort_);}if(!ratingGuard){return;}
  inlineDetails_=snapshot->inlineDetails;detailsOnDoubleClick_=snapshot->detailsOnDoubleClick;
  confirmExit_=snapshot->confirmExit;activeChecks_=snapshot->activeChecks;
  QPointer<LibraryWindow> placementGuard(this);placeDetailTabs();if(!placementGuard){return;}
  const bool restoring=snapshot->startupTotal>0&&snapshot->startupDone<snapshot->startupTotal&&snapshot->startupTotal<=LibraryModel::MaximumRows;
  startupProgress_->setRange(0,restoring?static_cast<int>(snapshot->startupTotal):1);if(!placementGuard){return;}
  startupProgress_->setValue(restoring?static_cast<int>(snapshot->startupDone):1);if(!placementGuard){return;}startupProgress_->setVisible(restoring);if(!placementGuard){return;}
  if (hashes.size() == 1 && selected() && hashes.front() == selected()->hash && fileIndex) { restoreFileIndex_ = fileIndex; }
  setMessage(model_->rowCount() == 1 ? tr("1 torrent") : tr("%1 torrents").arg(model_->rowCount()));
  QPointer<LibraryWindow> guard(this);
  selectionChanged();
  if (guard) { updateCounts(); }
  if(guard&&fileOperationSequence_){fileOperationSequence_->setPresentation(snapshot);}
}
void LibraryWindow::setTelemetry(const Telemetry& update) {
  const TableViewportGuard viewport(incompleteTable_,completeTable_);
  QPointer<LibraryWindow> guard(this);
  const auto hashes = selectedHashes();
  updating_ = true;
  const bool applied = model_->apply(update);
  if (!guard) { return; }
  if (applied) { restoreSelection(hashes); }
  if (!guard) { return; }
  updating_ = false;
  if (applied) {
    if (statisticsPage_ && model_->accepts(update.target)) {
      const auto row = model_->rowFor(update.target.hash);
      statisticsPage_->recordPayload(update, model_->data(model_->index(row, 0), Qt::DisplayRole).toString());
      if (!guard) { return; }
    }
    setMessage(update.state);
    if (!guard) { return; }
    selectionChanged();
    if (guard) { updateCounts(); }
  }
}
void LibraryWindow::setDetails(const Details& details) {
  const TableViewportGuard viewport(incompleteTable_,completeTable_);
  const auto hashes = selectedHashes();
  updating_ = true;
  const bool applied = model_->apply(details);
  if (applied) { restoreSelection(hashes); }
  updating_ = false;
  if (!applied) { return; }
  QPointer<LibraryWindow> guard(this);
  selectionChanged();
  if (!guard) { return; }
  const auto target = selected();
  if (target && *target == details.target && details.files) {
    const auto index = filesTable_->currentIndex();
    const auto before = index.isValid() ? filesModel_->originalIndex(filesProxy_->mapToSource(index).row()) : restoreFileIndex_;
    if (!filesModel_->apply(details.files)) { setMessage(tr("File details could not be displayed")); return; }
    if (before) {
      const auto row = filesModel_->rowFor(*before);
      if (row >= 0) { filesTable_->setCurrentIndex(filesProxy_->mapFromSource(filesModel_->index(row, 0))); }
    }
    restoreFileIndex_.reset();
  }
  updateActions();
  updateCounts();
}
void LibraryWindow::setMessage(const QString& message) { message_->setText(message.left(8192)); }
void LibraryWindow::setBusy(bool value) {
  QPointer<LibraryWindow> optionsGuard(this);
  if(intakeDialog_){intakeDialog_->setBusy(value);}if(!optionsGuard){return;}
  if(!value&&activeIntakePreview_){rejectTorrentIntake(*activeIntakePreview_);if(!optionsGuard){return;}}
  if(!value&&activeIntakeSubmission_){rejectTorrentIntakeSubmission(*activeIntakeSubmission_);if(!optionsGuard){return;}}
  if(fileOperationSequence_){fileOperationSequence_->setBusy(value);}if(!optionsGuard){return;}
  if(fileOperationDialog_){fileOperationDialog_->setBusy(value);}if(!optionsGuard){return;}
  if(torrentCreationDialog_){torrentCreationDialog_->setBusy(value);}if(!optionsGuard){return;}
  if(!value&&activeFileOperation_&&fileOperationDialog_&&fileOperationDialog_->pending()){
    const auto request=*activeFileOperation_;
    if(fileOperationExecuting_){FileOperationSubmission v{fileOperationDialog_->plan(),false};rejectFileOperationSubmission(v);}
    else{rejectFileOperationRequest(request);}
    if(!optionsGuard){return;}
  }
  if(!value&&activeTorrentCreation_&&torrentCreationDialog_&&torrentCreationDialog_->pending()){rejectTorrentCreationRequest(*activeTorrentCreation_);if(!optionsGuard){return;}}
  if (schedulerDialog_) { schedulerDialog_->setBusy(value); }
  if (!optionsGuard) { return; }
  if (optionsDialog_) { optionsDialog_->setBusy(value); }
  if (!optionsGuard) { return; }
  if (!value) {
    if(localRatingIdentity_&&localRatingLoading_){rejectLocalRatingRequest(*localRatingIdentity_);if(!optionsGuard){return;}}
    if(localRatingIdentity_&&localRatingSaving_){LocalRatingSubmission unknown;unknown.identity=*localRatingIdentity_;rejectLocalRatingSave(unknown);if(!optionsGuard){return;}}
    if (activeSchedulerLoad_) { rejectSchedulerRequest(*activeSchedulerLoad_); }
    if (!optionsGuard) { return; }
    if (activeSchedulerSave_) { rejectSchedulerSave(*activeSchedulerSave_); }
    if (!optionsGuard) { return; }
    if (activeOptionsLoad_) { rejectOptionsRequest(*activeOptionsLoad_); }
    if (!optionsGuard) { return; }
    if (activeOptionsSave_) { rejectOptionsSave(*activeOptionsSave_); }
    if (!optionsGuard) { return; }
  }
  QPointer<LibraryWindow> guard(this);
  busy_ = value;
  if(subscriptionsPage_){subscriptionsPage_->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard){return;}}
  if(rssPage_){rssPage_->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard){return;}}
  if(metasearchPage_){metasearchPage_->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard){return;}}
  for(const auto& controls:{sharingControls_,trackerControls_}){if(controls){controls->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard){return;}}}
  if(metasearchMagnetDialog_){metasearchMagnetDialog_->setBusy(busy_);if(!guard){return;}}
  if(!busy_&&pendingMetasearchReview_){const auto review=std::move(pendingMetasearchReview_);pendingMetasearchReview_.reset();setMetasearchDownloadResult(review);if(!guard){return;}}
  if (statisticsPage_) { statisticsPage_->setAccountingBusy(value); }
  if (!guard) { return; }
  updateActions();
  if (!guard || busy_) { return; }
  if (activeStatisticsRequest_) {
    const auto request = *activeStatisticsRequest_;
    activeStatisticsRequest_.reset();
    if (!closing_ && request.page && request.page == statisticsPage_) {
      if (request.range) {
        request.page->setHistoryRangeFailed(request.session, request.request, *request.range, tr("Rolling history request did not complete"));
      } else if (request.historyMonth) {
        request.page->setHistoryMonthFailed(request.session, request.request, *request.historyDate, tr("30-day history request did not complete"));
      } else if (request.historyWeek) {
        request.page->setHistoryWeekFailed(request.session, request.request, *request.historyDate, tr("Seven-day history request did not complete"));
      } else if (request.historyDate) {
        request.page->setHistoryFailed(request.session, request.request, *request.historyDate, tr("History request did not complete"));
      } else {
        request.page->setFailed(request.session, request.request, tr("Statistics request did not complete"));
      }
      if (!guard) { return; }
    }
  }
  if (activeSavedRequest_) {
    const auto request = *activeSavedRequest_;
    activeSavedRequest_.reset();
    failSavedServices(request, tr("Saved configuration request did not complete"));
    if (!guard) { return; }
  }
  if (activeColumnRequest_) {
    const auto request = *activeColumnRequest_;
    activeColumnRequest_.reset();
    if (!closing_ && request.session == columnSession_ && request.request == columnRequest_) {
      setMessage(tr("Saved column-layout request did not complete"));
      if (!guard) { return; }
    }
  }
  if (activeColumnSave_) {
    activeColumnSave_.reset();
    columnSaveAvailable_ = false;
    setMessage(tr("Column-layout save did not report a result; reload before retrying"));
    if (!guard) { return; }
  }
  updateActions();
  if (!guard) { return; }
  if (closing_) {
    pendingSavedRequest_.reset();
    pendingColumnRequest_.reset();
    pendingStatisticsRequest_.reset();
    pendingHistoryRequest_.reset();
    QTimer::singleShot(0, this, &QWidget::close);
    return;
  }
  queueScheduler();
  queueOptions();
  queueOptionalViews();
  if (mainTabs_->currentWidget() == torrentsPage_
      && (detailTabs_->currentIndex() == 1 || (selected() && !model_->hasDetails(*selected())))) {
    QTimer::singleShot(0, this, [this] { requestDetails(); });
  }
}
void LibraryWindow::closeEvent(QCloseEvent* event) {
  if(exitPromptOpen_){event->ignore();return;}
  if(!closing_&&(confirmExit_||activeChecks_>0)){
    QPointer<LibraryWindow> guard(this);exitPromptOpen_=true;
    QPointer<QMessageBox> question=new QMessageBox(QMessageBox::Warning,tr("Exit BiglyBTC"),activeChecks_>0
        ?tr("%1 torrent integrity checks are active. Exit and interrupt these checks? They may need to run again next time.").arg(activeChecks_)
        :tr("Exit BiglyBTC?"),QMessageBox::Yes|QMessageBox::No,this);
    question->setObjectName(QStringLiteral("classic-exit-confirmation"));question->setDefaultButton(QMessageBox::No);
    question->setEscapeButton(QMessageBox::No);question->setWindowModality(Qt::ApplicationModal);
    const auto answer=question->exec();if(!guard){return;}if(question){delete question.data();}exitPromptOpen_=false;
    if(answer!=QMessageBox::Yes){event->ignore();return;}
  }
  QPointer<LibraryWindow> optionsGuard(this);
  if(localRatingDialog_&&localRatingDialog_->isVisible()){localRatingDialog_->reject();if(!optionsGuard){return;}}
  if(intakeDialog_&&intakeDialog_->isVisible()&&!intakeDialog_->close()){event->ignore();return;}if(!optionsGuard){return;}
  if(fileOperationSequence_&&!fileOperationSequence_->closeForOwner()){event->ignore();return;}if(!optionsGuard){return;}
  if(fileOperationDialog_&&fileOperationDialog_->isVisible()&&!fileOperationDialog_->close()){event->ignore();return;}if(!optionsGuard){return;}
  if(torrentCreationDialog_&&torrentCreationDialog_->isVisible()&&!torrentCreationDialog_->close()){event->ignore();return;}if(!optionsGuard){return;}
  if (schedulerDialog_ && schedulerDialog_->isVisible() && !schedulerDialog_->close()) { event->ignore(); return; }
  if (!optionsGuard) { return; }
  pendingSchedulerLoad_.reset();
  if (optionsDialog_ && optionsDialog_->isVisible() && !optionsDialog_->close()) { event->ignore(); return; }
  if (!optionsGuard) { return; }
  pendingOptionsLoad_.reset();
  closing_ = true;
  if(subscriptionsPage_){subscriptionsPage_->invalidateSession();if(!optionsGuard){return;}}
  if(rssPage_){rssPage_->invalidateSession();if(!optionsGuard){return;}}
  if(metasearchPage_){metasearchPage_->invalidateSession();if(!optionsGuard){return;}}
  pendingDownloadHistoryCommand_.reset();if(downloadHistoryPage_){downloadHistoryPage_->invalidateSession();if(!optionsGuard){return;}}
  if(categoryDialog_){const auto dialog=categoryDialog_;categoryDialog_.clear();dialog->invalidateSession();if(!optionsGuard){return;}}
  if(manualTagDialog_){const auto dialog=manualTagDialog_;manualTagDialog_.clear();dialog->invalidateSession();if(!optionsGuard){return;}}
  pendingSavedRequest_.reset();
  pendingColumnRequest_.reset();
  pendingStatisticsRequest_.reset();
  pendingHistoryRequest_.reset();
  QPointer<LibraryWindow> guard(this);
  updateActions();
  if (!guard) { return; }
  if (busy_) {
    setMessage(tr("Finishing current operation…"));
    event->ignore();
    emit cancelRequested();
    return;
  }
  if (sessionCloseEnabled_ && !sessionCloseAcknowledged_) {
    event->ignore();
    if (!sessionCloseRequested_) {
      sessionCloseRequested_ = true;
      emit sessionCloseRequested();
    }
    return;
  }
  activeSavedRequest_.reset();
  activeColumnRequest_.reset();
  activeColumnSave_.reset();
  activeStatisticsRequest_.reset();
  QMainWindow::closeEvent(event);
}
void LibraryWindow::popup(QTableView* table, const QPoint& point) {
  activate(table);
  const auto index = table->indexAt(point);
  if (!index.isValid()) { table->selectionModel()->clear(); }
  if (index.isValid() && !table->selectionModel()->isSelected(index)) { table->setCurrentIndex(index); }
  QPointer<LibraryWindow> guard(this);
  selectionChanged();
  if (!guard) { return; }
  contextTarget_ = selected();
  contextSession_ = columnSession_;
  contextOpen_ = true;
  updateActions();
  QPointer<QMenu> menu(new QMenu(this));
  menu->setObjectName(QStringLiteral("classic-torrent-context"));
  bindDownloadMenu(menu);
  menu->addAction(findChild<QAction*>(QStringLiteral("classic-launch")));
  for (const auto& item : downloadActions_) { if (item) { menu->addAction(item.data()); } }
  menu->addSeparator();
  menu->addAction(startStop_.data());
  menu->addAction(pauseResume_.data());
  if(localRatingAction_){menu->addAction(localRatingAction_);}
  auto* related=menu->addAction(tr("Discover related torrents"));related->setObjectName("classic-discover-related");related->setEnabled(swarmSnapshot_.profile&&swarmSnapshot_.profile->settings.enabled&&selectedHashes().size()==1);connect(related,&QAction::triggered,this,[this]{const auto hashes=selectedHashes();if(hashes.size()!=1){return;}const auto hash=QByteArray::fromHex(hashes.front().toLatin1()).first(20);if(hash.size()!=20){return;}showSwarmDiscoveries();swarmPage_->filterRelated(hash);emit swarmLookupRequested(hash);});
  auto* tags=menu->addMenu(tr("Tags"));tags->setObjectName(QStringLiteral("classic-context-tags"));
  connect(tags,&QMenu::aboutToShow,this,[this,tags]{rebuildManualTagMenu(tags);});
  for(const auto& action:fileOperationActions_){if(action){menu->addAction(action);}}
  menu->addSeparator();
  menu->addAction(continue_);
  menu->addAction(cancel_);
  menu->addAction(files_);
  menu->addSeparator();
  menu->addAction(refresh_);
  menu->exec(table->viewport()->mapToGlobal(point));
  if (!guard) { return; }
  if (menu) { menu->deleteLater(); }
  contextOpen_ = false;
  contextTarget_.reset();
  updateActions();
}

#include "local_rating_window.inc"
#include "manual_tag_window.inc"
#include "category_window.inc"

void LibraryWindow::openOptions() {
  if (closing_ || !columnSession_) { return; }
  if (optionsDialog_) { optionsDialog_->show(); optionsDialog_->raise(); optionsDialog_->activateWindow(); return; }
  optionsDialog_ = new OptionsDialog(this);
  optionsDialog_->setAttribute(Qt::WA_DeleteOnClose);
  optionsDialog_->setWindowModality(Qt::WindowModal);
  optionsDialog_->beginSession(columnSession_);
  optionsDialog_->setBusy(busy_);
  connect(optionsDialog_, &OptionsDialog::loadRequested, this, [this](OptionsRequest request) {
    pendingOptionsLoad_ = request; queueOptions();
  });
  connect(optionsDialog_, &OptionsDialog::saveRequested, this, [this](OptionsSubmission submission) {
    activeOptionsSave_ = submission;
    emit optionsSaveRequested(std::move(submission));
  });
  connect(optionsDialog_, &QObject::destroyed, this, [this] {
    pendingOptionsLoad_.reset(); activeOptionsLoad_.reset(); activeOptionsSave_.reset();
  });
  optionsDialog_->show();
}
void LibraryWindow::queueOptions() {
  if (!pendingOptionsLoad_ || busy_ || closing_ || optionsDispatchQueued_) { return; }
  optionsDispatchQueued_ = true;
  QTimer::singleShot(0, this, [this] {
    optionsDispatchQueued_ = false;
    if (!pendingOptionsLoad_ || busy_ || closing_ || !optionsDialog_) { return; }
    const auto request = *pendingOptionsLoad_;
    pendingOptionsLoad_.reset(); activeOptionsLoad_ = request;
    emit optionsRequested(request);
  });
}
void LibraryWindow::setOptions(OptionsPresentation packet) {
  if (!packet || !optionsDialog_ || !activeOptionsLoad_
      || packet->session != activeOptionsLoad_->session || packet->request != activeOptionsLoad_->request) { return; }
  activeOptionsLoad_.reset(); optionsDialog_->setSnapshot(std::move(packet));
}
void LibraryWindow::setOptionsSaved(OptionsSavePresentation packet) {
  if (!packet || !optionsDialog_ || !activeOptionsSave_
      || packet->session != activeOptionsSave_->session || packet->request != activeOptionsSave_->request
      || packet->revision != activeOptionsSave_->revision) { return; }
  activeOptionsSave_.reset(); optionsDialog_->setSaveResult(std::move(packet));
}
void LibraryWindow::rejectOptionsRequest(OptionsRequest request) {
  auto packet = std::make_shared<OptionsPacket>(); packet->session = request.session; packet->request = request.request;
  packet->failure = tr("Settings could not be loaded; reload when the current operation finishes");
  setOptions(std::move(packet));
}
void LibraryWindow::rejectOptionsSave(const OptionsSubmission& request) {
  auto packet = std::make_shared<OptionsSavePacket>(); packet->session = request.session;
  packet->request = request.request; packet->revision = request.revision;
  packet->failure = tr("No settings result was received; reload settings before retrying");
  setOptionsSaved(std::move(packet));
}
void LibraryWindow::openScheduler() {
  if (closing_ || !columnSession_) { return; }
  if (schedulerDialog_) { schedulerDialog_->show(); schedulerDialog_->raise(); schedulerDialog_->activateWindow(); return; }
  schedulerDialog_ = new SchedulerDialog(this);
  schedulerDialog_->setAttribute(Qt::WA_DeleteOnClose);
  schedulerDialog_->setWindowModality(Qt::WindowModal);
  schedulerDialog_->beginSession(columnSession_);
  schedulerDialog_->setBusy(busy_);
  connect(schedulerDialog_, &SchedulerDialog::loadRequested, this, [this](SchedulerRequest request) {
    pendingSchedulerLoad_ = request; queueScheduler();
  });
  connect(schedulerDialog_, &SchedulerDialog::saveRequested, this, [this](SchedulerSubmission submission) {
    activeSchedulerSave_ = submission;
    emit schedulerSaveRequested(std::move(submission));
  });
  connect(schedulerDialog_, &QObject::destroyed, this, [this] {
    pendingSchedulerLoad_.reset(); activeSchedulerLoad_.reset(); activeSchedulerSave_.reset();
  });
  schedulerDialog_->show();
}
void LibraryWindow::queueScheduler() {
  if (!pendingSchedulerLoad_ || busy_ || closing_ || schedulerDispatchQueued_) { return; }
  schedulerDispatchQueued_ = true;
  QTimer::singleShot(0, this, [this] {
    schedulerDispatchQueued_ = false;
    if (!pendingSchedulerLoad_ || busy_ || closing_ || !schedulerDialog_) { return; }
    const auto request = *pendingSchedulerLoad_;
    pendingSchedulerLoad_.reset(); activeSchedulerLoad_ = request;
    emit schedulerRequested(request);
  });
}
void LibraryWindow::setScheduler(SchedulerPresentation packet) {
  if (!packet || !schedulerDialog_ || !activeSchedulerLoad_
      || packet->session != activeSchedulerLoad_->session || packet->request != activeSchedulerLoad_->request) { return; }
  activeSchedulerLoad_.reset(); schedulerDialog_->setSnapshot(std::move(packet));
}
void LibraryWindow::setSchedulerSaved(SchedulerSavePresentation packet) {
  if (!packet || !schedulerDialog_ || !activeSchedulerSave_
      || packet->session != activeSchedulerSave_->session || packet->request != activeSchedulerSave_->request
      || packet->revision != activeSchedulerSave_->revision) { return; }
  activeSchedulerSave_.reset(); schedulerDialog_->setSaveResult(std::move(packet));
}
void LibraryWindow::rejectSchedulerRequest(SchedulerRequest request) {
  auto packet = std::make_shared<SchedulerPacket>(); packet->session = request.session; packet->request = request.request;
  packet->failure = tr("Schedules could not be loaded; reload when the current operation finishes");
  setScheduler(std::move(packet));
}
void LibraryWindow::rejectSchedulerSave(const SchedulerSubmission& request) {
  auto packet = std::make_shared<SchedulerSavePacket>(); packet->session = request.session;
  packet->request = request.request; packet->revision = request.revision;
  packet->failure = tr("No schedule result was received; reload schedules before retrying");
  setSchedulerSaved(std::move(packet));
}
void LibraryWindow::setIntakeDefaults(quint64 session, QString directory) {
  if (!closing_ && session && session == columnSession_) { defaultSaveDirectory_ = std::move(directory); }
}

#include "file_operations_window.inc"
} // namespace biglybtc::ui

void biglybtc::ui::LibraryWindow::requestHybridCompanion(bool selectV2) {
  if(busy_||closing_||offlinePreview_||!columnSession_||updating_){return;}
  const auto targets=selectedDownloadTargets();
  if(!targets||targets->size()!=1||!model_->accepts(targets->front())){return;}
  const auto source=targets->front();
  emit hybridCompanionRequested(source,selectV2); // Last receiver access; direct slots may delete/rebind.
}

namespace biglybtc::ui {
void LibraryWindow::showChats(){
 if(closing_){return;}if(!chatPage_){chatPage_=new ChatPage(mainTabs_);auto* page=chatPage_.data();page->setSnapshot(chatSnapshot_);mainTabs_->addTab(page,tr("Public Chat Rooms"));
  connect(page,&ChatPage::joinRequested,this,&LibraryWindow::chatJoinRequested);connect(page,&ChatPage::selectionRequested,this,[this](QString key){pendingChatSelection_=key;QPointer<LibraryWindow> guard(this);emit chatSelectRequested(key);if(guard){updateChatReadContext();}});connect(page,&ChatPage::leaveRequested,this,&LibraryWindow::chatLeaveRequested);connect(page,&ChatPage::sendRequested,this,&LibraryWindow::chatSendRequested);connect(page,&ChatPage::optionsRequested,this,&LibraryWindow::chatOptionsRequested);connect(page,&ChatPage::nicknameModeRequested,this,&LibraryWindow::chatNicknameModeRequested);connect(page,&ChatPage::nicknameRequested,this,&LibraryWindow::chatNicknameRequested);
 }mainTabs_->setCurrentWidget(chatPage_);updateChatReadContext();
}
void LibraryWindow::setChatSnapshot(ChatViewSnapshot value){
 if(closing_||value.generation<chatSnapshot_.generation||(value.generation==chatSnapshot_.generation&&value.sequence<chatSnapshot_.sequence)){return;}
 if(value.generation!=chatSnapshot_.generation){pendingChatSelection_.clear();}chatSnapshot_=std::move(value);
 if(pendingChatSelection_==chatSnapshot_.selected){pendingChatSelection_.clear();}
 if(chatPage_){chatPage_->setSnapshot(chatSnapshot_);}if(azureusIndicator_){azureusIndicator_->setToolTip(chatSnapshot_.dhtStatus);}updateChatIndicator();updateChatReadContext();
}
void LibraryWindow::setDownloadChatSnapshot(quint64 session,QString hash,ChatViewSnapshot value){if(closing_||session!=columnSession_||!torrentDetailPages_){return;}torrentDetailPages_->setDownloadChat(session,std::move(hash),std::move(value));updateChatReadContext();}
void LibraryWindow::updateChatIndicator(){
 if(!chatIndicator_){return;}qint64 unread=0;int rooms=0;std::set<QString> seen;for(const auto* rows:{&chatSnapshot_.rooms,&chatSnapshot_.notifications}){for(const auto& room:*rows){if(room.unread>0&&seen.insert(room.key).second){unread+=room.unread;++rooms;}}}
 chatIndicator_->setProperty("unreadCount",unread);chatIndicator_->setText(unread?tr("Chats (%1)").arg(unread):tr("Chats"));chatIndicator_->setToolTip(unread?tr("%1 unread messages in %2 Public chat rooms").arg(unread).arg(rooms):tr("Open Public chat rooms"));
 chatIndicator_->setIcon(QIcon::fromTheme(unread?QStringLiteral("mail-unread"):QStringLiteral("mail-read"),style()->standardIcon(QStyle::SP_MessageBoxInformation)));
}
void LibraryWindow::updateChatReadContext(bool inactive){
 QString context;
 if(!inactive&&!closing_&&isVisible()&&!isMinimized()&&QApplication::activeWindow()==this&&chatPage_&&mainTabs_->currentWidget()==chatPage_&&pendingChatSelection_.isEmpty()){
   const auto room=std::ranges::find(chatSnapshot_.rooms,chatSnapshot_.selected,&ChatRoomView::key);if(room!=chatSnapshot_.rooms.end()&&room->joined){context=room->key;}
 }
 if(context.isEmpty()&&!inactive&&!closing_&&isVisible()&&!isMinimized()&&QApplication::activeWindow()==this&&torrentDetailPages_&&mainTabs_->currentWidget()&&!mainTabs_->currentWidget()->property("BiglyBTC.torrentDetailsHash").toString().isEmpty()){context=torrentDetailPages_->readingRoom();}
 if(context==chatReadingContext_){return;}chatReadingContext_=context;emit chatReadingChanged(context);
}
void LibraryWindow::chatSendFinished(QString key,QString text,bool accepted){if(torrentDetailPages_){auto* page=torrentDetailPages_->chatPage();if(accepted){page->submissionAccepted(key,text);}else{page->submissionFailed(key);}}if(chatPage_){if(accepted){chatPage_->submissionAccepted(key,text);}else{chatPage_->submissionFailed(key);}}}
}

namespace biglybtc::ui {
void LibraryWindow::showSwarmDiscoveries(){
 if(closing_){return;}if(!swarmPage_){swarmPage_=new SwarmDiscoveriesPage(mainTabs_);auto* p=swarmPage_.data();p->setSnapshot(swarmSnapshot_);mainTabs_->addTab(p,tr("Swarm Discoveries"));
 connect(p,&SwarmDiscoveriesPage::refreshRequested,this,&LibraryWindow::swarmRefreshRequested);connect(p,&SwarmDiscoveriesPage::discoverRequested,this,&LibraryWindow::swarmDiscoverRequested);connect(p,&SwarmDiscoveriesPage::searchRequested,this,&LibraryWindow::swarmSearchRequested);connect(p,&SwarmDiscoveriesPage::cancelSearchRequested,this,&LibraryWindow::swarmCancelSearchRequested);connect(p,&SwarmDiscoveriesPage::lookupRequested,this,&LibraryWindow::swarmLookupRequested);connect(p,&SwarmDiscoveriesPage::ratingRequested,this,&LibraryWindow::swarmRatingRequested);connect(p,&SwarmDiscoveriesPage::settingsRequested,this,&LibraryWindow::swarmSettingsRequested);connect(p,&SwarmDiscoveriesPage::readRequested,this,&LibraryWindow::swarmReadRequested);connect(p,&SwarmDiscoveriesPage::removeRequested,this,&LibraryWindow::swarmRemoveRequested);connect(p,&SwarmDiscoveriesPage::resetDeletedRequested,this,&LibraryWindow::swarmResetDeletedRequested);connect(p,&SwarmDiscoveriesPage::layoutRequested,this,&LibraryWindow::swarmLayoutRequested);
 }mainTabs_->setCurrentWidget(swarmPage_);emit swarmRefreshRequested();
}
void LibraryWindow::setSwarmSnapshot(SwarmViewSnapshot value){if(closing_||value.generation<swarmSnapshot_.generation||(value.generation==swarmSnapshot_.generation&&value.sequence<swarmSnapshot_.sequence)){return;}swarmSnapshot_=std::move(value);if(swarmPage_){swarmPage_->setSnapshot(swarmSnapshot_);}}
}

void biglybtc::ui::LibraryWindow::setFileMerge(FileMergePresentation p){if(!p||!p->session||p->session!=columnSession_||(latestFileMerge_&&p->session<latestFileMerge_->session)){return;}latestFileMerge_=std::move(p);if(statisticsPage_){statisticsPage_->setFileMerge(latestFileMerge_);}}
