from pathlib import Path
import hashlib, json

NATIVE = Path('/home/yurii/Java/BiglyBTC')
WORK = Path(__file__).resolve().parent
STAGE = WORK / 'root/staging'
records = []

def edit(name, changes, append=''):
    path = 'preview/native/' + name
    source = (NATIVE / path).read_text()
    result = source
    for before, after in changes:
        assert result.count(before) == 1, (name, before[:100], result.count(before))
        result = result.replace(before, after, 1)
    if append:
        at = result.rfind('} // namespace')
        assert at >= 0, name
        result = result[:at] + append + '\n' + result[at:]
    dest = STAGE / path; dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(result)
    records.append({'path': path, 'before_sha256': hashlib.sha256(source.encode()).hexdigest(),
                    'sha256': hashlib.sha256(dest.read_bytes()).hexdigest()})

edit('app/session_bridge.hpp', [
    ('#pragma once', '#pragma once\n#include "options_presentation.hpp"'),
    ('  bool refresh();', '  bool loadOptions(ui::OptionsRequest);\n  bool saveOptions(ui::OptionsSubmission);\n  bool refresh();'),
    ('  void snapshotReady(', '  void optionsReady(biglybtc::ui::OptionsPresentation);\n  void optionsSaved(biglybtc::ui::OptionsSavePresentation);\n  void intakeDefaultsReady(quint64 session, QString directory);\n  void snapshotReady('),
])

