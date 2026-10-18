#include "library_window.hpp"
#include "profile.hpp"
#include "session_bridge.hpp"
#include "biglybtc/saved_dnd.hpp"

#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QDir>
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
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <vector>
#include <zlib.h>

// Labelled saved snapshot columns only. No original-runtime/live-DND/pane
// equivalence claim. The one Bridge fixture uses only explicit generated files.
namespace {
using namespace biglybtc;
using O = std::optional<qint64>;
ui::SavedSelectedValues values(const O& size, const O& remaining, const std::optional<qint32>& done) {
  return {.bytes = size, .remaining = remaining, .percentThousandths = done};
}
bool sameValues(const std::optional<ui::SavedSelectedValues>& a, const std::optional<ui::SavedSelectedValues>& b) {
  if (a.has_value() != b.has_value()) { return false; }
  return !a || (a->bytes == b->bytes && a->remaining == b->remaining && a->percentThousandths == b->percentThousandths
      && a->defaults == b->defaults && a->diagnostics == b->diagnostics);
}
std::shared_ptr<ui::LibraryPresentation> sample(quint64 revision = 1, int count = 3, int complete = 2) {
  auto value = std::make_shared<ui::LibraryPresentation>();
  value->revision = revision;
  for (int row = 0; row < count; ++row) {
    const auto hash = QStringLiteral("%1").arg(row + 1, 40, 16, QChar('0')).toUpper();
    value->items.push_back({.key = hash, .hash = hash, .name = QStringLiteral("Row %1.bin").arg(row),
        .state = row == complete ? QStringLiteral("Stopped") : QStringLiteral("Queued"),
        .bytes = 100, .completed = row == complete ? 100 : 25, .received = 1000, .sent = 0,
        .canContinue = row != complete, .savedPosition = row, .savedSelected = values(80, 55, 312)});
  }
  return value;
}
QSortFilterProxyModel* proxy(QTableView& table) { return qobject_cast<QSortFilterProxyModel*>(table.model()); }
bool choose(ui::LibraryWindow& window, int row = 0) {
  auto& table = window.incompleteTable();
  auto* filter = proxy(table);
  if (!filter) { return false; }
  const auto index = filter->mapFromSource(window.model().index(row, 0));
  if (!index.isValid()) { return false; }
  table.setFocus();
  table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  return table.selectionModel()->isSelected(index);
}
std::optional<ui::Target> selected(ui::LibraryWindow& window) {
  auto& table = window.table();
  auto* filter = proxy(table);
  return filter && table.currentIndex().isValid()
      ? window.model().targetAt(filter->mapToSource(table.currentIndex()).row()) : std::nullopt;
}
QList<qint64> knownNumbers(QTableView& table, int column) {
  QList<qint64> result;
  for (int row = 0; row < table.model()->rowCount(); ++row) {
    const auto value = table.model()->data(table.model()->index(row, column), Qt::UserRole);
    if (value.isValid()) { result.push_back(value.toLongLong()); }
  }
  return result;
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string integer(qint64 value) { return 'i' + std::to_string(value) + 'e'; }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string output = "d";
  for (const auto& [key, value] : fields) { output += bytes(key) + value; }
  return output + 'e';
}
std::string dictWith(std::string dictionary, std::string_view key, std::string value) {
  dictionary.pop_back();
  return dictionary + bytes(key) + value + 'e';
}
QByteArray gzip(std::string_view input) {
  if (input.size() > 65536) { throw std::runtime_error("Generated cache fixture exceeds its small explicit bound"); }
  std::string output(static_cast<std::size_t>(::compressBound(static_cast<uLong>(input.size()))) + 32, '\0');
  z_stream stream{};
  if (::deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    throw std::runtime_error("Unable to initialize generated gzip fixture");
  }
  struct Cleanup { z_stream& stream; ~Cleanup() { ::deflateEnd(&stream); } } cleanup{stream};
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = reinterpret_cast<Bytef*>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());
  if (::deflate(&stream, Z_FINISH) != Z_STREAM_END || stream.avail_in != 0) { throw std::runtime_error("Generated gzip fixture failed"); }
  output.resize(output.size() - stream.avail_out);
  return QByteArray::fromStdString(output);
}
void write(const QString& path, const QByteArray& input) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(input) != input.size() || !file.flush()) {
    throw std::runtime_error("Unable to create exclusive generated fixture");
  }
}
struct Stamp {
  dev_t device; ino_t inode; mode_t mode; uid_t uid; gid_t gid; nlink_t links; off_t size;
  timespec modified; timespec changed; QByteArray content;
};
std::optional<Stamp> stamp(const QString& path) {
  struct stat info{};
  if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
    if (errno == ENOENT) { return std::nullopt; }
    throw std::runtime_error("Unable to inspect explicit generated fixture");
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
  return !a || (a->device == b->device && a->inode == b->inode && a->mode == b->mode && a->uid == b->uid && a->gid == b->gid
      && a->links == b->links && a->size == b->size && sameTime(a->modified, b->modified)
      && sameTime(a->changed, b->changed) && a->content == b->content);
}
struct Observed { QString path; std::optional<Stamp> before; };
void unchanged(const std::vector<Observed>& before) {
  for (const auto& file : before) { QVERIFY2(same(file.before, stamp(file.path)), "Saved selected read changed generated bytes or file identity (atime excluded)"); }
}
QString hashAt(int row) { return QString::fromLatin1(QByteArray(20, static_cast<char>(row + 1)).toHex().toUpper()); }
} // namespace

class SavedSelectedTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void independentValuesAndUnknownRoles_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("absent-envelope") << 0;
    QTest::newRow("present-unknown") << 1;
    QTest::newRow("zero-size-independent-done") << 2;
    QTest::newRow("remaining-without-size") << 3;
    QTest::newRow("done-without-size-or-remaining") << 4;
    QTest::newRow("all-known") << 5;
  }
  void independentValuesAndUnknownRoles() {
    QFETCH(int, kind);
    auto input = sample(1, 1, -1);
    std::optional<ui::SavedSelectedValues> expected;
    if (kind == 1) { expected.emplace(); }
    else if (kind == 2) { expected = values(0, {}, 1000); }
    else if (kind == 3) { expected = values({}, 7, {}); }
    else if (kind == 4) { expected = values({}, {}, 1); }
    else if (kind == 5) { expected = values(80, 55, 312); }
    if (expected) { expected->defaults = 0xABCDEF01U; expected->diagnostics = 0xDEADBEEFU; }
    input->items[0].savedSelected = expected;
    ui::LibraryModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QVERIFY(model.apply(input));
    QCOMPARE(model.columnCount(), 13);
    const QStringList oldHeaders{QStringLiteral("Name"), QStringLiteral("State"), QStringLiteral("Size (bytes)"),
        QStringLiteral("Progress"), QStringLiteral("Down (B/s)"), QStringLiteral("Up (B/s)"),
        QStringLiteral("Received (bytes)"), QStringLiteral("Sent (bytes)"), QStringLiteral("Hash"), QStringLiteral("Saved queue position")};
    for (int column = 0; column < oldHeaders.size(); ++column) {
      QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), oldHeaders[column]);
    }
    const QStringList labels{QStringLiteral("Saved selected size (bytes)"), QStringLiteral("Saved selected remaining (bytes)"),
        QStringLiteral("Saved selected done")};
    for (int column = 10; column <= 12; ++column) {
      const auto index = model.index(0, column);
      QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), labels[column - 10]);
      const auto tooltip = model.data(index, Qt::ToolTipRole).toString();
      QVERIFY(!tooltip.isEmpty());
      QVERIFY(tooltip.contains(QStringLiteral("saved"), Qt::CaseInsensitive));
      QVERIFY(!tooltip.contains(QStringLiteral("DEADBEEF"), Qt::CaseInsensitive));
      QVERIFY(!tooltip.contains(QString::number(0xDEADBEEFU)));
      QVERIFY(!tooltip.contains(QStringLiteral("ABCDEF01"), Qt::CaseInsensitive));
      QVERIFY(!tooltip.contains(QString::number(0xABCDEF01U)));
      QVERIFY(!model.headerData(column, Qt::Horizontal, Qt::ToolTipRole).toString().isEmpty());
      std::optional<qint64> number;
      if (expected) {
        if (column == 10) { number = expected->bytes; }
        else if (column == 11) { number = expected->remaining; }
        else if (expected->percentThousandths) { number = *expected->percentThousandths; }
      }
      const auto numeric = model.data(index, Qt::UserRole);
      QCOMPARE(numeric.isValid(), number.has_value());
      if (number) {
        QCOMPARE(numeric.metaType().id(), column == 12 ? QMetaType::Int : QMetaType::LongLong);
        QCOMPARE(numeric.toLongLong(), *number);
        if (column != 12) {
          QCOMPARE(model.data(index, Qt::DisplayRole).toLongLong(), *number);
          QCOMPARE(model.data(index, Qt::AccessibleTextRole).toLongLong(), *number);
        }
      } else {
        QCOMPARE(model.data(index, Qt::DisplayRole).toString(), QStringLiteral("—"));
        QCOMPARE(model.data(index, Qt::AccessibleTextRole).toString(), QStringLiteral("—"));
      }
      QCOMPARE(model.data(index, ui::LibraryModel::KeyRole), model.data(model.index(0, 0), ui::LibraryModel::KeyRole));
      QCOMPARE(model.data(index, ui::LibraryModel::CompletionRole).toBool(), false);
    }
    QCOMPARE(model.data(model.index(0, 2), Qt::UserRole).toLongLong(), qint64{100});
    QCOMPARE(model.data(model.index(0, 3), Qt::UserRole).toDouble(), 25.0);
    QVERIFY(!model.data(model.index(0, 2), Qt::ToolTipRole).isValid());
  }

  void exactPermilleDisplay_data() {
    QTest::addColumn<qint32>("number");
    QTest::addColumn<QString>("text");
    QTest::newRow("zero") << qint32{0} << QStringLiteral("0.0%");
    QTest::newRow("one") << qint32{1} << QStringLiteral("0.1%");
    QTest::newRow("nine") << qint32{9} << QStringLiteral("0.9%");
    QTest::newRow("ten") << qint32{10} << QStringLiteral("1.0%");
    QTest::newRow("fraction") << qint32{312} << QStringLiteral("31.2%");
    QTest::newRow("half") << qint32{500} << QStringLiteral("50.0%");
    QTest::newRow("almost") << qint32{999} << QStringLiteral("99.9%");
    QTest::newRow("complete") << qint32{1000} << QStringLiteral("100.0%");
  }
  void exactPermilleDisplay() {
    QFETCH(qint32, number);
    QFETCH(QString, text);
    auto input = sample(1, 1, -1);
    input->items[0].savedSelected = values({}, {}, number);
    ui::LibraryModel model;
    QVERIFY(model.apply(input));
    const auto index = model.index(0, 12);
    QCOMPARE(model.data(index, Qt::UserRole).metaType().id(), QMetaType::Int);
    QCOMPARE(model.data(index, Qt::UserRole).toInt(), number);
    QCOMPARE(model.data(index, Qt::DisplayRole).toString(), text);
    QCOMPARE(model.data(index, Qt::AccessibleTextRole).toString(), text);
  }

  void numericSortPreservesTargetAndFullPaneRule() {
    ui::LibraryWindow window;
    auto input = sample(1, 6, -1);
    const O byteValues[]{10, 2, {}, 0, std::numeric_limits<qint64>::max(), 2};
    const std::optional<qint32> doneValues[]{1000, 9, {}, 1, 999, 10};
    for (int row = 0; row < 6; ++row) { input->items[row].savedSelected = values(byteValues[row], byteValues[row], doneValues[row]); }
    window.setPresentation(input);
    auto& table = window.incompleteTable();
    auto* sorted = proxy(table);
    QVERIFY(sorted);
    QCOMPARE(sorted->sortRole(), Qt::UserRole);
    QCOMPARE(table.horizontalHeader()->sortIndicatorSection(), 0);
    QVERIFY(choose(window));
    const auto target = window.model().targetAt(0);
    QVERIFY(target);
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    table.sortByColumn(10, Qt::AscendingOrder);
    QCOMPARE(knownNumbers(table, 10), (QList<qint64>{0, 2, 2, 10, std::numeric_limits<qint64>::max()}));
    QVERIFY(selected(window) == target);
    table.sortByColumn(11, Qt::DescendingOrder);
    QCOMPARE(knownNumbers(table, 11), (QList<qint64>{std::numeric_limits<qint64>::max(), 10, 2, 2, 0}));
    QVERIFY(selected(window) == target);
    table.sortByColumn(12, Qt::AscendingOrder);
    QCOMPARE(knownNumbers(table, 12), (QList<qint64>{1, 9, 10, 999, 1000}));
    QVERIFY(selected(window) == target);
    QCOMPARE(table.model()->rowCount(), 6);
    QCOMPARE(window.completeTable().model()->rowCount(), 0);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(window.model().data(window.model().index(0, 3), Qt::UserRole).toDouble(), 25.0);
  }

  void invalidNewFieldsRejectBeforeAnyMutation_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("negative-selected-size") << 0;
    QTest::newRow("negative-selected-remaining") << 1;
    QTest::newRow("remaining-exceeds-known-size") << 2;
    QTest::newRow("negative-permille") << 3;
    QTest::newRow("permille-above1000") << 4;
  }
  void invalidNewFieldsRejectBeforeAnyMutation() {
    QFETCH(int, kind);
    ui::LibraryWindow window;
    auto input = sample();
    window.setPresentation(input);
    QVERIFY(choose(window));
    const auto target = window.model().targetAt(0);
    QVERIFY(target);
    QList<QVariant> before;
    for (int row = 0; row < window.model().rowCount(); ++row) {
      for (int column = 0; column < 13; ++column) {
        before.push_back(window.model().data(window.model().index(row, column), Qt::DisplayRole));
        before.push_back(window.model().data(window.model().index(row, column), Qt::UserRole));
      }
    }
    auto bad = std::make_shared<ui::LibraryPresentation>(*input);
    bad->revision = 2;
    bad->items[0].name = QStringLiteral("must-not-commit-earlier-valid-row");
    auto& invalid = *bad->items[1].savedSelected;
    if (kind == 0) { invalid.bytes = -1; }
    else if (kind == 1) { invalid.remaining = -1; }
    else if (kind == 2) { invalid.bytes = 0; invalid.remaining = 1; }
    else if (kind == 3) { invalid.percentThousandths = -1; }
    else { invalid.percentThousandths = 1001; }
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    QSignalSpy changes(&window.model(), &QAbstractItemModel::dataChanged);
    QVERIFY(!window.model().apply(bad));
    QVERIFY(window.model().accepts(*target));
    QVERIFY(selected(window) == target);
    QCOMPARE(window.model().rowCount(), 3);
    int at = 0;
    for (int row = 0; row < window.model().rowCount(); ++row) {
      for (int column = 0; column < 13; ++column) {
        QCOMPARE(window.model().data(window.model().index(row, column), Qt::DisplayRole), before.at(at++));
        QCOMPARE(window.model().data(window.model().index(row, column), Qt::UserRole), before.at(at++));
      }
    }
    QCOMPARE(changes.count(), 0);
    QCOMPARE(resets.count(), 0);
  }

  void newerSnapshotsClearSameRowsAndRestoreSelectionAfterReorder() {
    ui::LibraryWindow window;
    auto input = sample();
    window.setPresentation(input);
    QVERIFY(choose(window));
    const auto old = selected(window);
    QVERIFY(old);
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    auto next = std::make_shared<ui::LibraryPresentation>(*input);
    next->revision = 2;
    next->items[0].savedSelected.reset();
    next->items[1].savedSelected = values(70, {}, {});
    next->items[2].savedSelected.emplace();
    window.setPresentation(next);
    QCOMPARE(resets.count(), 0);
    const auto current = selected(window);
    QVERIFY(current);
    QCOMPARE(current->hash, old->hash);
    QCOMPARE(current->generation, quint64{2});
    QVERIFY(!window.model().accepts(*old));
    for (int column = 10; column <= 12; ++column) {
      QVERIFY(!window.model().data(window.model().index(0, column), Qt::UserRole).isValid());
      QVERIFY(!window.model().data(window.model().index(2, column), Qt::UserRole).isValid());
    }
    QCOMPARE(window.model().data(window.model().index(1, 10), Qt::UserRole).toLongLong(), qint64{70});
    QVERIFY(!window.model().data(window.model().index(1, 11), Qt::UserRole).isValid());
    QVERIFY(!window.model().data(window.model().index(1, 12), Qt::UserRole).isValid());
    auto reordered = std::make_shared<ui::LibraryPresentation>(*next);
    reordered->revision = 3;
    std::reverse(reordered->items.begin(), reordered->items.end());
    window.setPresentation(reordered);
    const auto latest = selected(window);
    QVERIFY(latest);
    QCOMPARE(latest->hash, old->hash);
    QCOMPARE(latest->generation, quint64{3});
    QVERIFY(!window.model().data(window.model().index(window.model().rowFor(latest->hash), 10), Qt::UserRole).isValid());
  }

  void telemetryAndDetailsKeepSavedValuesWithoutChangingCompletionPolicy() {
    ui::LibraryWindow window;
    auto input = sample(1, 2, -1);
    input->items[0].savedSelected = values(0, {}, 1000);
    input->items[1].savedSelected.reset();
    const auto expected = input->items[0].savedSelected;
    window.setPresentation(input);
    QVERIFY(choose(window));
    const auto target = selected(window);
    QVERIFY(target);
    QCOMPARE(window.incompleteTable().model()->rowCount(), 2);
    QCOMPARE(window.completeTable().model()->rowCount(), 0);
    QCOMPARE(window.model().data(window.model().index(0, 12), Qt::DisplayRole).toString(), QStringLiteral("100.0%"));
    QCOMPARE(window.model().data(window.model().index(0, 3), Qt::UserRole).toDouble(), 25.0);
    QVERIFY(window.model().completionAt(0) == std::optional<bool>{false});
    window.setTelemetry({*target, 50, 4, 2, QStringLiteral("Downloading")});
    QCOMPARE(window.model().data(window.model().index(0, 3), Qt::UserRole).toDouble(), 50.0);
    window.setDetails({*target, QStringLiteral("Renamed.bin"), 200});
    QCOMPARE(window.model().data(window.model().index(0, 2), Qt::UserRole).toLongLong(), qint64{200});
    QCOMPARE(window.model().data(window.model().index(0, 3), Qt::UserRole).toDouble(), 25.0);
    QCOMPARE(window.model().data(window.model().index(0, 10), Qt::UserRole).toLongLong(), qint64{0});
    QVERIFY(!window.model().data(window.model().index(0, 11), Qt::UserRole).isValid());
    QCOMPARE(window.model().data(window.model().index(0, 12), Qt::UserRole).toInt(), expected->percentThousandths.value());
    window.setTelemetry({*target, 200, 0, 0, QStringLiteral("Complete")});
    QCOMPARE(window.incompleteTable().model()->rowCount(), 1);
    QCOMPARE(window.completeTable().model()->rowCount(), 1);
    QCOMPARE(&window.table(), &window.completeTable());
    QVERIFY(selected(window) == target);
    QCOMPARE(window.model().data(window.model().index(0, 12), Qt::DisplayRole).toString(), QStringLiteral("100.0%"));
    const auto other = window.model().targetAt(1);
    QVERIFY(other);
    window.setTelemetry({*other, 30, 1, 1, QStringLiteral("Downloading")});
    window.setDetails({*other, QStringLiteral("Other.bin"), 100});
    for (int column = 10; column <= 12; ++column) { QVERIFY(!window.model().data(window.model().index(1, column), Qt::UserRole).isValid()); }
    // Do not claim these saved values were recalculated from the new live C/T.
  }

  void chooserDefaultsAndResetKeepBothPanesIndependent() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    QVERIFY(choose(window));
    const auto target = selected(window);
    QVERIFY(target);
    auto& incomplete = window.incompleteTable();
    auto& complete = window.completeTable();
    bool oldIncomplete[10]{};
    bool oldComplete[10]{};
    for (int column = 0; column < 10; ++column) {
      oldIncomplete[column] = incomplete.isColumnHidden(column);
      oldComplete[column] = complete.isColumnHidden(column);
    }
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    for (int column = 10; column <= 12; ++column) {
      auto* toggle = incomplete.findChild<QAction*>(QStringLiteral("classic-columns-incomplete-column-%1").arg(column));
      auto* other = complete.findChild<QAction*>(QStringLiteral("classic-columns-complete-column-%1").arg(column));
      QVERIFY(toggle && other);
      QVERIFY(incomplete.isColumnHidden(column) && complete.isColumnHidden(column));
      QVERIFY(!toggle->isChecked() && !other->isChecked());
      QCOMPARE(toggle->text(), window.model().headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
      toggle->trigger();
      QVERIFY(!incomplete.isColumnHidden(column));
      QVERIFY(complete.isColumnHidden(column));
    }
    auto* completeRemaining = complete.findChild<QAction*>(QStringLiteral("classic-columns-complete-column-11"));
    auto* resetIncomplete = incomplete.findChild<QAction*>(QStringLiteral("classic-columns-incomplete-reset"));
    auto* resetComplete = complete.findChild<QAction*>(QStringLiteral("classic-columns-complete-reset"));
    QVERIFY(completeRemaining && resetIncomplete && resetComplete);
    completeRemaining->trigger();
    incomplete.horizontalHeader()->moveSection(incomplete.horizontalHeader()->visualIndex(10), 0);
    incomplete.sortByColumn(12, Qt::DescendingOrder);
    resetIncomplete->trigger();
    for (int column = 10; column <= 12; ++column) { QVERIFY(incomplete.isColumnHidden(column)); }
    QVERIFY(!complete.isColumnHidden(11));
    resetComplete->trigger();
    for (int column = 10; column <= 12; ++column) { QVERIFY(complete.isColumnHidden(column)); }
    for (int column = 0; column < 10; ++column) {
      QCOMPARE(incomplete.isColumnHidden(column), oldIncomplete[column]);
      QCOMPARE(complete.isColumnHidden(column), oldComplete[column]);
    }
    QCOMPARE(incomplete.horizontalHeader()->visualIndex(10), 10);
    QCOMPARE(incomplete.horizontalHeader()->sortIndicatorSection(), 12);
    QCOMPARE(incomplete.horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
    QVERIFY(selected(window) == target);
    QCOMPARE(resets.count(), 0);
  }

  void actualBridgeUsesCurrentCacheRawCAndNeverKnownGeometryFallback() {
    QTemporaryDir profile(QStringLiteral("/tmp/biglybtc-saved-selected-XXXXXX"));
    QVERIFY(profile.isValid());
    QVERIFY(QDir(profile.path()).mkdir(QStringLiteral("active")));
    const O completed[]{25, {}, -1, 25, 0, 50, 90};
    std::string inventory = "l";
    std::string cached = "l";
    for (int row = 0; row < 7; ++row) {
      auto saved = map({{"torrent", bytes(profile.path().toStdString() + "/not-created-" + std::to_string(row) + ".torrent")},
          {"save_dir", bytes(profile.path().toStdString() + "/not-created-payload")}, {"save_file", bytes("row-" + std::to_string(row) + ".bin")},
          {"torrent_hash", bytes(std::string(20, static_cast<char>(row + 1)))}, {"state", integer(50)}, {"downloaded", integer(99999)}});
      if (completed[row]) { saved = dictWith(std::move(saved), "completedbytes", integer(*completed[row])); }
      inventory += saved;
      if (row == 5) { continue; } // No matching cache source for this identity.
      std::string attrs = "de";
      if (row != 3) { attrs = dictWith(std::move(attrs), "skipfss", integer(row == 2 ? 100 : row == 4 ? 0 : 20)); }
      if (row != 0) { attrs = dictWith(std::move(attrs), "skipbdl", row == 2 ? "3:bad" : integer(0)); }
      attrs = dictWith(std::move(attrs), "parameters", map({{"dndflags", integer(3)}}));
      attrs = dictWith(std::move(attrs), "displayname", bytes("saved-" + std::to_string(row)));
      const auto record = map({{"hash", bytes(std::string(20, static_cast<char>(row + 1)))}, {"size", integer(row == 4 ? 0 : 100)},
          {"name", bytes("cache-name")}, {"attributes", attrs}});
      cached += record;
    }
    inventory += 'e'; cached += 'e';
    const auto config = profile.path() + QStringLiteral("/downloads.config");
    const auto cachePath = profile.path() + QStringLiteral("/active/cache.dat");
    write(config, QByteArray::fromStdString(map({{"downloads", inventory}, {"unknown", bytes(std::string("\0\xff", 2))}})));
    write(config + QStringLiteral(".bak"), "synthetic untouched backup");
    write(config + QStringLiteral(".saving"), "synthetic untouched saving");
    write(cachePath, gzip(map({{"state", cached}, {"unknown", "4:keep"}})));
    const std::vector<Observed> inputs{{config, stamp(config)},
        {config + QStringLiteral(".bak"), stamp(config + QStringLiteral(".bak"))},
        {config + QStringLiteral(".saving"), stamp(config + QStringLiteral(".saving"))}, {cachePath, stamp(cachePath)}};
    bool guiThread = false;
    app::SessionBridge bridge;
    QSignalSpy results(&bridge, &app::SessionBridge::snapshotReady);
    QObject::connect(&bridge, &app::SessionBridge::snapshotReady, &bridge,
        [&](const ui::Presentation&) { guiThread = QThread::currentThread() == qApp->thread(); }, Qt::DirectConnection);
    QVERIFY(bridge.open(std::filesystem::path(profile.path().toStdString()), 0));
    QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1 && !bridge.busy(), 30000);
    QVERIFY(guiThread);
    const auto first = qvariant_cast<ui::Presentation>(results.first().at(0));
    QVERIFY(first);
    QCOMPARE(first->items.size(), std::size_t{7});
    for (int row = 0; row < 7; ++row) { QCOMPARE(first->items[row].hash, hashAt(row)); }
    QVERIFY(first->items[0].savedSelected);
    const auto& normal = *first->items[0].savedSelected;
    QVERIFY(normal.bytes == 80 && normal.remaining == 55 && normal.percentThousandths == 312);
    QVERIFY(core::hasSavedDndDefault(normal.defaults, core::SavedDndDefault::SkippedDownloadedBytes));
    QVERIFY(first->items[1].savedSelected);
    const auto& missingC = *first->items[1].savedSelected;
    QVERIFY(missingC.bytes == 80 && !missingC.remaining && !missingC.percentThousandths);
    QVERIFY(core::hasSavedDndIssue(missingC.diagnostics, core::SavedDndIssue::CompletedMissing));
    QVERIFY(first->items[2].savedSelected);
    const auto& negativeC = *first->items[2].savedSelected;
    QVERIFY(negativeC.bytes == 0 && !negativeC.remaining && negativeC.percentThousandths == 1000);
    QVERIFY(core::hasSavedDndIssue(negativeC.diagnostics, core::SavedDndIssue::CompletedRange));
    QVERIFY(!core::hasSavedDndIssue(negativeC.diagnostics, core::SavedDndIssue::CompletedMissing));
    QVERIFY(core::hasSavedDndIssue(negativeC.diagnostics, core::SavedDndIssue::SkippedDownloadedBytesType));
    QVERIFY(!first->items[2].completed); // Existing full-progress C remains sanitized independently.
    QVERIFY(first->items[3].savedSelected);
    QVERIFY(!first->items[3].savedSelected->bytes);
    QVERIFY(core::hasSavedDndDefault(first->items[3].savedSelected->defaults, core::SavedDndDefault::SkippedBytes));
    QVERIFY(core::hasSavedDndIssue(first->items[3].savedSelected->diagnostics, core::SavedDndIssue::SkippedUnresolved));
    QVERIFY(first->items[4].savedSelected);
    QVERIFY(!first->items[4].savedSelected->bytes && !first->items[4].savedSelected->percentThousandths);
    QVERIFY(core::hasSavedDndIssue(first->items[4].savedSelected->diagnostics, core::SavedDndIssue::TotalUnresolved));
    QVERIFY(!first->items[5].savedSelected);
    QVERIFY(first->items[6].savedSelected);
    QVERIFY(first->items[6].savedSelected->bytes == 80 && !first->items[6].savedSelected->remaining && !first->items[6].savedSelected->percentThousandths);
    QVERIFY(core::hasSavedDndIssue(first->items[6].savedSelected->diagnostics, core::SavedDndIssue::CountersInconsistent));
    ui::LibraryModel model;
    QVERIFY(model.apply(first));
    QVERIFY(!model.completionAt(2).has_value());
    QCOMPARE(model.data(model.index(2, 0), ui::LibraryModel::CompletionRole).toBool(), false);
    QCOMPARE(model.data(model.index(2, 12), Qt::DisplayRole).toString(), QStringLiteral("100.0%"));
    QCOMPARE(model.data(model.index(0, 3), Qt::UserRole).toDouble(), 25.0);
    unchanged(inputs);
    std::vector<Observed> other{{profile.path(), stamp(profile.path())},
        {profile.path() + QStringLiteral("/.azlock"), stamp(profile.path() + QStringLiteral("/.azlock"))},
        {profile.path() + QStringLiteral("/active"), stamp(profile.path() + QStringLiteral("/active"))},
        {profile.path() + QStringLiteral("/not-created-payload"), stamp(profile.path() + QStringLiteral("/not-created-payload"))}};
    for (int row = 0; row < 7; ++row) {
      const auto path = profile.path() + QStringLiteral("/not-created-%1.torrent").arg(row);
      other.push_back({path, stamp(path)});
      QVERIFY(!other.back().before);
    }
    QVERIFY(bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(results.count() == 2 && !bridge.busy(), 30000);
    const auto reread = qvariant_cast<ui::Presentation>(results.last().at(0));
    QVERIFY(reread);
    QCOMPARE(reread->items.size(), std::size_t{7});
    for (int row = 0; row < 7; ++row) { QVERIFY(sameValues(reread->items[row].savedSelected, first->items[row].savedSelected)); }
    unchanged(inputs); unchanged(other);
    // This explicit test mutation removes only the private fixture's current
    // cache. Baseline again after our rename; source reads must not modify it.
    const auto held = cachePath + QStringLiteral(".held");
    QVERIFY(QFile::rename(cachePath, held));
    std::vector<Observed> afterRename{{config, stamp(config)}, {held, stamp(held)}, {cachePath, stamp(cachePath)},
        {profile.path(), stamp(profile.path())}, {profile.path() + QStringLiteral("/active"), stamp(profile.path() + QStringLiteral("/active"))}};
    QVERIFY(!afterRename[2].before);
    afterRename.push_back(inputs[1]);
    afterRename.push_back(inputs[2]);
    for (const auto& file : other) {
      if (file.path != profile.path() && file.path != profile.path() + QStringLiteral("/active")) { afterRename.push_back(file); }
    }
    QVERIFY(bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(results.count() == 3 && !bridge.busy(), 30000);
    const auto noCache = qvariant_cast<ui::Presentation>(results.last().at(0));
    QVERIFY(noCache);
    QCOMPARE(noCache->items.size(), std::size_t{7});
    QVERIFY(noCache->revision > reread->revision);
    QVERIFY(noCache->items[0].bytes == 100); // Old full-size Known fallback remains available.
    QCOMPARE(noCache->items[0].name, first->items[0].name);
    for (int row = 0; row < 7; ++row) { QVERIFY(!noCache->items[row].savedSelected); }
    QVERIFY(!noCache->items[5].bytes); // Never had a matching current or remembered cache row.
    QVERIFY(first->items[0].savedSelected->bytes == 80 && first->items[0].savedSelected->remaining == 55);
    unchanged(afterRename);
  }
};

QTEST_MAIN(SavedSelectedTests)
#include "saved_selected_tests.moc"
