// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_plugin_updates.hpp"

#include "library_window.hpp"
#include "session_bridge.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace biglybtc::app {
namespace {

QString label(const char* text) {
  return QCoreApplication::translate("NativePluginUpdates", text);
}

class PluginUpdateControls final : public QObject {
public:
  PluginUpdateControls(ui::LibraryWindow& window, SessionBridge& bridge, QMenu& menu)
      : QObject(&window), window_(window), bridge_(bridge),
        action_(menu.addAction(label("Native plugin updates…"))) {
    action_->setObjectName(QStringLiteral("classic-native-plugin-updates"));
    action_->setEnabled(false);
    connect(action_, &QAction::triggered, this, [this] { open(); });
    connect(&bridge_, &SessionBridge::columnLayoutSessionReady, this, [this](quint64 session) {
      if (session_ != session && dialog_) { dialog_->close(); }
      session_ = session;
      enableActions();
    });
    auto closed = [this] {
      session_ = 0;
      if (dialog_) { dialog_->close(); }
      enableActions();
    };
    connect(&bridge_, &SessionBridge::sessionClosed, this, closed);
    connect(&bridge_, &SessionBridge::sessionClosedWithoutCheckpoint, this, closed);
    connect(&bridge_, &SessionBridge::busyChanged, this, [this](bool busy) {
      busy_ = busy;
      if (!busy && pending_) {
        pending_ = false;
        if (dialog_) { message_->setText(label("The plugin request did not finish. Reload to see its saved state.")); }
      }
      enableActions();
    });
    connect(&bridge_, &SessionBridge::nativePluginUpdateReady, this, [this](NativePluginStatus status) {
      if (!dialog_ || !pending_ || status.session != session_) { return; }
      pending_ = false;
      status_ = std::move(status);
      showStatus();
    });
  }

private:
  void open() {
    if (dialog_) {
      dialog_->raise();
      dialog_->activateWindow();
      return;
    }
    status_ = {};
    auto* dialog = new QDialog(&window_);
    dialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("native-plugin-updates-dialog"));
    dialog->setWindowTitle(label("Native plugin updates"));
    auto* layout = new QVBoxLayout(dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto field = [&](const char* title, const char* name) {
      auto* value = new QLabel(dialog);
      value->setObjectName(QString::fromLatin1(name));
      value->setTextFormat(Qt::PlainText);
      form->addRow(label(title), value);
      return value;
    };
    field("Plugin", "native-plugin-id")->setText(QStringLiteral("azlocprov"));
    running_ = field("Signed version running", "native-plugin-running");
    active_ = field("Selected for next start", "native-plugin-active");
    staged_ = field("Ready to install", "native-plugin-staged");
    previous_ = field("Previous version", "native-plugin-previous");
    message_ = new QLabel(dialog);
    message_->setObjectName(QStringLiteral("native-plugin-message"));
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    layout->addWidget(buttons);
    auto button = [&](const char* title, const char* name, NativePluginAction command) {
      auto* control = buttons->addButton(label(title), QDialogButtonBox::ActionRole);
      control->setObjectName(QString::fromLatin1(name));
      control->setAutoDefault(false);
      connect(control, &QPushButton::clicked, this, [this, command] { run(command); });
      return control;
    };
    reload_ = button("Reload status", "native-plugin-reload", NativePluginAction::Status);
    download_ = button("Check and download", "native-plugin-download", NativePluginAction::Download);
    install_ = button("Install for restart", "native-plugin-install", NativePluginAction::Install);
    rollback_ = button("Roll back for restart", "native-plugin-rollback", NativePluginAction::Rollback);
    cancel_ = buttons->addButton(label("Cancel operation"), QDialogButtonBox::ActionRole);
    cancel_->setObjectName(QStringLiteral("native-plugin-cancel"));
    cancel_->setAutoDefault(false);
    connect(cancel_, &QPushButton::clicked, this, [this] {
      cancel_->setEnabled(false);
      requestStop_.request_stop();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, [this] {
      dialog_.clear();
      if (pending_) { requestStop_.request_stop(); }
      pending_ = false;
    });
    showStatus();
    dialog->show();
    run(NativePluginAction::Status);
  }

  void run(NativePluginAction command) {
    if (!dialog_ || busy_ || pending_ || !session_) { return; }
    const auto selected = command == NativePluginAction::Install ? status_.staged
        : command == NativePluginAction::Rollback ? status_.previous : std::nullopt;
    pending_ = true;
    requestStop_ = std::stop_source{};
    status_.message = label("Working…");
    showStatus();
    if (!bridge_.nativePluginUpdate({session_, command, selected, requestStop_.get_token()})) {
      pending_ = false;
      status_.message = label("The current profile cannot handle this plugin request.");
      showStatus();
    }
  }

  void showStatus() {
    const auto number = [](const auto& value) { return value ? value->version : label("None"); };
    running_->setText(status_.running.isEmpty() ? label("None") : status_.running);
    active_->setText(number(status_.active));
    staged_->setText(number(status_.staged));
    previous_->setText(number(status_.previous));
    auto notification = status_.message;
    if (!status_.startupFailure.isEmpty()) {
      if (!notification.isEmpty()) { notification += QLatin1Char('\n'); }
      notification += status_.startupFailure;
    }
    message_->setText(notification);
    enableActions();
  }

  void enableActions() {
    action_->setEnabled(session_ && !busy_);
    if (!dialog_) { return; }
    const bool ready = session_ && !busy_ && !pending_;
    reload_->setEnabled(ready);
    download_->setEnabled(ready && status_.canDownload);
    install_->setEnabled(ready && status_.staged.has_value());
    rollback_->setEnabled(ready && status_.previous.has_value());
    cancel_->setEnabled(pending_);
  }

  ui::LibraryWindow& window_;
  SessionBridge& bridge_;
  QAction* action_;
  QPointer<QDialog> dialog_;
  QLabel *running_ = nullptr, *active_ = nullptr, *previous_ = nullptr, *staged_ = nullptr, *message_ = nullptr;
  QPushButton *reload_ = nullptr, *download_ = nullptr, *install_ = nullptr, *rollback_ = nullptr, *cancel_ = nullptr;
  NativePluginStatus status_;
  quint64 session_ = 0;
  bool busy_ = false;
  bool pending_ = false;
  std::stop_source requestStop_;
};

} // namespace

void bindNativePluginUpdateControls(ui::LibraryWindow& window, SessionBridge& bridge) {
  if (auto* menu = window.findChild<QMenu*>(QStringLiteral("classic-plugins-menu"))) {
    new PluginUpdateControls(window, bridge, *menu);
  }
}

} // namespace biglybtc::app
