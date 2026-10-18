#include "library_window.hpp"
#include "classic_filter.hpp"
#include "classic_columns.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
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
#include <QVBoxLayout>

#include <limits>

namespace biglybtc::ui {
namespace {
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
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setColumnWidth(1, 100);
    table->setColumnWidth(3, 85);
    table->setColumnWidth(9, 180);
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
  auto* incompleteColumns = new ClassicColumns(*incompleteTable_, false);
  auto* completeColumns = new ClassicColumns(*completeTable_, true);
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
  filesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  filesTable_->setAlternatingRowColors(true);
  filesTable_->verticalHeader()->hide();
  filesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  auto* general = new QWidget(detailTabs_);
  auto* form = new QFormLayout(general);
  form->addRow(tr("Name:"), generalName_);
  form->addRow(tr("State:"), generalState_);
  form->addRow(tr("Size (bytes):"), generalSize_);
  form->addRow(tr("Progress:"), generalProgress_);
  form->addRow(tr("Received / sent (bytes):"), generalReceived_);
  form->addRow(tr("Hash:"), generalHash_);
  detailTabs_->addTab(general, tr("General"));
  detailTabs_->addTab(filesTable_, tr("Files"));
  for (const auto& name : {tr("Trackers"), tr("Peers"), tr("Pieces")}) {
    const auto index = detailTabs_->addTab(new QWidget(detailTabs_), name);
    detailTabs_->setTabEnabled(index, false);
    detailTabs_->setTabToolTip(index, tr("This view is not available in the preview yet."));
  }
  connect(detailTabs_, &QTabWidget::currentChanged, this, [this](int index) {
    if (index == 1) { suppressAutomaticDetails_ = false; requestDetails(); }
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
  layout->addLayout(header);
  auto* content = new QSplitter(Qt::Vertical, library);
  content->setChildrenCollapsible(false);
  content->addWidget(splitter_);
  content->addWidget(detailTabs_);
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
  statusBar()->addWidget(message_, 1);
  statusBar()->addPermanentWidget(previewStatus_);
  auto* file = menuBar()->addMenu(tr("&File"));
  auto* transfers = menuBar()->addMenu(tr("&Transfers"));
  auto* torrent = menuBar()->addMenu(tr("T&orrent"));
  auto* view = menuBar()->addMenu(tr("&View"));
  auto* tools = menuBar()->addMenu(tr("&Tools"));
  auto* plugins = menuBar()->addMenu(tr("&Plugins"));
  auto* community = menuBar()->addMenu(tr("&Community"));
  auto* help = menuBar()->addMenu(tr("&Help"));
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
  toolbar->addAction(unavailable(file, tr("Add"), QStringLiteral("open"), QStyle::SP_DialogOpenButton));
  toolbar->addAction(unavailable(tools, tr("Search"), QStringLiteral("search"), QStyle::SP_FileDialogContentsView));
  toolbar->addSeparator();
  toolbar->addAction(unavailable(torrent, tr("Launch"), QStringLiteral("launch"), QStyle::SP_MediaPlay));
  for (const auto& item : {std::pair{tr("Top"), QStringLiteral("top")}, {tr("Up"), QStringLiteral("up")},
      {tr("Down"), QStringLiteral("down")}, {tr("Bottom"), QStringLiteral("bottom")}}) {
    toolbar->addAction(unavailable(torrent, item.first, item.second,
        item.second == QStringLiteral("top") || item.second == QStringLiteral("up") ? QStyle::SP_ArrowUp : QStyle::SP_ArrowDown));
  }
  toolbar->addSeparator();
  toolbar->addAction(unavailable(torrent, tr("Start / Stop"), QStringLiteral("start-stop"), QStyle::SP_MediaPlay));
  toolbar->addAction(unavailable(torrent, tr("Delete"), QStringLiteral("remove"), QStyle::SP_TrashIcon));
  unavailable(transfers, tr("Start All"), QStringLiteral("start-all"), QStyle::SP_MediaPlay);
  unavailable(transfers, tr("Stop All"), QStringLiteral("stop-all"), QStyle::SP_MediaStop);
  unavailable(tools, tr("Options…"), QStringLiteral("options"), QStyle::SP_FileDialogDetailedView);
  unavailable(plugins, tr("Plugin management…"), QStringLiteral("plugins"), QStyle::SP_FileDialogDetailedView);
  unavailable(community, tr("Forums"), QStringLiteral("forums"), QStyle::SP_DialogHelpButton);
  auto* about = help->addAction(tr("About BiglyBTC"));
  connect(about, &QAction::triggered, this, [this] {
    QMessageBox::about(this, tr("About BiglyBTC"), tr("BiglyBTC\nClassic desktop preview\n\nLibrary and file details use generated sample data.\nThe full client is under development."));
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
  unavailable(view, tr("Remote Pairing…"), QStringLiteral("remote-pairing"), QStyle::SP_DriveNetIcon);
  unavailable(view, tr("Subscriptions"), QStringLiteral("subscriptions"), QStyle::SP_FileDialogListView);
  view->addSeparator();
  view->addMenu(incompleteColumns->menu());
  view->addMenu(completeColumns->menu());
  view->addSeparator();
  refresh_ = view->addAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("&Refresh"));
  refresh_->setObjectName(QStringLiteral("classic-refresh"));
  refresh_->setShortcut(QKeySequence(Qt::Key_F5));
  connect(refresh_, &QAction::triggered, this, [this] {
    if (closing_) { return; }
    if (onSavedServicePage()) { refreshSavedServices(); return; }
    if (mainTabs_->currentWidget() != torrentsPage_ || busy_) { return; }
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
    const bool showing = detailTabs_->currentIndex() == 1;
    suppressAutomaticDetails_ = false;
    if (!showing) { detailsRequestedFor_.reset(); }
    QPointer<LibraryWindow> guard(this);
    detailTabs_->setCurrentIndex(1);
    if (guard) { requestDetails(showing); }
  });
  connect(continue_, &QAction::triggered, this, [this] {
    const auto target = actionTarget();
    if (!busy_ && !closing_ && target && model_->canContinue(*target)) { emit continueRequested(*target); }
  });
  connect(cancel_, &QAction::triggered, this, [this] {
    suppressAutomaticDetails_ = true;
    emit cancelRequested();
  });
  connect(model_, &QAbstractItemModel::dataChanged, this, [this] {
    if (!updating_) { updateActions(); updateGeneral(); updateCounts(); }
  });
  connect(mainTabs_, &QTabWidget::tabCloseRequested, this, &LibraryWindow::closePage);
  connect(mainTabs_, &QTabWidget::currentChanged, this, [this] {
    QPointer<LibraryWindow> guard(this);
    updateActions();
    if (guard && mainTabs_->currentWidget() == torrentsPage_) { selectionChanged(); }
  });
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
    hostingPage_ = new SavedHostingPage(mainTabs_);
    mainTabs_->addTab(hostingPage_.data(), tr("My Tracker"));
    if (!guard) { return; }
  }
  mainTabs_->setCurrentWidget(hostingPage_.data());
  if (!guard) { return; }
  hostingPage_->table().setFocus(Qt::ShortcutFocusReason);
  if (guard && fresh) { refreshSavedServices(); }
}
void LibraryWindow::showMyShares() {
  if (closing_) { return; }
  QPointer<LibraryWindow> guard(this);
  const bool fresh = !sharesPage_;
  if (fresh) {
    sharesPage_ = new SavedSharesPage(mainTabs_);
    mainTabs_->addTab(sharesPage_.data(), tr("My Shares"));
    if (!guard) { return; }
  }
  mainTabs_->setCurrentWidget(sharesPage_.data());
  if (!guard) { return; }
  sharesPage_->table().setFocus(Qt::ShortcutFocusReason);
  if (guard && fresh) { refreshSavedServices(); }
}
void LibraryWindow::closePage(int index) {
  if (closing_) { return; }
  QPointer<QWidget> page(mainTabs_->widget(index));
  if (!page) { return; }
  const bool retainedLibrary = page == torrentsPage_;
  if (!retainedLibrary) {
    if (page == hostingPage_.data()) { hostingPage_.clear(); }
    else if (page == sharesPage_.data()) { sharesPage_.clear(); }
    else { return; }
  }
  QPointer<LibraryWindow> guard(this);
  mainTabs_->removeTab(index);
  if (!guard) { return; }
  // Keep the library widgets and selection owned for View > My Torrents.
  if (!retainedLibrary && page) { page->deleteLater(); }
  if (pendingSavedRequest_ && !hasLoadingServices(*pendingSavedRequest_)) { pendingSavedRequest_.reset(); }
  updateActions();
  if (guard) { queueSavedServices(); }
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
  if (savedDispatchQueued_ || closing_ || busy_ || activeSavedRequest_ || !pendingSavedRequest_) { return; }
  savedDispatchQueued_ = true;
  QTimer::singleShot(0, this, [this] {
    savedDispatchQueued_ = false;
    if (closing_ || busy_ || activeSavedRequest_ || !pendingSavedRequest_) { return; }
    const auto request = *pendingSavedRequest_;
    pendingSavedRequest_.reset();
    if (!hasLoadingServices(request)) { return; }
    activeSavedRequest_ = request;
    emit savedServicesRequested(request);
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
  if (hostingPage_) { applySavedResult(*hostingPage_, packet->request, packet->hosting); }
  if (!guard) { return; }
  if (sharesPage_) { applySavedResult(*sharesPage_, packet->request, packet->sharing); }
  if (!guard) { return; }
  queueSavedServices();
}

void LibraryWindow::setOfflinePreview(bool offline) {
  previewStatus_->setText(offline ? tr("Offline preview") : tr("Local test"));
}
bool LibraryWindow::eventFilter(QObject* watched, QEvent* event) {
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
  const auto rows = activeTable_->selectionModel()->selectedRows(0);
  const auto current = activeTable_->currentIndex();
  if (rows.size() != 1 || !current.isValid() || current.row() != rows.front().row()) { return std::nullopt; }
  const auto source = proxy(activeTable_)->mapToSource(rows.front());
  return source.isValid() ? model_->targetAt(source.row()) : std::nullopt;
}
std::optional<Target> LibraryWindow::actionTarget() const {
  if (mainTabs_->currentWidget() != torrentsPage_) { return std::nullopt; }
  const auto target = selected();
  return contextOpen_ && target != contextTarget_ ? std::nullopt : target;
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
  if (hashes.size() == 1) {
    const auto source = model_->index(model_->rowFor(hashes.front()), 0);
    for (auto* table : {incompleteTable_, completeTable_}) {
      if (proxy(table)->mapFromSource(source).isValid()) { activate(table); break; }
    }
  }
  for (auto* table : {incompleteTable_, completeTable_}) { table->selectionModel()->clear(); }
  QModelIndex first;
  QItemSelection selection;
  for (const auto& hash : hashes) {
    const auto index = proxy(activeTable_)->mapFromSource(model_->index(model_->rowFor(hash), 0));
    if (!index.isValid()) { continue; }
    selection.select(index, index);
    if (!first.isValid()) { first = index; }
  }
  if (!first.isValid() && initial) {
    if (incompleteTable_->model()->rowCount() == 0) { activate(completeTable_); }
    first = activeTable_->model()->index(0, 0);
    if (first.isValid()) { selection.select(first, first); }
  }
  activeTable_->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  activeTable_->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
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
  cancel_->setEnabled(busy_ && !closing_);
  refresh_->setEnabled(!closing_ && (onSavedServicePage() || (!busy_ && mainTabs_->currentWidget() == torrentsPage_)));
  files_->setEnabled(!busy_ && !closing_ && target.has_value());
  padding_->setEnabled(!closing_ && mainTabs_->currentWidget() == torrentsPage_);
  closeTab_->setEnabled(!closing_ && (mainTabs_->currentWidget() == torrentsPage_ || onSavedServicePage()));
}
void LibraryWindow::selectionChanged() {
  if (updating_) { return; }
  updateActions();
  updateGeneral();
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
  if (mainTabs_->currentWidget() != torrentsPage_ || busy_ || closing_ || (suppressAutomaticDetails_ && !force)) { return; }
  const auto target = selected();
  if (!target || (!force && detailsRequestedFor_ == target)) { return; }
  detailsRequestedFor_ = target;
  emit detailsRequested(*target);
}
void LibraryWindow::setPresentation(const Presentation& snapshot) {
  const auto hashes = selectedHashes();
  const auto previousFile = filesTable_->currentIndex();
  const auto fileIndex = previousFile.isValid()
      ? filesModel_->originalIndex(filesProxy_->mapToSource(previousFile).row()) : std::nullopt;
  updating_ = true;
  const bool applied = model_->apply(snapshot);
  if (applied) { restoreSelection(hashes, !presented_); presented_ = true; }
  updating_ = false;
  if (!applied) { return; }
  if (hashes.size() == 1 && selected() && hashes.front() == selected()->hash && fileIndex) { restoreFileIndex_ = fileIndex; }
  setMessage(model_->rowCount() == 1 ? tr("1 torrent") : tr("%1 torrents").arg(model_->rowCount()));
  QPointer<LibraryWindow> guard(this);
  selectionChanged();
  if (guard) { updateCounts(); }
}
void LibraryWindow::setTelemetry(const Telemetry& update) {
  const auto hashes = selectedHashes();
  updating_ = true;
  const bool applied = model_->apply(update);
  if (applied) { restoreSelection(hashes); }
  updating_ = false;
  if (applied) {
    setMessage(update.state);
    QPointer<LibraryWindow> guard(this);
    selectionChanged();
    if (guard) { updateCounts(); }
  }
}
void LibraryWindow::setDetails(const Details& details) {
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
  busy_ = value;
  updateActions();
  if (busy_) { return; }
  QPointer<LibraryWindow> guard(this);
  if (activeSavedRequest_) {
    const auto request = *activeSavedRequest_;
    activeSavedRequest_.reset();
    failSavedServices(request, tr("Saved configuration request did not complete"));
    if (!guard) { return; }
  }
  if (closing_) {
    pendingSavedRequest_.reset();
    QTimer::singleShot(0, this, &QWidget::close);
    return;
  }
  queueSavedServices();
  if (mainTabs_->currentWidget() == torrentsPage_
      && (detailTabs_->currentIndex() == 1 || (selected() && !model_->hasDetails(*selected())))) {
    QTimer::singleShot(0, this, [this] { requestDetails(); });
  }
}
void LibraryWindow::closeEvent(QCloseEvent* event) {
  closing_ = true;
  pendingSavedRequest_.reset();
  QPointer<LibraryWindow> guard(this);
  updateActions();
  if (!guard) { return; }
  if (busy_) {
    setMessage(tr("Finishing current operation…"));
    event->ignore();
    emit cancelRequested();
    return;
  }
  activeSavedRequest_.reset();
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
  contextOpen_ = true;
  updateActions();
  QPointer<QMenu> menu(new QMenu(this));
  menu->addAction(findChild<QAction*>(QStringLiteral("classic-launch")));
  menu->addAction(findChild<QAction*>(QStringLiteral("classic-start-stop")));
  menu->addAction(findChild<QAction*>(QStringLiteral("classic-remove")));
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
} // namespace biglybtc::ui