binding = r'''
namespace {
engine::LiveQueueLimits optionQueues(const core::Document& document) {
  engine::LiveQueueLimits result;
  for (const auto& [key, slot] : {std::pair{"max downloads", &result.maximumDownloads},
                                std::pair{"max active torrents", &result.maximumActive}}) {
    if (const auto* value = document.find(key)) {
      if (value->type != core::Node::Type::Integer || value->integer < 0 || value->integer > 1000000) {
        throw Failure{{storage::ErrorCode::InvalidFormat, std::string("Invalid setting: ") + key}};
      }
      *slot = static_cast<int>(value->integer);
    }
  }
  return result;
}
ui::OptionsPresentation optionPacket(const storage::ApplicationSettingsSnapshot& snapshot,
    quint64 session, quint64 request, quint64 revision, bool restart) {
  auto values = std::make_shared<ui::OptionsValues>();
  values->fields.reserve(snapshot.fields.size());
  for (const auto& field : snapshot.fields) {
    ui::OptionsFieldState state;
    switch (field.state) {
      case storage::SettingState::Defaulted: state = ui::OptionsFieldState::Defaulted; break;
      case storage::SettingState::Stored: state = ui::OptionsFieldState::Stored; break;
      case storage::SettingState::InvalidType: state = ui::OptionsFieldState::InvalidType; break;
      case storage::SettingState::OutOfRange: state = ui::OptionsFieldState::OutOfRange; break;
      case storage::SettingState::UnavailableDefault: state = ui::OptionsFieldState::UnavailableDefault; break;
      default: throw Failure{{storage::ErrorCode::InvalidFormat, "Unknown configuration field state"}};
    }
    values->fields.push_back({field.id, state, field.value});
  }
  auto packet = std::make_shared<ui::OptionsPacket>();
  packet->session = session; packet->request = request; packet->revision = revision;
  packet->snapshot = std::move(values); packet->restartRequired = restart;
  return packet;
}
QString optionDefaultPath(const storage::ApplicationSettingsSnapshot& snapshot) {
  for (const auto& field : snapshot.fields) {
    if (field.id != core::SettingId::DefaultSavePath || !field.value
        || (field.state != storage::SettingState::Stored && field.state != storage::SettingState::Defaulted)) { continue; }
    const auto* raw = std::get_if<std::string>(&*field.value);
    if (!raw) { return {}; }
    const auto value = QString::fromUtf8(raw->data(), static_cast<qsizetype>(raw->size()));
    return value.toUtf8().toStdString() == *raw ? value : QString{};
  }
  return {};
}
}

bool SessionBridge::loadOptions(ui::OptionsRequest request) {
  if (profileClosed_ || !request.session || !request.request || request.session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, request](std::stop_token stop) {
    auto failure = std::make_shared<ui::OptionsPacket>();
    failure->session = request.session; failure->request = request.request;
    ui::OptionsPresentation result = failure;
    if (!host_ || host_->columnLayoutSession != request.session) {
      failure->failure = tr("This settings session is no longer active");
    } else {
      host_->optionsBaseline.reset();
      host_->optionsReadStop = stop;
      auto read = storage::readApplicationSettings(host_->lease, stop);
      if (!read) { failure->failure = text(read.error().message); }
      else if (stop.stop_requested()) { failure->failure = tr("Cancelled"); }
      else if (host_->optionsRevision == std::numeric_limits<quint64>::max()) {
        failure->failure = tr("Reopen the profile before editing more settings");
      } else {
        bool restart = !host_->engine;
        if (read->source) {
          const auto policy = engine::readNetworkPolicy(read->source.get(), {.mode = engine::NetworkMode::Normal});
          if (policy) {
            auto live = engine::prepareLiveSettings(host_->networkPolicy, *policy);
            restart = restart || !live || live->restartFields != 0;
          }
        }
        result = optionPacket(*read, request.session, request.request, ++host_->optionsRevision, restart);
        host_->optionsBaseline = std::move(*read);
      }
    }
    QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
      QPointer<SessionBridge> guard(this);
      if (result->session == publishedColumnLayoutSession_) { emit optionsReady(result); }
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false);
}

bool SessionBridge::saveOptions(ui::OptionsSubmission submission) {
  if (profileClosed_ || !submission.session || !submission.request || !submission.revision
      || submission.session != publishedColumnLayoutSession_
      || submission.changes.size() > core::applicationSettingsCatalog().size()) { return false; }
  for (const auto& change : submission.changes) {
    const auto* field = core::applicationSetting(change.id);
    if (!field || !core::validApplicationSettingValue(*field, change.value)) { return false; }
  }
  return dispatch([this, submission = std::move(submission)](std::stop_token stop) {
    auto packet = std::make_shared<ui::OptionsSavePacket>();
    packet->session = submission.session; packet->request = submission.request; packet->revision = submission.revision;
    if (!host_ || host_->columnLayoutSession != submission.session || !host_->optionsBaseline
        || host_->optionsRevision != submission.revision || host_->optionsReadStop.stop_requested()
        || host_->optionsRevision == std::numeric_limits<quint64>::max()) {
      packet->failure = tr("Reload settings before saving; this snapshot is no longer current");
    } else {
      auto prepared = storage::prepareApplicationSettings(*host_->optionsBaseline, submission.changes);
      if (!prepared) { packet->failure = text(prepared.error().message); }
      else {
        auto policy = engine::readNetworkPolicy(&prepared->document(), {.mode = engine::NetworkMode::Normal});
        if (!policy) { packet->failure = tr("Unsupported or invalid setting: %1").arg(text(policy.error().field)); }
        else {
          const auto queues = optionQueues(prepared->document());
          auto planned = engine::prepareLiveSettings(host_->networkPolicy, *policy, queues);
          if (!planned) { packet->failure = text(planned.error().message); }
          else {
            auto saved = storage::saveApplicationSettings(host_->lease, *host_->optionsBaseline, *prepared, stop);
            // Once committed, never replace the actual write result with cancellation.
            if (!saved) { packet->failure = text(saved.error().message); }
            else {
              packet->saved = true;
              packet->restartRequired = !host_->engine || planned->restartFields != 0;
              host_->optionsBaseline = std::move(*saved);
              ++host_->optionsRevision;
              if (host_->engine) {
                auto applied = host_->engine->applyLiveSettings(*policy, queues);
                if (applied) { host_->networkPolicy = std::move(planned->effectivePolicy); }
                else {
                  packet->restartRequired = true;
                  packet->failure = tr("Settings were saved; restart BiglyBTC to apply them");
                }
              }
              packet->snapshot = optionPacket(*host_->optionsBaseline, submission.session,
                  submission.request, host_->optionsRevision, packet->restartRequired);
              host_->intakeDefault = optionDefaultPath(*host_->optionsBaseline);
            }
          }
        }
      }
    }
    const auto directory = host_ ? host_->intakeDefault : QString{};
    ui::OptionsSavePresentation result = std::move(packet);
    QMetaObject::invokeMethod(this, [this, result = std::move(result), directory] {
      QPointer<SessionBridge> guard(this);
      if (result->session == publishedColumnLayoutSession_) {
        emit optionsSaved(result);
        if (!guard) { return; }
        if (result->saved) { emit intakeDefaultsReady(result->session, directory); }
      }
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false);
}
'''
edit('app/session_bridge.cpp', [
    ('#include "session_bridge.hpp"', '#include "session_bridge.hpp"\n#include "application_settings.hpp"\n#include "../engine/live_settings.hpp"'),
    ('  Host(std::filesystem::path p,', '  std::optional<storage::ApplicationSettingsSnapshot> optionsBaseline;\n  quint64 optionsRevision = 0;\n  std::stop_token optionsReadStop;\n  QString intakeDefault;\n  bool intakeDefaultLoaded = false;\n\n  Host(std::filesystem::path p,'),
    ('void SessionBridge::ready(ui::Presentation snapshot, QString message) {',
     'namespace { QString optionDefaultPath(const storage::ApplicationSettingsSnapshot&); engine::LiveQueueLimits optionQueues(const core::Document&); }\n\nvoid SessionBridge::ready(ui::Presentation snapshot, QString message) {\n  if (host_ && !host_->intakeDefaultLoaded) {\n    host_->intakeDefaultLoaded = true;\n    auto settings = storage::readApplicationSettings(host_->lease);\n    if (settings) { host_->intakeDefault = optionDefaultPath(*settings); }\n  }'),
    ('snapshot = std::move(snapshot), message = std::move(message), session] {',
     'snapshot = std::move(snapshot), message = std::move(message), session, directory = host_ ? host_->intakeDefault : QString{}] {'),
    ('    emit snapshotReady(snapshot);\n    if (!guard) { return; }',
     '    emit snapshotReady(snapshot);\n    if (!guard) { return; }\n    emit intakeDefaultsReady(session, directory);\n    if (!guard) { return; }'),
    ('    host_->networkPolicy = std::move(*policy);',
     '    host_->networkPolicy = std::move(*policy);\n    auto live = (*owner)->applyLiveSettings(host_->networkPolicy, config ? optionQueues(config->document()) : engine::LiveQueueLimits{});\n    if (!live) { throw Failure{live.error()}; }'),
], binding)

