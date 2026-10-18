#include "library_window.hpp"
#include "session_bridge.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QItemSelectionModel>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabWidget>
#include <QtTest>

#include <array>

namespace {
using namespace biglybtc;

QByteArray bytes(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

void wire(ui::LibraryWindow& window, app::SessionBridge& bridge) {
  QObject::connect(&bridge, &app::SessionBridge::snapshotReady, &window, &ui::LibraryWindow::setPresentation);
  QObject::connect(&bridge, &app::SessionBridge::progressReady, &window, &ui::LibraryWindow::setTelemetry);
  QObject::connect(&bridge, &app::SessionBridge::detailsReady, &window, &ui::LibraryWindow::setDetails);
  QObject::connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
  QObject::connect(&bridge, &app::SessionBridge::messageReady, &window, &ui::LibraryWindow::setMessage);
  QObject::connect(&window, &ui::LibraryWindow::refreshRequested, &bridge, &app::SessionBridge::refresh);
  QObject::connect(&window, &ui::LibraryWindow::continueRequested, &bridge, &app::SessionBridge::continueDownload);
  QObject::connect(&window, &ui::LibraryWindow::detailsRequested, &bridge, &app::SessionBridge::details);
  QObject::connect(&window, &ui::LibraryWindow::cancelRequested, &bridge, &app::SessionBridge::cancel);
}
} // namespace

class PreviewSmoke final : public QObject {
  Q_OBJECT
private slots:
  void generatedSampleLibraryAndFiles() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    QVERIFY2(!profile.isEmpty(), "Prepare the sample with preview.py and set BIGLYBTC_PREVIEW_PROFILE");
    const QFileInfo profileInfo(profile);
    const QDir root(profileInfo.absolutePath());
    QVERIFY(profileInfo.isDir());
    QCOMPARE(profileInfo.fileName(), QStringLiteral("profile"));
    QCOMPARE(profileInfo.canonicalFilePath(), profile);
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));

    QMap<QString, QByteArray> original;
    original.insert(profile + QStringLiteral("/downloads.config"), {});
    original.insert(profile + QStringLiteral("/active/cache.dat"), {});
    const QDir active(profile + QStringLiteral("/active"));
    const auto states = active.entryList({QStringLiteral("*.dat")}, QDir::Files);
    QCOMPARE(states.size(), 5); // Four torrent states and the original metadata cache.
    for (const auto& name : states) { original.insert(active.filePath(name), {}); }
    for (auto it = original.begin(); it != original.end(); ++it) {
      it.value() = bytes(it.key());
      QVERIFY2(!it.value().isEmpty(), qPrintable(it.key()));
    }
    const QDir payload(root.filePath(QStringLiteral("payload")));
    QVERIFY(payload.exists());
    QVERIFY(payload.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());

    const std::array<QString, 4> names{QStringLiteral("Welcome to BiglyBTC.txt"),
        QString::fromUtf8("Sample music — ש العربية"), QStringLiteral("Example documents"),
        QStringLiteral("Paused sample.bin")};
    const std::array<QString, 4> expectedStates{QStringLiteral("Complete"), QStringLiteral("Stopped"),
        QStringLiteral("Queued"), QStringLiteral("Paused")};
    constexpr std::array<int, 4> fileCounts{1, 2, 2, 1};

    ui::LibraryWindow window;
    app::SessionBridge bridge;
    wire(window, bridge);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy details(&bridge, &app::SessionBridge::detailsReady);
    QVERIFY(bridge.open(std::filesystem::path(profile.toStdString()), 0));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 1 && !bridge.busy(), 30000);
    QCOMPARE(window.model().rowCount(), 4);
    QCOMPARE(window.mainTabs().tabText(0), QStringLiteral("My Torrents"));
    auto* myTorrents = window.findChild<QAction*>(QStringLiteral("classic-my-torrents"));
    QVERIFY(myTorrents && myTorrents->isEnabled());
    QCOMPARE(myTorrents->shortcut(), QKeySequence(QStringLiteral("Ctrl+1")));
    QVERIFY(!window.continueAction().isEnabled());
    QCOMPARE(window.incompleteTable().model()->rowCount(), 3);
    QCOMPARE(window.completeTable().model()->rowCount(), 1);

    for (int row = 0; row < 4; ++row) {
      const auto at = static_cast<std::size_t>(row);
      const qint64 total = (row + 1) * qint64{32768};
      QCOMPARE(window.model().data(window.model().index(row, 0), Qt::DisplayRole).toString(), names[at]);
      QCOMPARE(window.model().data(window.model().index(row, 1), Qt::DisplayRole).toString(), expectedStates[at]);
      QCOMPARE(window.model().data(window.model().index(row, 2), Qt::UserRole).toLongLong(), total);
      const auto target = window.model().targetAt(row);
      QVERIFY(target.has_value());
      QVERIFY(!window.model().canContinue(*target));
      auto& pane = row == 0 ? window.completeTable() : window.incompleteTable();
      auto* proxy = qobject_cast<QSortFilterProxyModel*>(pane.model());
      QVERIFY(proxy);
      const auto visible = proxy->mapFromSource(window.model().index(row, 0));
      QVERIFY(visible.isValid());
      pane.setFocus();
      QCoreApplication::processEvents();
      pane.selectionModel()->setCurrentIndex(visible, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      if (row == 0) {
        QVERIFY(window.filesAction().isEnabled());
        window.filesAction().trigger();
      }
      QTRY_VERIFY_WITH_TIMEOUT(details.count() == row + 1 && !bridge.busy(), 30000);
      QVERIFY(window.filesModel().target() == target);
      QCOMPARE(window.filesModel().rowCount(), fileCounts[at]);
      QCOMPARE(window.filesTable().model()->rowCount(), fileCounts[at]);
      QVERIFY(!window.continueAction().isEnabled());
      qint64 sum = 0;
      for (int file = 0; file < fileCounts[at]; ++file) {
        const auto expectedPath = fileCounts[at] == 1 ? names[at]
            : file == 0 ? QStringLiteral("Read me.txt") : QString::fromUtf8("Examples/Unicode — ש.txt");
        QCOMPARE(window.filesModel().data(window.filesModel().index(file, 0), Qt::DisplayRole).toString(), expectedPath);
        const auto size = window.filesModel().data(window.filesModel().index(file, 1), Qt::UserRole).toLongLong();
        QCOMPARE(size, total / fileCounts[at]);
        sum += size;
        QCOMPARE(window.filesModel().originalIndex(file).value_or(-1), file);
        QVERIFY(window.filesModel().data(window.filesModel().index(file, 3), Qt::UserRole).toBool());
      }
      QCOMPARE(sum, total);
    }

    const auto selected = window.filesModel().target();
    QVERIFY(selected.has_value());
    QVERIFY(bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 2 && details.count() == 5 && !bridge.busy(), 30000);
    QCOMPARE(window.model().rowCount(), 4);
    QCOMPARE(window.filesModel().target()->hash, selected->hash);
    QVERIFY(window.filesModel().target()->generation > selected->generation);
    QVERIFY(!window.continueAction().isEnabled());
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    if (!capture.isEmpty()) {
      const QFileInfo image(capture);
      QCOMPARE(image.absoluteDir().canonicalPath(), root.canonicalPath());
      QVERIFY(!image.exists());
      QVERIFY(window.grab().save(capture));
    }
    QVERIFY(window.close());
    QVERIFY(!window.isVisible());
    for (auto it = original.cbegin(); it != original.cend(); ++it) { QCOMPARE(bytes(it.key()), it.value()); }
    QVERIFY(payload.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  }
};

QTEST_MAIN(PreviewSmoke)
#include "preview_smoke.moc"
