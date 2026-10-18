#include "library_window.hpp"
#include "profile.hpp"
#include "session_bridge.hpp"

#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <algorithm>
#include <cerrno>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <vector>

// Raw saved-state projection only: positions are not live ranks, scheduler
// decisions, or original-Java execution results. Generated profiles stay private.
namespace {
using namespace biglybtc;
using Positions = std::vector<std::optional<qint64>>;
std::shared_ptr<ui::LibraryPresentation> sample(const Positions& positions, quint64 revision = 1, int complete = -1) {
  auto value = std::make_shared<ui::LibraryPresentation>();
  value->revision = revision;
  for (std::size_t row = 0; row < positions.size(); ++row) {
    const auto hash = QStringLiteral("%1").arg(row + 1, 40, 16, QChar('0')).toUpper();
    value->items.push_back({.key = hash, .hash = hash, .name = QStringLiteral("Row %1.bin").arg(row),
        .state = static_cast<int>(row) == complete ? QStringLiteral("Stopped") : QStringLiteral("Queued"),
        .bytes = 100, .completed = static_cast<int>(row) == complete ? 100 : 25, .received = 100, .sent = 0,
        .canContinue = static_cast<int>(row) != complete, .savedPosition = positions[row]});
  }
  return value;
}
QSortFilterProxyModel* proxy(QTableView& table) { return qobject_cast<QSortFilterProxyModel*>(table.model()); }
bool choose(ui::LibraryWindow& window, QTableView& table, int sourceRow) {
  auto* filter = proxy(table);
  if (!filter) { return false; }
  const auto index = filter->mapFromSource(window.model().index(sourceRow, 0));
  if (!index.isValid()) { return false; }
  table.setFocus();
  table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  return table.selectionModel()->isSelected(index);
}
std::optional<ui::Target> selected(ui::LibraryWindow& window, QTableView& table) {
  auto* filter = proxy(table);
  return filter && table.currentIndex().isValid() ? window.model().targetAt(filter->mapToSource(table.currentIndex()).row()) : std::nullopt;
}
QList<qint64> positions(QTableView& table) {
  QList<qint64> result;
  for (int row = 0; row < table.model()->rowCount(); ++row) {
    const auto value = table.model()->data(table.model()->index(row, 9), Qt::UserRole);
    if (value.isValid()) { result.push_back(value.toLongLong()); }
  }
  return result;
}
std::string bytes(std::string_view input) { return std::to_string(input.size()) + ':' + std::string(input); }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string result = "d";
  for (const auto& [key, value] : fields) { result += bytes(key) + value; }
  return result + 'e';
}
struct Stamp {
  dev_t device;
  ino_t inode;
  mode_t mode;
  uid_t uid;
  gid_t gid;
  nlink_t links;
  off_t size;
  timespec modified;
  timespec changed;
  QByteArray content;
};
std::optional<Stamp> stamp(const QString& path) {
  struct stat info{};
  if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
    if (errno == ENOENT) { return std::nullopt; }
    throw std::runtime_error("Unable to identify explicit generated fixture");
  }
  QByteArray content;
  if (S_ISREG(info.st_mode)) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { throw std::runtime_error("Unable to read explicit generated fixture"); }
    content = file.readAll();
    if (file.error() != QFileDevice::NoError) { throw std::runtime_error("Generated fixture read failed"); }
  }
  return Stamp{info.st_dev, info.st_ino, info.st_mode, info.st_uid, info.st_gid, info.st_nlink,
      info.st_size, info.st_mtim, info.st_ctim, std::move(content)};
}
bool sameTime(const timespec& a, const timespec& b) { return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec; }
bool same(const std::optional<Stamp>& a, const std::optional<Stamp>& b) {
  if (a.has_value() != b.has_value()) { return false; }
  if (!a) { return true; }
  // Reading may change atime; no persistent byte or inode metadata may change.
  return a->device == b->device && a->inode == b->inode && a->mode == b->mode && a->uid == b->uid && a->gid == b->gid
      && a->links == b->links && a->size == b->size && sameTime(a->modified, b->modified)
      && sameTime(a->changed, b->changed) && a->content == b->content;
}
struct Observed { QString path; std::optional<Stamp> before; };
void unchanged(const std::vector<Observed>& files) {
  for (const auto& file : files) { QVERIFY2(same(file.before, stamp(file.path)), "Raw saved-position read changed generated persistent bytes or identity"); }
}
void write(const QString& path, const QByteArray& input) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(input) != input.size() || !file.flush()) {
    throw std::runtime_error("Unable to write exclusive generated profile fixture");
  }
}
QByteArray downloads(const QString& directory, const Positions& values) {
  std::string entries = "l";
  for (std::size_t row = 0; row < values.size(); ++row) {
    const auto suffix = std::to_string(row);
    auto record = map({{"torrent", bytes(directory.toStdString() + "/not-created-" + suffix + ".torrent")},
        {"save_dir", bytes(directory.toStdString() + "/not-created-payload")}, {"save_file", bytes("row-" + suffix + ".bin")},
        {"torrent_hash", bytes(std::string(20, static_cast<char>(row + 1)))}, {"state", "i50e"}, {"persistent", "i1e"},
        {"completedbytes", "i0e"}, {"unknown.preserved", bytes(std::string("\0\xff", 2))}});
    if (values[row]) {
      record.pop_back();
      record += bytes("position") + 'i' + std::to_string(*values[row]) + "ee";
    }
    entries += record;
  }
  entries += 'e';
  return QByteArray::fromStdString(map({{"downloads", entries}, {"unknown.root", "l4:keepe"}}));
}
} // namespace

class SavedPositionTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void rawSignedValuesAndExistingColumns_data() {
    QTest::addColumn<bool>("present");
    QTest::addColumn<qint64>("value");
    QTest::newRow("absent") << false << qint64{0};
    QTest::newRow("zero") << true << qint64{0};
    QTest::newRow("saved-minus-one") << true << qint64{-1};
    QTest::newRow("negative") << true << qint64{-7};
    QTest::newRow("positive") << true << qint64{10};
    QTest::newRow("above-int32") << true << qint64{2147483648LL};
    QTest::newRow("below-int32") << true << qint64{-2147483649LL};
    QTest::newRow("int64-min") << true << std::numeric_limits<qint64>::min();
    QTest::newRow("int64-max") << true << std::numeric_limits<qint64>::max();
  }
  void rawSignedValuesAndExistingColumns() {
    QFETCH(bool, present);
    QFETCH(qint64, value);
    ui::LibraryModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QVERIFY(model.apply(sample({present ? std::optional<qint64>{value} : std::nullopt})));
    QCOMPARE(model.columnCount(), 10);
    const QStringList oldHeaders{QStringLiteral("Name"), QStringLiteral("State"), QStringLiteral("Size (bytes)"),
        QStringLiteral("Progress"), QStringLiteral("Down (B/s)"), QStringLiteral("Up (B/s)"),
        QStringLiteral("Received (bytes)"), QStringLiteral("Sent (bytes)"), QStringLiteral("Hash")};
    for (int column = 0; column < oldHeaders.size(); ++column) {
      QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), oldHeaders[column]);
    }
    QCOMPARE(model.headerData(9, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("Saved queue position"));
    const auto index = model.index(0, 9);
    const auto numeric = model.data(index, Qt::UserRole);
    QCOMPARE(numeric.isValid(), present);
    if (present) {
      QCOMPARE(numeric.metaType().id(), QMetaType::LongLong);
      QCOMPARE(numeric.toLongLong(), value);
      QCOMPARE(model.data(index, Qt::DisplayRole).toLongLong(), value);
      QCOMPARE(model.data(index, Qt::AccessibleTextRole).toLongLong(), value);
    } else {
      QCOMPARE(model.data(index, Qt::DisplayRole).toString(), QStringLiteral("—"));
      QCOMPARE(model.data(index, Qt::AccessibleTextRole).toString(), QStringLiteral("—"));
    }
    QCOMPARE(model.data(index, ui::LibraryModel::KeyRole), model.data(model.index(0, 0), ui::LibraryModel::KeyRole));
    QCOMPARE(model.data(index, ui::LibraryModel::CompletionRole).toBool(), false);
    QCOMPARE(model.data(model.index(0, 2), Qt::UserRole).toLongLong(), qint64{100});
    QCOMPARE(model.data(model.index(0, 3), Qt::UserRole).toDouble(), 25.0);
    QVERIFY(!model.data(index, Qt::ToolTipRole).isValid());
  }

  void numericSortRetainsDuplicatesAndSelectedTarget() {
    ui::LibraryWindow window;
    window.setPresentation(sample({10, 2, -3, 0, 2}));
    auto& table = window.incompleteTable();
    auto* filter = proxy(table);
    QVERIFY(filter);
    QCOMPARE(filter->sortRole(), Qt::UserRole);
    QCOMPARE(table.horizontalHeader()->sortIndicatorSection(), 0);
    QCOMPARE(table.model()->data(table.model()->index(0, 9), Qt::UserRole).toLongLong(), qint64{10});
    QVERIFY(choose(window, table, 0));
    const auto target = window.model().targetAt(0);
    QVERIFY(target);
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    table.sortByColumn(9, Qt::AscendingOrder);
    QCOMPARE(positions(table), (QList<qint64>{-3, 0, 2, 2, 10}));
    QVERIFY(selected(window, table) == target);
    table.sortByColumn(9, Qt::DescendingOrder);
    QCOMPARE(positions(table), (QList<qint64>{10, 2, 2, 0, -3}));
    QVERIFY(selected(window, table) == target);
    QCOMPARE(window.model().rowCount(), 5);
    QCOMPARE(resets.count(), 0);
    // Source rows retain their exact duplicate/raw values after either sort.
    const qint64 raw[]{10, 2, -3, 0, 2};
    for (int row = 0; row < 5; ++row) {
      QCOMPARE(window.model().data(window.model().index(row, 9), Qt::UserRole).toLongLong(), raw[row]);
    }
  }

  void refreshPreservesIdentityAndMayClearPosition() {
    ui::LibraryWindow window;
    window.setPresentation(sample({10, 2, 0}));
    auto& table = window.incompleteTable();
    QVERIFY(choose(window, table, 0));
    const auto old = window.model().targetAt(0);
    QVERIFY(old);
    table.sortByColumn(9, Qt::AscendingOrder);
    auto next = sample({std::nullopt, -7, 0}, 2);
    std::reverse(next->items.begin(), next->items.end());
    window.setPresentation(next);
    const auto current = selected(window, table);
    QVERIFY(current);
    QCOMPARE(current->hash, old->hash);
    QCOMPARE(current->generation, quint64{2});
    QVERIFY(!window.model().accepts(*old));
    const auto row = window.model().rowFor(current->hash);
    QVERIFY(row >= 0);
    QVERIFY(!window.model().data(window.model().index(row, 9), Qt::UserRole).isValid());
    QCOMPARE(window.model().data(window.model().index(row, 9), Qt::DisplayRole).toString(), QStringLiteral("—"));
    QVERIFY(!window.model().apply(ui::Telemetry{*old, 50, 1, 1, QStringLiteral("stale")}));
    auto third = std::make_shared<ui::LibraryPresentation>(*next);
    third->revision = 3;
    third->items[static_cast<std::size_t>(row)].savedPosition = std::numeric_limits<qint64>::min();
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    window.setPresentation(third);
    const auto newest = selected(window, table);
    QVERIFY(newest);
    QCOMPARE(newest->hash, old->hash);
    QCOMPARE(newest->generation, quint64{3});
    QCOMPARE(window.model().data(window.model().index(window.model().rowFor(newest->hash), 9), Qt::UserRole).toLongLong(),
        std::numeric_limits<qint64>::min());
    QCOMPARE(resets.count(), 0);
    // Preserve row order/keys to exercise the optimized sameRows refresh, not
    // the reset path above. A fresh absent position must clear the old value.
    auto fourth = std::make_shared<ui::LibraryPresentation>(*third);
    fourth->revision = 4;
    fourth->items[static_cast<std::size_t>(row)].savedPosition.reset();
    window.setPresentation(fourth);
    const auto cleared = selected(window, table);
    QVERIFY(cleared);
    QCOMPARE(cleared->hash, newest->hash);
    QCOMPARE(cleared->generation, quint64{4});
    QVERIFY(!window.model().accepts(*newest));
    QCOMPARE(window.model().rowFor(cleared->hash), row);
    QVERIFY(!window.model().data(window.model().index(row, 9), Qt::UserRole).isValid());
    QCOMPARE(window.model().data(window.model().index(row, 9), Qt::DisplayRole).toString(), QStringLiteral("—"));
    QCOMPARE(resets.count(), 0);
  }

  void telemetryAndDetailsNeverInventPosition_data() {
    QTest::addColumn<bool>("present");
    QTest::newRow("unknown-stays-unknown") << false;
    QTest::newRow("raw-negative-stays-raw") << true;
  }
  void telemetryAndDetailsNeverInventPosition() {
    QFETCH(bool, present);
    const auto expected = present ? std::optional<qint64>{-9} : std::nullopt;
    ui::LibraryWindow window;
    window.setPresentation(sample({expected, 0}, 1, 1));
    QVERIFY(choose(window, window.incompleteTable(), 0));
    const auto target = window.model().targetAt(0);
    QVERIFY(target);
    const auto original = window.model().data(window.model().index(0, 9), Qt::UserRole);
    QSignalSpy changes(&window.model(), &QAbstractItemModel::dataChanged);
    window.setTelemetry({*target, 100, 12, 3, QStringLiteral("Complete")});
    QCOMPARE(window.incompleteTable().model()->rowCount(), 0);
    QCOMPARE(window.completeTable().model()->rowCount(), 2);
    QCOMPARE(&window.table(), &window.completeTable());
    QVERIFY(selected(window, window.completeTable()) == target);
    QCOMPARE(window.model().data(window.model().index(0, 9), Qt::UserRole), original);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(qvariant_cast<QModelIndex>(changes.first().at(0)).column(), 1);
    QCOMPARE(qvariant_cast<QModelIndex>(changes.first().at(1)).column(), 5);
    changes.clear();
    window.setDetails({*target, QStringLiteral("Renamed.bin"), 100});
    QCOMPARE(window.model().data(window.model().index(0, 9), Qt::UserRole), original);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(qvariant_cast<QModelIndex>(changes.first().at(1)).column(), 3);
    QCOMPARE(window.model().data(window.model().index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("Renamed.bin"));
    if (present) { QCOMPARE(original.toLongLong(), qint64{-9}); }
    else { QVERIFY(!original.isValid()); }
  }

  void queueColumnChooserAndResetAreIndependent() {
    ui::LibraryWindow window;
    window.setPresentation(sample({-5, 0}, 1, 1));
    auto& incomplete = window.incompleteTable();
    auto& complete = window.completeTable();
    auto* hideIncomplete = incomplete.findChild<QAction*>(QStringLiteral("classic-columns-incomplete-column-9"));
    auto* hideComplete = complete.findChild<QAction*>(QStringLiteral("classic-columns-complete-column-9"));
    auto* resetIncomplete = incomplete.findChild<QAction*>(QStringLiteral("classic-columns-incomplete-reset"));
    auto* resetComplete = complete.findChild<QAction*>(QStringLiteral("classic-columns-complete-reset"));
    QVERIFY(hideIncomplete && hideComplete && resetIncomplete && resetComplete);
    QCOMPARE(hideIncomplete->text(), QStringLiteral("Saved queue position"));
    QCOMPARE(hideComplete->text(), QStringLiteral("Saved queue position"));
    QVERIFY(hideIncomplete->isChecked() && hideComplete->isChecked());
    QVERIFY(!incomplete.isColumnHidden(9) && !complete.isColumnHidden(9));
    const auto incompleteWidth = incomplete.horizontalHeader()->sectionSize(9);
    const auto completeWidth = complete.horizontalHeader()->sectionSize(9);
    QVERIFY(incompleteWidth > 0 && completeWidth > 0);
    const auto incompleteOrder = incomplete.horizontalHeader()->visualIndex(9);
    const auto completeOrder = complete.horizontalHeader()->visualIndex(9);
    QVERIFY(choose(window, incomplete, 0));
    const auto target = window.model().targetAt(0);
    QVERIFY(target);
    incomplete.sortByColumn(9, Qt::DescendingOrder);
    hideIncomplete->trigger();
    QVERIFY(incomplete.isColumnHidden(9));
    QVERIFY(!complete.isColumnHidden(9));
    complete.horizontalHeader()->resizeSection(9, completeWidth + 37);
    complete.horizontalHeader()->moveSection(completeOrder, 0);
    hideComplete->trigger();
    incomplete.horizontalHeader()->moveSection(incompleteOrder, 0);
    resetIncomplete->trigger();
    QVERIFY(!incomplete.isColumnHidden(9));
    QVERIFY(complete.isColumnHidden(9));
    QCOMPARE(incomplete.horizontalHeader()->sectionSize(9), incompleteWidth);
    QCOMPARE(incomplete.horizontalHeader()->visualIndex(9), incompleteOrder);
    QCOMPARE(complete.horizontalHeader()->visualIndex(9), 0);
    QCOMPARE(incomplete.horizontalHeader()->sortIndicatorSection(), 9);
    QCOMPARE(incomplete.horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
    QVERIFY(selected(window, incomplete) == target);
    resetComplete->trigger();
    QVERIFY(!complete.isColumnHidden(9));
    QCOMPARE(complete.horizontalHeader()->sectionSize(9), completeWidth);
    QCOMPARE(complete.horizontalHeader()->visualIndex(9), completeOrder);
    QVERIFY(!incomplete.isColumnHidden(4) && !incomplete.isColumnHidden(5));
    QVERIFY(complete.isColumnHidden(4) && !complete.isColumnHidden(5));
  }

  void realBridgeKeepsRawPositionsAndProfileUnchanged() {
    QTemporaryDir profile(QStringLiteral("/tmp/biglybtc-saved-position-XXXXXX"));
    QVERIFY(profile.isValid());
    const Positions expected{std::nullopt, 0, -7, 4294967296LL,
        std::numeric_limits<qint64>::min(), std::numeric_limits<qint64>::max(), 0};
    const auto config = profile.path() + QStringLiteral("/downloads.config");
    write(config, downloads(profile.path(), expected));
    write(config + QStringLiteral(".bak"), "synthetic backup kept unchanged");
    write(config + QStringLiteral(".saving"), "synthetic saving kept unchanged");
    const std::vector<Observed> inputs{{config, stamp(config)},
        {config + QStringLiteral(".bak"), stamp(config + QStringLiteral(".bak"))},
        {config + QStringLiteral(".saving"), stamp(config + QStringLiteral(".saving"))}};
    ui::Presentation retained;
    {
      app::SessionBridge bridge;
      QSignalSpy results(&bridge, &app::SessionBridge::snapshotReady);
      bool guiThread = false;
      QObject::connect(&bridge, &app::SessionBridge::snapshotReady, &bridge,
          [&](const ui::Presentation&) { guiThread = QThread::currentThread() == qApp->thread(); }, Qt::DirectConnection);
      QVERIFY(bridge.open(std::filesystem::path(profile.path().toStdString()), 0));
      QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1 && !bridge.busy(), 30000);
      QVERIFY(guiThread);
      unchanged(inputs);
      std::vector<Observed> baseline{{profile.path(), stamp(profile.path())},
          {profile.path() + QStringLiteral("/.azlock"), stamp(profile.path() + QStringLiteral("/.azlock"))},
          {profile.path() + QStringLiteral("/active"), stamp(profile.path() + QStringLiteral("/active"))},
          {profile.path() + QStringLiteral("/not-created-payload"), stamp(profile.path() + QStringLiteral("/not-created-payload"))}};
      for (std::size_t row = 0; row < expected.size(); ++row) {
        const auto path = profile.path() + QStringLiteral("/not-created-%1.torrent").arg(row);
        baseline.push_back({path, stamp(path)});
        QVERIFY(!baseline.back().before);
      }
      retained = qvariant_cast<ui::Presentation>(results.first().at(0));
      QVERIFY(retained);
      QCOMPARE(retained->items.size(), expected.size());
      ui::LibraryModel model;
      QVERIFY(model.apply(retained));
      for (std::size_t row = 0; row < expected.size(); ++row) {
        QCOMPARE(retained->items[row].hash,
            QString::fromLatin1(QByteArray(20, static_cast<char>(row + 1)).toHex().toUpper()));
        QCOMPARE(retained->items[row].name, QStringLiteral("row-%1.bin").arg(row));
        QVERIFY(retained->items[row].savedPosition == expected[row]);
        const auto value = model.data(model.index(static_cast<int>(row), 9), Qt::UserRole);
        QCOMPARE(value.isValid(), expected[row].has_value());
        if (expected[row]) { QCOMPARE(value.toLongLong(), *expected[row]); }
        QVERIFY(!retained->items[row].canContinue); // No owned peer port was supplied.
      }
      QVERIFY(bridge.refresh());
      QTRY_VERIFY_WITH_TIMEOUT(results.count() == 2 && !bridge.busy(), 30000);
      const auto refreshed = qvariant_cast<ui::Presentation>(results.last().at(0));
      QVERIFY(refreshed);
      QVERIFY(refreshed->revision > retained->revision);
      QCOMPARE(refreshed->items.size(), expected.size());
      for (std::size_t row = 0; row < expected.size(); ++row) {
        QVERIFY(refreshed->items[row].savedPosition == expected[row]);
        QVERIFY(retained->items[row].savedPosition == expected[row]);
      }
      unchanged(inputs);
      unchanged(baseline);
    }
    QVERIFY(retained);
    QVERIFY(retained->items[3].savedPosition == std::optional<qint64>{4294967296LL});
    unchanged(inputs);
    auto released = storage::ProfileLease::acquire(std::filesystem::path(profile.path().toStdString()));
    QVERIFY(released.has_value());
  }
};

QTEST_MAIN(SavedPositionTests)
#include "saved_position_tests.moc"