window = r'''
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
void LibraryWindow::setIntakeDefaults(quint64 session, QString directory) {
  if (!closing_ && session && session == columnSession_) { defaultSaveDirectory_ = std::move(directory); }
}
'''
edit('ui/library_window.hpp', [
    ('#pragma once', '#pragma once\n#include "options_presentation.hpp"'),
    ('class ClassicColumns;', 'class OptionsDialog;\nclass ClassicColumns;'),
    ('  void setBusy(bool);', '  void setOptions(OptionsPresentation);\n  void setOptionsSaved(OptionsSavePresentation);\n  void rejectOptionsRequest(OptionsRequest);\n  void rejectOptionsSave(const OptionsSubmission&);\n  void setIntakeDefaults(quint64 session, QString directory);\n  void setBusy(bool);'),
    ('signals:\n', 'signals:\n  void optionsRequested(biglybtc::ui::OptionsRequest);\n  void optionsSaveRequested(biglybtc::ui::OptionsSubmission);\n'),
    ('  void updateGeneral();', '  void openOptions();\n  void queueOptions();\n  void updateGeneral();'),
    ('  LibraryModel* model_;', '  QAction* optionsAction_ = nullptr;\n  QPointer<OptionsDialog> optionsDialog_;\n  std::optional<OptionsRequest> pendingOptionsLoad_, activeOptionsLoad_;\n  std::optional<OptionsSubmission> activeOptionsSave_;\n  bool optionsDispatchQueued_ = false;\n  QString defaultSaveDirectory_;\n  LibraryModel* model_;'),
])
edit('ui/library_window.cpp', [
    ('#include "library_window.hpp"', '#include "library_window.hpp"\n#include "options_dialog.hpp"'),
    ('  unavailable(tools, tr("Options…"), QStringLiteral("options"), QStyle::SP_FileDialogDetailedView);',
     '  optionsAction_ = tools->addAction(tr("Options…"));\n  optionsAction_->setObjectName(QStringLiteral("classic-options"));\n  connect(optionsAction_, &QAction::triggered, this, &LibraryWindow::openOptions);'),
    ('  columnSession_ = session;', '  columnSession_ = session;\n  defaultSaveDirectory_.clear();\n  pendingOptionsLoad_.reset(); activeOptionsLoad_.reset(); activeOptionsSave_.reset();\n  if (optionsDialog_) { optionsDialog_->invalidateSession(); optionsDialog_->close(); }'),
    ('  sessionCloseAcknowledged_ = true;', '  sessionCloseAcknowledged_ = true;\n  if (optionsDialog_) { optionsDialog_->invalidateSession(); }'),
    ('  reloadColumns_->setEnabled(!closing_ && columnSession_ != 0 && columnRequest_ != std::numeric_limits<quint64>::max());\n  queueDownloadActions();\n}', '  reloadColumns_->setEnabled(!closing_ && columnSession_ != 0 && columnRequest_ != std::numeric_limits<quint64>::max());\n  if (optionsAction_) { optionsAction_->setEnabled(!closing_ && !busy_ && columnSession_ != 0); }\n  queueDownloadActions();\n}'),
    ('  destination->setMaxLength(4096); destination->setAccessibleName(tr("Download directory"));',
     '  destination->setMaxLength(4096); destination->setAccessibleName(tr("Download directory"));\n  destination->setText(defaultSaveDirectory_);'),
    ('QFileDialog::getExistingDirectory(dialog.data(), tr("Download directory"))',
     'QFileDialog::getExistingDirectory(dialog.data(), tr("Download directory"), destination->text())'),
    ('void LibraryWindow::setBusy(bool value) {',
     'void LibraryWindow::setBusy(bool value) {\n  QPointer<LibraryWindow> optionsGuard(this);\n  if (optionsDialog_) { optionsDialog_->setBusy(value); }\n  if (!optionsGuard) { return; }\n  if (!value) {\n    if (activeOptionsLoad_) { rejectOptionsRequest(*activeOptionsLoad_); }\n    if (!optionsGuard) { return; }\n    if (activeOptionsSave_) { rejectOptionsSave(*activeOptionsSave_); }\n    if (!optionsGuard) { return; }\n  }'),
    ('  queueOptionalViews();\n  if (mainTabs_->currentWidget()', '  queueOptions();\n  queueOptionalViews();\n  if (mainTabs_->currentWidget()'),
    ('void LibraryWindow::closeEvent(QCloseEvent* event) {\n  closing_ = true;',
     'void LibraryWindow::closeEvent(QCloseEvent* event) {\n  QPointer<LibraryWindow> optionsGuard(this);\n  if (optionsDialog_ && optionsDialog_->isVisible() && !optionsDialog_->close()) { event->ignore(); return; }\n  if (!optionsGuard) { return; }\n  pendingOptionsLoad_.reset();\n  closing_ = true;'),
], window)

