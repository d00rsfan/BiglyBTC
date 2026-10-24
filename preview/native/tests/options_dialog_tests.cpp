#include "options_dialog.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QtTest>

#include <functional>

namespace {
using namespace biglybtc;
using namespace biglybtc::ui;
QString id(core::SettingId value) { return QStringLiteral("options-field-%1").arg(static_cast<int>(value)); }
std::shared_ptr<OptionsValues> defaults() {
  auto values = std::make_shared<OptionsValues>();
  for (const auto& descriptor : core::applicationSettingsCatalog()) {
    auto value = core::applicationSettingDefault(descriptor);
    values->fields.push_back({descriptor.id, value ? OptionsFieldState::Defaulted : OptionsFieldState::UnavailableDefault, std::move(value)});
  }
  return values;
}
OptionsPresentation snapshot(OptionsRequest request, quint64 revision = 10, std::shared_ptr<OptionsValues> values = defaults()) {
  auto packet = std::make_shared<OptionsPacket>(); packet->session = request.session; packet->request = request.request;
  packet->revision = revision; packet->snapshot = std::move(values); return packet;
}
OptionsSavePresentation saved(const OptionsSubmission& submission, bool success, std::shared_ptr<OptionsValues> values = defaults(), bool restart = false) {
  auto result = std::make_shared<OptionsSavePacket>(); result->session = submission.session; result->request = submission.request;
  result->revision = submission.revision; result->saved = success; result->restartRequired = restart;
  if (success) { result->snapshot = snapshot({submission.session, submission.request}, submission.revision + 1, std::move(values)); }
  else { result->failure = QStringLiteral("Generated save conflict; draft retained"); }
  return result;
}
struct Harness {
  OptionsDialog dialog;
  std::vector<OptionsRequest> loads;
  std::vector<OptionsSubmission> saves;
  Harness() {
    QObject::connect(&dialog, &OptionsDialog::loadRequested, &dialog, [&](auto value) { loads.push_back(value); });
    QObject::connect(&dialog, &OptionsDialog::saveRequested, &dialog, [&](auto value) { saves.push_back(std::move(value)); });
  }
  QLineEdit* line(core::SettingId field) { return dialog.findChild<QLineEdit*>(id(field)); }
  QPushButton* button(const char* name) { return dialog.findChild<QPushButton*>(QString::fromLatin1(name)); }
  void show() { dialog.beginSession(5); dialog.show(); }
};
class EnabledCallback final : public QObject {
public:
  std::function<void()> callback;
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() == QEvent::EnabledChange && callback) {
      const auto call = std::move(callback); callback = {}; call();
    }
    return false;
  }
};
}

class OptionsDialogTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    qRegisterMetaType<OptionsRequest>(); qRegisterMetaType<OptionsSubmission>();
    qRegisterMetaType<OptionsPresentation>(); qRegisterMetaType<OptionsSavePresentation>();
  }
  void classicPagesMaskPasswordsAndLabelUnavailableFeatures() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1});
    auto values = defaults();
    values->fields[static_cast<std::size_t>(core::SettingId::ProxyPassword)] = {core::SettingId::ProxyPassword, OptionsFieldState::Stored, std::string("secret-for-mask")};
    QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.front(), 10, values)));
    auto* tree = h.dialog.findChild<QTreeWidget*>(QStringLiteral("options-sections")); QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 4); QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Connection"));
    QCOMPARE(tree->topLevelItem(0)->childCount(), 2); QCOMPARE(tree->topLevelItem(0)->child(1)->text(0), QStringLiteral("Proxy"));
    QCOMPARE(h.line(core::SettingId::ProxyPassword)->echoMode(), QLineEdit::Password);
    const auto* unsupported = h.dialog.findChild<QLabel*>(QStringLiteral("options-field-state-%1").arg(static_cast<int>(core::SettingId::AutoUpload)));
    QVERIFY(unsupported && unsupported->text().contains(QStringLiteral("not supported")));
    QVERIFY(h.line(core::SettingId::DefaultSavePath)->text().isEmpty()); QVERIFY(!h.dialog.dirty());
    QVERIFY(!h.button("options-apply")->isEnabled()); QVERIFY(h.button("options-ok")->isEnabled());
    const auto capture = qEnvironmentVariable("BIGLYBTC_OPTIONS_CAPTURE_DIRECTORY");
    if (!capture.isEmpty()) {
      QVERIFY(QDir::isAbsolutePath(capture)); QVERIFY(QDir().mkpath(capture));
      QCoreApplication::processEvents();
      QVERIFY(h.dialog.grab().save(capture + QStringLiteral("/options-connection.png")));
      tree->setCurrentItem(tree->topLevelItem(0)->child(1));
      QCoreApplication::processEvents();
      QVERIFY(h.dialog.grab().save(capture + QStringLiteral("/options-proxy.png")));

      tree->expandAll();
      for (const auto& name : {QStringLiteral("Queue"), QStringLiteral("Seeding"), QStringLiteral("First Priority"), QStringLiteral("Ignore")}) {
        QTreeWidgetItem* page = nullptr;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) { if ((*it)->text(0) == name) { page = *it; break; } }
        QVERIFY(page); tree->setCurrentItem(page); QCoreApplication::processEvents();
        QVERIFY(h.dialog.grab().save(capture + QStringLiteral("/options-") + name.toLower().replace(' ', '-') + QStringLiteral(".png")));
      }
    }
  }
  void hiddenAndBusyInitialReadCoalescesUntilVisibleIdle() {
    Harness h; QVERIFY(h.dialog.beginSession(5)); h.dialog.setBusy(true);
    QCoreApplication::processEvents(); QVERIFY(h.loads.empty());
    h.dialog.show(); QCoreApplication::processEvents(); QVERIFY(h.loads.empty());
    h.dialog.setBusy(false); QTRY_COMPARE(h.loads.size(), std::size_t{1});
    h.dialog.setBusy(false); QCoreApplication::processEvents(); QCOMPARE(h.loads.size(), std::size_t{1});
    QVERIFY(h.loads.front().session == 5 && h.loads.front().request != 0 && h.dialog.pending());
  }
  void staleAndMalformedReadPacketsCannotReplaceCurrentProfile() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1});
    const auto first = h.loads.front();
    QVERIFY(!h.dialog.setSnapshot(snapshot({first.session + 1, first.request})));
    QVERIFY(!h.dialog.setSnapshot(snapshot({first.session, first.request + 1})));
    auto malformed = defaults(); malformed->fields.back().id = core::SettingId::TcpPort;
    QVERIFY(!h.dialog.setSnapshot(snapshot(first, 10, malformed))); QVERIFY(h.dialog.pending());
    QVERIFY(h.dialog.beginSession(6)); QTRY_COMPARE(h.loads.size(), std::size_t{2});
    QVERIFY(!h.dialog.beginSession(5)); QVERIFY(!h.dialog.beginSession(6)); QVERIFY(!h.dialog.beginSession(0));
    QVERIFY(!h.dialog.setSnapshot(snapshot(first))); QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.back(), 20)));
    QCOMPARE(h.dialog.session(), quint64{6}); QCOMPARE(h.dialog.revision(), quint64{20});
    QVERIFY(!h.dialog.pending());
  }
  void sparseEditsPreserveInvalidAndMissingFields() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1});
    auto values = defaults();
    values->fields[static_cast<std::size_t>(core::SettingId::TcpPort)] = {core::SettingId::TcpPort, OptionsFieldState::InvalidType, {}};
    values->fields[static_cast<std::size_t>(core::SettingId::ProxyHost)] = {core::SettingId::ProxyHost, OptionsFieldState::Stored, std::string("\xffx", 2)};
    values->fields[static_cast<std::size_t>(core::SettingId::TcpEnabled)] = {core::SettingId::TcpEnabled, OptionsFieldState::OutOfRange, std::int64_t{2}};
    QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.front(), 10, values))); QVERIFY(!h.dialog.dirty());
    QVERIFY(h.line(core::SettingId::TcpPort)->text().isEmpty());
    QCOMPARE(h.dialog.findChild<QCheckBox*>(id(core::SettingId::TcpEnabled))->checkState(), Qt::PartiallyChecked);
    h.line(core::SettingId::UploadLimit)->setText(QStringLiteral("73"));
    QVERIFY(h.dialog.dirty()); h.button("options-apply")->click(); QCOMPARE(h.saves.size(), std::size_t{1});
    const auto& submission = h.saves.front(); QCOMPARE(submission.revision, quint64{10});
    QCOMPARE(submission.changes.size(), std::size_t{1}); QCOMPARE(submission.changes.front().id, core::SettingId::UploadLimit);
    QCOMPARE(std::get<std::int64_t>(submission.changes.front().value), std::int64_t{73});
    QVERIFY(h.dialog.pending()); QVERIFY(!h.line(core::SettingId::UploadLimit)->isEnabled());
  }
  void returningToLoadedValueSubmitsNothing() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1}); QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.front())));
    h.line(core::SettingId::DownloadLimit)->setText(QStringLiteral("29")); QVERIFY(h.dialog.dirty());
    h.line(core::SettingId::DownloadLimit)->setText(QStringLiteral("0")); QVERIFY(!h.dialog.dirty());
    h.button("options-apply")->click(); QVERIFY(h.saves.empty());
  }
  void invalidEditedValueDoesNotReachWorker() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1}); QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.front())));
    h.line(core::SettingId::TcpPort)->setText(QStringLiteral("0")); h.button("options-apply")->click();
    QVERIFY(h.dialog.dirty() && !h.dialog.pending() && h.saves.empty());
    QVERIFY(h.dialog.findChild<QLabel*>(QStringLiteral("options-status"))->text().contains(QStringLiteral("TCP listen port")));
    h.line(core::SettingId::TcpPort)->setText(QStringLiteral("65535")); h.button("options-apply")->click();
    QCOMPARE(h.saves.size(), std::size_t{1});
  }
  void failedSaveRetainsDraftAndRetryUsesOriginalRevision() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1}); QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.front())));
    h.line(core::SettingId::UploadLimit)->setText(QStringLiteral("41")); h.button("options-apply")->click();
    QCOMPARE(h.saves.size(), std::size_t{1}); const auto first = h.saves.front();
    auto stale = std::make_shared<OptionsSavePacket>(*saved(first, false)); ++stale->request;
    QVERIFY(!h.dialog.setSaveResult(stale)); QVERIFY(h.dialog.pending());
    QVERIFY(h.dialog.setSaveResult(saved(first, false))); QVERIFY(h.dialog.dirty() && !h.dialog.pending());
    QCOMPARE(h.line(core::SettingId::UploadLimit)->text(), QStringLiteral("41")); QCOMPARE(h.dialog.revision(), quint64{10});
    h.button("options-apply")->click(); QCOMPARE(h.saves.size(), std::size_t{2});
    QCOMPARE(h.saves.back().revision, first.revision); QVERIFY(h.saves.back().request > first.request);
    auto values = defaults(); values->fields[static_cast<std::size_t>(core::SettingId::UploadLimit)] = {core::SettingId::UploadLimit, OptionsFieldState::Stored, std::int64_t{41}};
    QVERIFY(h.dialog.setSaveResult(saved(h.saves.back(), true, values, true)));
    QVERIFY(!h.dialog.dirty() && !h.dialog.pending() && h.dialog.isVisible()); QCOMPARE(h.dialog.revision(), quint64{11});
    QVERIFY(!h.dialog.findChild<QLabel*>(QStringLiteral("options-restart-status"))->text().isEmpty());
  }
  void okWaitsForMatchingCommitAndPendingCloseCannotDiscard() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1}); QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.front())));
    QSignalSpy accepted(&h.dialog, &QDialog::accepted);
    h.line(core::SettingId::UploadLimit)->setText(QStringLiteral("51")); h.dialog.accept();
    QCOMPARE(h.saves.size(), std::size_t{1}); h.dialog.reject(); h.dialog.close();
    QVERIFY(h.dialog.isVisible() && h.dialog.pending()); QCOMPARE(accepted.count(), 0);
    auto values = defaults(); values->fields[static_cast<std::size_t>(core::SettingId::UploadLimit)] = {core::SettingId::UploadLimit, OptionsFieldState::Stored, std::int64_t{51}};
    QVERIFY(h.dialog.setSaveResult(saved(h.saves.back(), true, values))); QCOMPARE(accepted.count(), 1);
    QVERIFY(!h.dialog.isVisible());
  }
  void dirtyCancelAndDiscardAreExplicit() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1}); QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.front())));
    h.line(core::SettingId::UploadLimit)->setText(QStringLiteral("61")); h.dialog.reject();
    auto* prompt = h.dialog.findChild<QMessageBox*>(QStringLiteral("options-discard-confirmation")); QVERIFY(prompt);
    prompt->button(QMessageBox::Cancel)->click(); QTRY_VERIFY(!h.dialog.findChild<QMessageBox*>(QStringLiteral("options-discard-confirmation")));
    QVERIFY(h.dialog.isVisible() && h.dialog.dirty()); QCOMPARE(h.line(core::SettingId::UploadLimit)->text(), QStringLiteral("61"));
    h.dialog.close(); prompt = h.dialog.findChild<QMessageBox*>(QStringLiteral("options-discard-confirmation")); QVERIFY(prompt);
    prompt->button(QMessageBox::Discard)->click(); QTRY_VERIFY(!h.dialog.isVisible());
    QVERIFY(h.saves.empty() && h.dialog.session() == 0 && !h.dialog.dirty());
  }
  void readFailureCanRetryAndSessionInvalidationDropsCredentials() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1});
    auto failure = std::make_shared<OptionsPacket>(); failure->session = h.loads.front().session; failure->request = h.loads.front().request;
    failure->failure = QStringLiteral("Generated read failure"); QVERIFY(h.dialog.setSnapshot(failure));
    QVERIFY(!h.dialog.pending()); h.button("options-reload")->click(); QCOMPARE(h.loads.size(), std::size_t{2});
    auto values = defaults(); values->fields[static_cast<std::size_t>(core::SettingId::ProxyPassword)] = {core::SettingId::ProxyPassword, OptionsFieldState::Stored, std::string("sensitive")};
    QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.back(), 10, values))); h.dialog.invalidateSession();
    QVERIFY(h.line(core::SettingId::ProxyPassword)->text().isEmpty()); QVERIFY(!h.button("options-ok")->isEnabled());
    QVERIFY(!h.dialog.setSnapshot(snapshot(h.loads.back()))); QVERIFY(!h.dialog.beginSession(5));
  }
  void loadAndSaveSignalCallbacksMayDeleteDialog() {
    QPointer<OptionsDialog> first = new OptionsDialog;
    QObject::connect(first, &OptionsDialog::loadRequested, qApp, [&] { delete first.data(); });
    first->beginSession(1); first->show(); QTRY_VERIFY(!first);
    QPointer<OptionsDialog> second = new OptionsDialog;
    std::optional<OptionsRequest> request;
    QObject::connect(second, &OptionsDialog::loadRequested, qApp, [&](auto value) { request = value; });
    second->beginSession(2); second->show(); QTRY_VERIFY(request.has_value()); QVERIFY(second->setSnapshot(snapshot(*request)));
    QObject::connect(second, &OptionsDialog::saveRequested, qApp, [&](auto) { delete second.data(); });
    second->findChild<QLineEdit*>(id(core::SettingId::UploadLimit))->setText(QStringLiteral("81"));
    second->findChild<QPushButton*>(QStringLiteral("options-apply"))->click(); QVERIFY(!second);
  }
  void enabledCallbackMayRebindProfileWithoutOldValuesLeaking() {
    Harness h; h.show(); QTRY_COMPARE(h.loads.size(), std::size_t{1}); const auto old = h.loads.front();
    EnabledCallback event; event.callback = [&] { h.dialog.beginSession(6); };
    h.line(core::SettingId::TcpPort)->installEventFilter(&event);
    auto values = defaults(); values->fields[static_cast<std::size_t>(core::SettingId::ProxyPassword)] = {core::SettingId::ProxyPassword, OptionsFieldState::Stored, std::string("old-secret")};
    QVERIFY(h.dialog.setSnapshot(snapshot(old, 10, values))); QTRY_COMPARE(h.loads.size(), std::size_t{2});
    QCOMPARE(h.dialog.session(), quint64{6}); QVERIFY(h.line(core::SettingId::ProxyPassword)->text().isEmpty());
    QVERIFY(!h.dialog.setSnapshot(snapshot(old))); QVERIFY(h.dialog.setSnapshot(snapshot(h.loads.back(), 20)));
    QCOMPARE(h.dialog.revision(), quint64{20}); QVERIFY(!h.dialog.dirty());
    h.line(core::SettingId::UploadLimit)->setText(QStringLiteral("91")); QVERIFY(h.dialog.dirty());
  }
};
QTEST_MAIN(OptionsDialogTests)
#include "options_dialog_tests.moc"
