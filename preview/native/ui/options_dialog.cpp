#include "options_dialog.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <set>

namespace biglybtc::ui {
namespace {
struct SignalPause {
  QPointer<QObject> object;
  bool previous;
  explicit SignalPause(QObject* target) : object(target), previous(target->blockSignals(true)) {}
  ~SignalPause() { if (object) { object->blockSignals(previous); } }
};
QString text(std::string_view value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
QString effect(core::SettingsApplicability value) {
  switch (value) {
    case core::SettingsApplicability::Live: return QStringLiteral("Current-session setting");
    case core::SettingsApplicability::NextAdmission: return QStringLiteral("New downloads only");
    case core::SettingsApplicability::Restart: return QStringLiteral("Restart required");
    case core::SettingsApplicability::OriginalOnly: return QStringLiteral("Saved only; not supported by this engine");
  }
  return {};
}
QString fieldState(OptionsFieldState value) {
  switch (value) {
    case OptionsFieldState::Defaulted: return QStringLiteral("Default; not stored");
    case OptionsFieldState::Stored: return QStringLiteral("Saved value");
    case OptionsFieldState::InvalidType: return QStringLiteral("Invalid saved type; unchanged until edited");
    case OptionsFieldState::OutOfRange: return QStringLiteral("Saved value outside the supported range; unchanged until edited");
    case OptionsFieldState::UnavailableDefault: return QStringLiteral("No explicit value; choose a value to save it");
  }
  return {};
}
}
struct OptionsDialog::Impl {
  struct Editor { QPointer<QWidget> input; QPointer<QLabel> state; };
  struct Changes { std::vector<core::SettingChange> values; QString error; };
  OptionsDialog* q;
  quint64 session = 0, highestSession = 0, request = 0, activeRequest = 0, revision = 0, serial = 0;
  bool loading = false, saving = false, busy = false, dirty = false, rendering = false;
  bool loadWanted = false, closeAfterSave = false;
  OptionsPresentation baseline;
  std::set<core::SettingId> touched;
  std::array<Editor, static_cast<std::size_t>(core::SettingId::Count)> editors{};
  QPointer<QLabel> status, restart;
  QPointer<QPushButton> apply, ok, cancel, reload;
  QPointer<QMessageBox> question;
  explicit Impl(OptionsDialog* owner) : q(owner) {}
  static bool current(const QPointer<OptionsDialog>& self, quint64 token) {
    return self && self->impl_->serial == token;
  }
  const OptionsField* field(core::SettingId id) const {
    if (!baseline || !baseline->snapshot) { return nullptr; }
    const auto& values = baseline->snapshot->fields;
    const auto found = std::find_if(values.begin(), values.end(), [id](const auto& item) { return item.id == id; });
    return found == values.end() ? nullptr : &*found;
  }
  bool message(const QString& value) {
    const QPointer<OptionsDialog> self(q); const auto token = serial;
    const auto label = status;
    if (!label) { return false; }
    label->setText(value.left(8192));
    return current(self, token) && label;
  }
  bool actions() {
    const QPointer<OptionsDialog> self(q); const auto token = serial;
    const bool ready = session && baseline && !loading && !saving && !busy;
    const std::array<std::pair<QPointer<QPushButton>, bool>, 4> buttons{{
      {apply, ready && dirty}, {ok, ready}, {cancel, !saving}, {reload, session && !loading && !saving && !busy && !dirty}}};
    for (const auto& [button, enabled] : buttons) {
      if (!button) { continue; }
      button->setEnabled(enabled);
      if (!current(self, token)) { return false; }
    }
    for (const auto& editor : editors) {
      const auto target = editor.input;
      if (!target) { continue; }
      target->setEnabled(ready);
      if (!current(self, token)) { return false; }
    }
    return true;
  }
  Changes changes() const {
    Changes result;
    for (const auto& descriptor : core::applicationSettingsCatalog()) {
      if (!touched.contains(descriptor.id)) { continue; }
      const auto* original = field(descriptor.id);
      const auto target = editors[static_cast<std::size_t>(descriptor.id)].input;
      std::optional<core::SettingValue> value;
      if (descriptor.kind == core::SettingKind::Boolean) {
        const auto* checkbox = qobject_cast<QCheckBox*>(target.data());
        if (checkbox && checkbox->checkState() != Qt::PartiallyChecked) { value = checkbox->isChecked(); }
      } else {
        const auto* line = qobject_cast<QLineEdit*>(target.data());
        if (line) {
          if (descriptor.kind == core::SettingKind::Integer) {
            bool valid = false; const auto number = line->text().toLongLong(&valid);
            if (valid) { value = static_cast<std::int64_t>(number); }
          } else { value = line->text().toUtf8().toStdString(); }
        }
      }
      if (value && original && original->value && *value == *original->value) { continue; }
      if (!value || !core::validApplicationSettingValue(descriptor, *value)) {
        result.error = QStringLiteral("Check %1.").arg(text(descriptor.label)); return result;
      }
      result.values.push_back({descriptor.id, std::move(*value)});
    }
    return result;
  }
  void edited(core::SettingId id) {
    if (rendering || !baseline || !session || loading || saving || busy) { return; }
    ++serial; touched.insert(id);
    const auto value = changes(); dirty = !value.values.empty() || !value.error.isEmpty();
    if (!message(value.error.isEmpty() ? (dirty ? QStringLiteral("Unsaved changes") : QStringLiteral("No changes")) : value.error)) { return; }
    static_cast<void>(actions());
  }
  bool render(OptionsPresentation packet, bool requiresRestart) {
    const QPointer<OptionsDialog> self(q); const auto token = ++serial;
    baseline = std::move(packet); revision = baseline->revision;
    touched.clear(); dirty = false; rendering = true;
    for (const auto& descriptor : core::applicationSettingsCatalog()) {
      const auto* value = field(descriptor.id);
      const auto editor = editors[static_cast<std::size_t>(descriptor.id)];
      if (!value || !editor.input || !editor.state) { rendering = false; return false; }
      const auto target = editor.input;
      const SignalPause blocked(target);
      if (auto* checkbox = qobject_cast<QCheckBox*>(target.data())) {
        const auto* boolean = value->value ? std::get_if<bool>(&*value->value) : nullptr;
        checkbox->setTristate(!boolean);
        if (!current(self, token) || !target) { return false; }
        checkbox->setCheckState(boolean ? (*boolean ? Qt::Checked : Qt::Unchecked) : Qt::PartiallyChecked);
      } else if (auto* line = qobject_cast<QLineEdit*>(target.data())) {
        QString display;
        if (value->value) {
          if (const auto* number = std::get_if<std::int64_t>(&*value->value)) { display = QString::number(static_cast<qlonglong>(*number)); }
          else if (const auto* string = std::get_if<std::string>(&*value->value)) { display = text(std::string_view(*string).substr(0, 4096)); }
        }
        line->setText(display);
      }
      if (!current(self, token) || !target) { return false; }
      if (!editor.state) { return false; }
      editor.state->setText(fieldState(value->state) + QStringLiteral(" · ") + effect(descriptor.applicability));
      if (!current(self, token) || !editor.state) { return false; }
    }
    rendering = false;
    if (restart) { restart->setText(requiresRestart ? QStringLiteral("Restart the application to use the saved connection settings.") : QString()); }
    if (!current(self, token)) { return false; }
    if (!message(QStringLiteral("Settings loaded"))) { return false; }
    return actions();
  }
  void maybeLoad() {
    if (!loadWanted || !session || loading || saving || busy || !q->isVisible()) { return; }
    if (request == std::numeric_limits<quint64>::max()) { static_cast<void>(message(QStringLiteral("Reopen Options to load settings."))); return; }
    ++serial; activeRequest = ++request; loadWanted = false; loading = true;
    const OptionsRequest query{session, activeRequest};
    if (!message(QStringLiteral("Loading settings…")) || !actions()) { return; }
    emit q->loadRequested(query); // May delete/rebind the dialog. No work after delivery.
  }
  void submit(bool close) {
    if (saving || loading || busy || !session || !baseline) { return; }
    const auto value = changes();
    if (!value.error.isEmpty()) { static_cast<void>(message(value.error)); return; }
    if (value.values.empty()) {
      if (close) { q->done(QDialog::Accepted); }
      return;
    }
    if (request == std::numeric_limits<quint64>::max()) { static_cast<void>(message(QStringLiteral("Reopen Options to save settings."))); return; }
    ++serial; activeRequest = ++request; saving = true; closeAfterSave = close;
    OptionsSubmission submission{session, activeRequest, revision, value.values};
    if (!message(QStringLiteral("Saving settings…")) || !actions()) { return; }
    emit q->saveRequested(std::move(submission)); // Value-owned; no work after callbacks.
  }
  void requestClose() {
    if (saving) { static_cast<void>(message(QStringLiteral("Wait for settings to finish saving."))); return; }
    if (!dirty) {
      const QPointer<OptionsDialog> self(q); q->invalidateSession();
      if (self && !self->session()) { self->done(QDialog::Rejected); }
      return;
    }
    if (question) { question->raise(); return; }
    auto* prompt = new QMessageBox(QMessageBox::Question, QStringLiteral("Unsaved settings"),
        QStringLiteral("Save your changes before closing Options?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, q);
    prompt->setObjectName(QStringLiteral("options-discard-confirmation"));
    prompt->setTextFormat(Qt::PlainText); prompt->setAttribute(Qt::WA_DeleteOnClose);
    prompt->setDefaultButton(QMessageBox::Cancel); question = prompt;
    const auto expectedSession = session;
    QObject::connect(prompt, &QMessageBox::finished, q, [self = QPointer<OptionsDialog>(q), expectedSession](int result) {
      if (!self || self->impl_->session != expectedSession) { return; }
      self->impl_->question.clear();
      if (result == QMessageBox::Save) { self->impl_->submit(true); }
      else if (result == QMessageBox::Discard) {
        self->invalidateSession();
        if (self && !self->session()) { self->done(QDialog::Rejected); }
      }
    });
    prompt->open();
  }
};

OptionsDialog::OptionsDialog(QWidget* parent) : QDialog(parent), impl_(std::make_unique<Impl>(this)) {
  setObjectName(QStringLiteral("options-dialog")); setWindowTitle(QStringLiteral("Options"));
  resize(920, 650); setMinimumSize(680, 420);
  auto* layout = new QVBoxLayout(this); auto* body = new QHBoxLayout;
  auto* tree = new QTreeWidget(this); tree->setObjectName(QStringLiteral("options-sections"));
  tree->setHeaderHidden(true); tree->setMaximumWidth(210); tree->setMinimumWidth(160);
  auto* pages = new QStackedWidget(this); pages->setObjectName(QStringLiteral("options-pages"));
  const std::array<QString, 6> names{QStringLiteral("Connection"), QStringLiteral("Advanced"), QStringLiteral("Proxy"),
      QStringLiteral("Transfer"), QStringLiteral("Queue"), QStringLiteral("Files")};
  std::array<QFormLayout*, 6> forms{}; QTreeWidgetItem* connection = nullptr;
  for (std::size_t index = 0; index < names.size(); ++index) {
    auto* item = index == 1 || index == 2 ? new QTreeWidgetItem(connection) : new QTreeWidgetItem(tree);
    item->setText(0, names[index]); item->setData(0, Qt::UserRole, static_cast<int>(index));
    if (index == 0) { connection = item; }
    auto* scroll = new QScrollArea(pages); scroll->setWidgetResizable(true);
    auto* page = new QWidget(scroll); auto* vertical = new QVBoxLayout(page);
    auto* heading = new QLabel(names[index], page); heading->setTextFormat(Qt::PlainText); vertical->addWidget(heading);
    forms[index] = new QFormLayout; forms[index]->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    forms[index]->setRowWrapPolicy(QFormLayout::WrapLongRows); vertical->addLayout(forms[index]); vertical->addStretch();
    scroll->setWidget(page); pages->addWidget(scroll);
  }
  connection->setExpanded(true); tree->setCurrentItem(connection);
  for (const auto& field : core::applicationSettingsCatalog()) {
    auto* container = new QWidget(pages); auto* column = new QVBoxLayout(container); column->setContentsMargins(0, 2, 0, 6);
    QWidget* editor = nullptr;
    if (field.kind == core::SettingKind::Boolean) {
      auto* checkbox = new QCheckBox(container); editor = checkbox;
      connect(checkbox, &QCheckBox::checkStateChanged, this, [this, id = field.id](Qt::CheckState) { impl_->edited(id); });
    } else {
      auto* line = new QLineEdit(container); editor = line;
      line->setMaxLength(field.kind == core::SettingKind::Integer ? 32 : static_cast<int>(std::max<std::size_t>(field.maximumBytes, 4096)));
      if (field.secret) { line->setEchoMode(QLineEdit::Password); }
      connect(line, &QLineEdit::textChanged, this, [this, id = field.id](const QString&) { impl_->edited(id); });
    }
    editor->setObjectName(QStringLiteral("options-field-%1").arg(static_cast<int>(field.id)));
    editor->setAccessibleName(text(field.label)); editor->setToolTip(text(field.help));
    auto* state = new QLabel(container); state->setTextFormat(Qt::PlainText); state->setWordWrap(true);
    state->setObjectName(QStringLiteral("options-field-state-%1").arg(static_cast<int>(field.id)));
    column->addWidget(editor); column->addWidget(state);
    auto* label = new QLabel(text(field.label), container); label->setTextFormat(Qt::PlainText); label->setBuddy(editor);
    forms[static_cast<std::size_t>(field.section)]->addRow(label, container);
    impl_->editors[static_cast<std::size_t>(field.id)] = {editor, state}; editor->setEnabled(false);
  }
  connect(tree, &QTreeWidget::currentItemChanged, pages, [pages](QTreeWidgetItem* item, QTreeWidgetItem*) {
    if (item) { pages->setCurrentIndex(item->data(0, Qt::UserRole).toInt()); }
  });
  body->addWidget(tree); body->addWidget(pages, 1); layout->addLayout(body, 1);
  auto* restart = new QLabel(this); restart->setObjectName(QStringLiteral("options-restart-status"));
  restart->setTextFormat(Qt::PlainText); restart->setWordWrap(true); impl_->restart = restart; layout->addWidget(restart);
  auto* status = new QLabel(QStringLiteral("Choose a profile to load settings."), this); status->setObjectName(QStringLiteral("options-status"));
  status->setTextFormat(Qt::PlainText); status->setWordWrap(true); impl_->status = status; layout->addWidget(status);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  auto* reload = buttons->addButton(QStringLiteral("Reload"), QDialogButtonBox::ResetRole); impl_->reload = reload;
  impl_->apply = buttons->button(QDialogButtonBox::Apply); impl_->ok = buttons->button(QDialogButtonBox::Ok);
  impl_->cancel = buttons->button(QDialogButtonBox::Cancel);
  impl_->apply->setObjectName(QStringLiteral("options-apply")); impl_->ok->setObjectName(QStringLiteral("options-ok"));
  impl_->cancel->setObjectName(QStringLiteral("options-cancel")); reload->setObjectName(QStringLiteral("options-reload"));
  connect(impl_->apply, &QPushButton::clicked, this, [this] { impl_->submit(false); });
  connect(impl_->ok, &QPushButton::clicked, this, &OptionsDialog::accept);
  connect(impl_->cancel, &QPushButton::clicked, this, &OptionsDialog::reject);
  connect(reload, &QPushButton::clicked, this, [this] { impl_->loadWanted = true; impl_->maybeLoad(); });
  layout->addWidget(buttons); static_cast<void>(impl_->actions());
}
OptionsDialog::~OptionsDialog() = default;
bool OptionsDialog::beginSession(quint64 value) {
  if (!value || value <= impl_->highestSession) { return false; }
  const QPointer<OptionsDialog> self(this);
  invalidateSession();
  if (!self || self->impl_->session || value <= self->impl_->highestSession) { return false; }
  impl_->session = value; impl_->highestSession = value; impl_->loadWanted = true;
  impl_->maybeLoad(); return true;
}
void OptionsDialog::invalidateSession() {
  ++impl_->serial; impl_->session = 0; impl_->baseline.reset(); impl_->revision = 0; impl_->touched.clear();
  impl_->dirty = false; impl_->loading = false; impl_->saving = false; impl_->rendering = false; impl_->closeAfterSave = false; impl_->loadWanted = false;
  const QPointer<OptionsDialog> self(this); const auto token = impl_->serial;
  if (impl_->question) { impl_->question->close(); }
  if (!self || self->impl_->serial != token) { return; }
  // Drop credentials and every old-profile field before a new read.
  for (const auto& editor : impl_->editors) {
    const auto target = editor.input;
    if (auto* line = qobject_cast<QLineEdit*>(target.data())) { line->clear(); }
    else if (auto* checkbox = qobject_cast<QCheckBox*>(target.data())) { checkbox->setCheckState(Qt::Unchecked); }
    if (!self || self->impl_->serial != token) { return; }
    const auto state = editor.state;
    if (state) { state->clear(); }
    if (!self || self->impl_->serial != token) { return; }
  }
  if (impl_->restart) { impl_->restart->clear(); }
  if (!self || self->impl_->serial != token) { return; }
  if (!impl_->message(QStringLiteral("No active profile"))) { return; }
  static_cast<void>(impl_->actions());
}
quint64 OptionsDialog::session() const noexcept { return impl_->session; }
quint64 OptionsDialog::revision() const noexcept { return impl_->revision; }
bool OptionsDialog::dirty() const noexcept { return impl_->dirty; }
bool OptionsDialog::pending() const noexcept { return impl_->loading || impl_->saving; }
void OptionsDialog::showEvent(QShowEvent* event) {
  const QPointer<OptionsDialog> self(this);
  QDialog::showEvent(event);
  if (!self) { return; }
  QMetaObject::invokeMethod(self, [self] { if (self) { self->impl_->maybeLoad(); } }, Qt::QueuedConnection);
}
void OptionsDialog::closeEvent(QCloseEvent* event) {
  const QPointer<OptionsDialog> self(this);
  impl_->requestClose();
  // An immediate approval must also satisfy QWidget::close callers, including
  // the parent window. A pending save or an open dirty prompt still vetoes it.
  if (!self || !self->isVisible() || !self->session()) { event->accept(); }
  else { event->ignore(); }
}
void OptionsDialog::accept() { impl_->submit(true); }
void OptionsDialog::reject() { impl_->requestClose(); }
void OptionsDialog::setBusy(bool value) {
  ++impl_->serial; impl_->busy = value;
  if (!impl_->actions()) { return; }
  impl_->maybeLoad();
}
bool OptionsDialog::setSnapshot(OptionsPresentation packet) {
  if (!packet || !impl_->session || packet->session != impl_->session || !impl_->loading
      || packet->request != impl_->activeRequest || packet->failure.size() > 8192) { return false; }
  if (packet->snapshot && (!packet->failure.isEmpty() || !packet->revision || packet->revision < impl_->revision || !validOptionsValues(*packet->snapshot))) { return false; }
  ++impl_->serial; impl_->loading = false;
  if (!packet->snapshot) {
    if (!impl_->message(packet->failure.isEmpty() ? QStringLiteral("Settings could not be loaded. Use Reload to retry.") : packet->failure)) { return true; }
    static_cast<void>(impl_->actions()); return true;
  }
  const bool restart = packet->restartRequired;
  static_cast<void>(impl_->render(std::move(packet), restart)); return true;
}
bool OptionsDialog::setSaveResult(OptionsSavePresentation packet) {
  if (!packet || !impl_->session || packet->session != impl_->session || !impl_->saving
      || packet->request != impl_->activeRequest || packet->revision != impl_->revision || packet->failure.size() > 8192) { return false; }
  if (packet->saved && (!packet->failure.isEmpty() || !packet->snapshot || !packet->snapshot->snapshot || !packet->snapshot->failure.isEmpty() || packet->snapshot->session != impl_->session
      || packet->snapshot->request != packet->request || packet->snapshot->revision <= impl_->revision
      || !validOptionsValues(*packet->snapshot->snapshot))) { return false; }
  ++impl_->serial; impl_->saving = false;
  const bool close = impl_->closeAfterSave; impl_->closeAfterSave = false;
  if (!packet->saved) {
    if (!impl_->message(packet->failure.isEmpty() ? QStringLiteral("Settings were not saved. Your changes are retained.") : packet->failure)) { return true; }
    static_cast<void>(impl_->actions()); return true;
  }
  const QPointer<OptionsDialog> self(this);
  if (!impl_->render(packet->snapshot, packet->restartRequired) || !self) { return true; }
  if (!impl_->message(QStringLiteral("Settings saved")) || !self) { return true; }
  if (close) { done(QDialog::Accepted); }
  return true;
}
} // namespace biglybtc::ui