edit('app/main_desktop.cpp', [
    ('    QObject::connect(&bridge, &Bridge::snapshotReady,',
     '    QObject::connect(&bridge, &Bridge::optionsReady, &window, &Window::setOptions);\n    QObject::connect(&bridge, &Bridge::optionsSaved, &window, &Window::setOptionsSaved);\n    QObject::connect(&bridge, &Bridge::intakeDefaultsReady, &window, &Window::setIntakeDefaults);\n    QObject::connect(&window, &Window::optionsRequested, &bridge, [&](biglybtc::ui::OptionsRequest request) {\n      if (!bridge.loadOptions(request)) { window.rejectOptionsRequest(request); }\n    });\n    QObject::connect(&window, &Window::optionsSaveRequested, &bridge, [&](biglybtc::ui::OptionsSubmission submission) {\n      if (!bridge.saveOptions(submission)) { window.rejectOptionsSave(submission); }\n    });\n    QObject::connect(&bridge, &Bridge::snapshotReady,'),
])
edit('adapters/CMakeLists.txt', [], '')
p=STAGE/'preview/native/adapters/CMakeLists.txt';p.write_text(p.read_text()+'\ntarget_sources(biglybtc_profile PRIVATE application_settings.cpp)\n');records[-1]['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
edit('ui/CMakeLists.txt', [])
p=STAGE/'preview/native/ui/CMakeLists.txt';p.write_text(p.read_text()+'\ntarget_sources(biglybtc_ui PRIVATE options_presentation.hpp options_dialog.hpp options_dialog.cpp)\n');records[-1]['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
edit('engine/CMakeLists.txt', [])
p=STAGE/'preview/native/engine/CMakeLists.txt';p.write_text(p.read_text()+'\ntarget_sources(biglybtc_local_engine PRIVATE live_settings.cpp live_settings.hpp)\n');records[-1]['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
edit('tests/CMakeLists.txt', [])
p=STAGE/'preview/native/tests/CMakeLists.txt'
p.write_text(p.read_text()+'''
# Compatible classic Options, safe settings application and owned round trips.
add_executable(biglybtc-application-settings-tests application_settings_tests.cpp)
target_link_libraries(biglybtc-application-settings-tests PRIVATE biglybtc_profile)
target_compile_options(biglybtc-application-settings-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(biglybtc-application-settings-tests PRIVATE "LINKER:--wrap=fsync")
add_test(NAME application-settings COMMAND biglybtc-application-settings-tests)
set_tests_properties(application-settings PROPERTIES TIMEOUT 90 LABELS "unit;options;compatibility")
add_executable(biglybtc-settings-roundtrip-driver settings_roundtrip_driver.cpp)
target_link_libraries(biglybtc-settings-roundtrip-driver PRIVATE biglybtc_profile)
target_compile_options(biglybtc-settings-roundtrip-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BIGLYBTC_DESKTOP)
  foreach(options_case IN ITEMS options-dialog options-host engine-live-settings)
    string(REPLACE "-" "_" options_source "${options_case}")
    add_executable(biglybtc-${options_case}-tests ${options_source}_tests.cpp)
    set_target_properties(biglybtc-${options_case}-tests PROPERTIES AUTOMOC ON)
    target_compile_options(biglybtc-${options_case}-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME ${options_case} COMMAND biglybtc-${options_case}-tests)
    set_tests_properties(${options_case} PROPERTIES TIMEOUT 240 LABELS "unit;options" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endforeach()
  target_link_libraries(biglybtc-options-dialog-tests PRIVATE biglybtc_ui Qt6::Test)
  target_link_libraries(biglybtc-options-host-tests PRIVATE biglybtc_desktop_host biglybtc_profile biglybtc_local_engine Qt6::Test)
  target_link_libraries(biglybtc-engine-live-settings-tests PRIVATE biglybtc_local_engine biglybtc_profile LibtorrentRasterbar::torrent-rasterbar Qt6::Test)
endif()
''')
records[-1]['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
path='preview/native/tests/options_host_tests.cpp';p=STAGE/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((WORK/'options_host_tests.cpp').read_bytes())
records.append({'path':path,'before_sha256':None,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(WORK/'root/manifest.json').write_text(json.dumps({'files':records},indent=2)+'\n')
print('Root integration staged:',len(records),'files')
