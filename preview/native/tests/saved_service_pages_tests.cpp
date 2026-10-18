#include "saved_service_pages.hpp"
#include "biglybtc/saved_services.hpp"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QByteArray>
#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>
#include <QtTest>

#include <limits>
#include <type_traits>

// Synthetic DTO/core-codec inputs only; no original Java or service equality.
namespace {
using namespace biglybtc;
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string output = "d";
  for (const auto& [key, value] : fields) { output += bytes(key) + value; }
  return output + 'e';
}
std::shared_ptr<const core::SavedHosting> hosting(std::string input) {
  auto document = core::Document::parse(std::move(input));
  if (!document) { return {}; }
  auto result = core::readSavedHosting(*document);
  return result ? std::make_shared<const core::SavedHosting>(std::move(*result)) : nullptr;
}
std::shared_ptr<const core::SavedShares> shares(std::string input) {
  auto document = core::Document::parse(std::move(input));
  if (!document) { return {}; }
  auto result = core::readSavedShares(*document);
  return result ? std::make_shared<const core::SavedShares>(std::move(*result)) : nullptr;
}
std::string stats(int completed, int uploaded) {
  return map({{"completed", "i" + std::to_string(completed) + 'e'}, {"announces", "i2e"},
      {"uploaded", "i" + std::to_string(uploaded) + 'e'}, {"downloaded", "i100e"}});
}
std::shared_ptr<const core::SavedHosting> hostingSample() {
  const auto first = map({{"hash", bytes(std::string(20, 'A'))}, {"status", "i0e"}, {"dateadded", "i0e"},
      {"stats", stats(100, 1000)}, {"torrent_file", bytes("HOSTING_PATH_MUST_NOT_RENDER")}});
  const auto second = map({{"hash", bytes(std::string(20, 'B'))}, {"persistent", "i0e"},
      {"backup_time", "i123e"}, {"stats", stats(9, 9)}});
  const auto fourth = map({{"hash", bytes(std::string(20, 'C'))}, {"status", "i7e"},
      {"passive", "i1e"}, {"dateadded", "i9e"}, {"stats", stats(1000, 100)}});
  return hosting(map({{"torrents", "l" + first + second + "i5e" + fourth + 'e'}}));
}
std::shared_ptr<const core::SavedShares> sharesSample() {
  const auto first = map({{"type", "i1e"}, {"file", bytes("<b>not-rich</b>/Alpha")},
      {"attributes", map({{"Category", bytes("<img src=x>")}})},
      {"props", map({{"persistent", "4:true"}, {"tags", "3:7,8"}})},
      {"ihash", bytes("FINGERPRINT_MUST_NOT_RENDER")}, {"ifile", bytes("CACHE_MUST_NOT_RENDER")},
      {"per_key", bytes("PERSONAL_MUST_NOT_RENDER")}});
  const auto second = map({{"type", "i2e"}, {"file", "4:Beta"}, {"ihash", "2:fp"}, {"ifile", "5:cache"}});
  const auto third = map({{"type", "i3e"}, {"file", "5:Gamma"}, {"recursive", "i1e"},
      {"attributes", map({{"Category", "0:"}})}, {"props", map({{"tags", "2:-2"}})}});
  const auto fifth = map({{"type", "i99e"}, {"file", "7:Unknown"}});
  const auto sixth = map({{"type", "i1e"}, {"file", bytes(std::string("\xff\0", 2))},
      {"ihash", "2:fp"}, {"ifile", "5:cache"}, {"props", map({{"persistent", "i1e"}})}});
  return shares(map({{"resources", "l" + first + second + third + "i5e" + fifth + sixth + 'e'}}));
}
QSortFilterProxyModel* proxy(ui::SavedServicePage& page) { return qobject_cast<QSortFilterProxyModel*>(page.table().model()); }
void verifyNoSensitiveRoles(const QAbstractItemModel& model) {
  const QStringList secrets{QStringLiteral("HOSTING_PATH_MUST_NOT_RENDER"), QStringLiteral("FINGERPRINT_MUST_NOT_RENDER"),
      QStringLiteral("CACHE_MUST_NOT_RENDER"), QStringLiteral("PERSONAL_MUST_NOT_RENDER")};
  for (int row = 0; row < model.rowCount(); ++row) {
    for (int column = 0; column < model.columnCount(); ++column) {
      const auto index = model.index(row, column);
      QVERIFY(!model.data(index, Qt::ToolTipRole).isValid());
      for (int role = Qt::DisplayRole; role <= Qt::UserRole + 4; ++role) {
        const auto value = model.data(index, role).toString();
        for (const auto& secret : secrets) {
          QVERIFY(!value.contains(secret));
          QVERIFY(!value.contains(QString::fromLatin1(secret.toUtf8().toHex().toUpper())));
        }
      }
    }
  }
}
template<class Model>
void invalidModelsPreservePrevious(Model& model, auto snapshot) {
  using Snapshot = std::remove_const_t<typename decltype(snapshot)::element_type>;
  QVERIFY(snapshot);
  QVERIFY(model.setSnapshot(snapshot));
  const auto rows = model.rowCount();
  const auto first = model.data(model.index(0, 0), Qt::DisplayRole);
  QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
  const auto reject = [&](auto invalid) {
    QVERIFY(!model.setSnapshot(std::move(invalid)));
    QCOMPARE(model.rowCount(), rows);
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole), first);
    QCOMPARE(resets.count(), 0);
  };
  reject(std::shared_ptr<const Snapshot>{});
  auto invalid = std::make_shared<Snapshot>(*snapshot);
  invalid->records[1].sourceIndex = invalid->records[0].sourceIndex;
  reject(invalid);
  invalid = std::make_shared<Snapshot>(*snapshot);
  invalid->listPresent = false;
  reject(invalid);
  invalid = std::make_shared<Snapshot>(*snapshot);
  invalid->records.resize(core::SavedServiceLimits{}.rows + 1);
  reject(invalid);
  invalid = std::make_shared<Snapshot>(*snapshot);
  invalid->issues.resize(core::SavedServiceLimits{}.issues + 1);
  reject(invalid);
  invalid = std::make_shared<Snapshot>(*snapshot);
  if constexpr (std::is_same_v<Snapshot, core::SavedHosting>) { invalid->records[0].hash = std::string(core::SavedServiceLimits{}.fieldBytes + 1, 'H'); }
  else { invalid->records[0].file->bytes.assign(core::SavedServiceLimits{}.fieldBytes + 1, 'P'); }
  reject(invalid);
  invalid = std::make_shared<Snapshot>();
  invalid->listPresent = true;
  invalid->records.resize(17);
  for (std::size_t index = 0; index < 17; ++index) {
    invalid->records[index].sourceIndex = index;
    if constexpr (std::is_same_v<Snapshot, core::SavedHosting>) { invalid->records[index].hash = std::string(core::SavedServiceLimits{}.fieldBytes, 'H'); }
    else { invalid->records[index].file = core::SavedText{std::string(core::SavedServiceLimits{}.fieldBytes, 'P')}; }
  }
  reject(invalid);
}
} // namespace

class SavedServicePagesTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void hostingFieldsNumericSortAndSourceIndices() {
    ui::SavedHostingPage page;
    QAbstractItemModelTester tester(&page.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto snapshot = hostingSample();
    QVERIFY(snapshot);
    QVERIFY(page.beginRequest(1));
    QVERIFY(page.setReady(1, snapshot, ui::SavedPageSource::Backup));
    QCOMPARE(page.model().rowCount(), 4);
    QCOMPARE(page.model().columnCount(), 10);
    QCOMPARE(page.model().data(page.model().index(0, 2)).toString(), QStringLiteral("Failed"));
    QCOMPARE(page.model().data(page.model().index(0, 2), Qt::UserRole).toInt(), 0);
    QCOMPARE(page.model().data(page.model().index(1, 1)).toString(), QStringLiteral("Statistics backup"));
    QCOMPARE(page.model().data(page.model().index(0, 1)).toString(), QStringLiteral("Persistent host"));
    QVERIFY(!page.model().data(page.model().index(1, 2), Qt::UserRole).isValid());
    QCOMPARE(page.model().data(page.model().index(3, 2)).toString(), QStringLiteral("Unknown (7)"));
    QCOMPARE(page.model().data(page.model().index(3, 3), Qt::UserRole).metaType().id(), QMetaType::Bool);
    QCOMPARE(page.model().data(page.model().index(0, 4), Qt::UserRole).toLongLong(), qint64{0});
    QVERIFY(!page.model().data(page.model().index(1, 4), Qt::UserRole).isValid());
    QCOMPARE(page.model().data(page.model().index(1, 9), Qt::UserRole).toLongLong(), qint64{123});
    QCOMPARE(page.model().data(page.model().index(0, 7), Qt::UserRole).metaType().id(), QMetaType::LongLong);
    QVERIFY(!page.model().data(page.model().index(2, 0), Qt::ToolTipRole).isValid());
    auto* sorted = proxy(page);
    QVERIFY(sorted);
    QCOMPARE(sorted->sortRole(), Qt::UserRole);
    QAbstractItemModelTester proxyTester(sorted, QAbstractItemModelTester::FailureReportingMode::QtTest);
    page.table().sortByColumn(5, Qt::AscendingOrder);
    QList<qlonglong> completed;
    for (int row = 0; row < sorted->rowCount(); ++row) {
      const auto value = sorted->data(sorted->index(row, 5), Qt::UserRole);
      if (value.isValid()) { completed.push_back(value.toLongLong()); }
      const auto source = sorted->mapToSource(sorted->index(row, 0));
      QVERIFY(page.model().sourceIndexAt(source.row()).has_value());
      QCOMPARE(sorted->data(sorted->index(row, 0), ui::SavedHostingModel::SourceIndexRole).toULongLong(), static_cast<qulonglong>(*page.model().sourceIndexAt(source.row())));
    }
    QCOMPARE(completed, (QList<qlonglong>{9, 100, 1000}));
    page.table().sortByColumn(2, Qt::AscendingOrder);
    QList<int> statuses;
    for (int row = 0; row < sorted->rowCount(); ++row) {
      const auto value = sorted->data(sorted->index(row, 2), Qt::UserRole);
      if (value.isValid()) { statuses.push_back(value.toInt()); }
    }
    QCOMPARE(statuses, (QList<int>{0, 7}));
    page.table().sortByColumn(3, Qt::AscendingOrder);
    QList<bool> passive;
    for (int row = 0; row < sorted->rowCount(); ++row) {
      const auto value = sorted->data(sorted->index(row, 3), Qt::UserRole);
      if (value.isValid()) { passive.push_back(value.toBool()); }
    }
    QCOMPARE(passive, (QList<bool>{false, false, true}));
    for (int row = 0; row < page.model().rowCount(); ++row) {
      for (int column = 0; column < page.model().columnCount(); ++column) {
        QCOMPARE(page.model().data(page.model().index(row, column), ui::SavedHostingModel::SourceIndexRole).toULongLong(), static_cast<qulonglong>(row));
      }
    }
    verifyNoSensitiveRoles(page.model());
    QVERIFY(page.provenanceText().contains(QStringLiteral("Backup (.bak)")));
    QVERIFY(page.statusText().contains(QStringLiteral("diagnostics")));
  }

  void sharingFieldsUnknownsAndPrivateData() {
    ui::SavedSharesPage page;
    QAbstractItemModelTester tester(&page.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto snapshot = sharesSample();
    QVERIFY(snapshot);
    QVERIFY(page.beginRequest(1));
    QVERIFY(page.setReady(1, snapshot, ui::SavedPageSource::Saving));
    QCOMPARE(page.model().rowCount(), 6);
    QCOMPARE(page.model().columnCount(), 6);
    QCOMPARE(page.model().data(page.model().index(0, 0)).toString(), QStringLiteral("<b>not-rich</b>/Alpha"));
    QCOMPARE(page.model().data(page.model().index(0, 2)).toString(), QStringLiteral("<img src=x>"));
    QCOMPARE(page.model().data(page.model().index(0, 5)).toString(), QStringLiteral("7,8"));
    QCOMPARE(page.model().data(page.model().index(0, 1)).toString(), QStringLiteral("File"));
    QCOMPARE(page.model().data(page.model().index(1, 1)).toString(), QStringLiteral("Directory"));
    QCOMPARE(page.model().data(page.model().index(2, 1)).toString(), QStringLiteral("Directory contents"));
    QCOMPARE(page.model().data(page.model().index(4, 1)).toString(), QStringLiteral("Unknown (99)"));
    QCOMPARE(page.model().data(page.model().index(2, 2)).toString(), QStringLiteral("(empty)"));
    QCOMPARE(page.model().data(page.model().index(1, 2)).toString(), QString::fromUtf8("—"));
    QCOMPARE(page.model().data(page.model().index(5, 0)).toString(), QString::fromUtf8("—"));
    QVERIFY(!page.model().data(page.model().index(5, 3), Qt::UserRole).isValid());
    QCOMPARE(page.model().data(page.model().index(0, 3), Qt::UserRole).metaType().id(), QMetaType::Bool);
    QCOMPARE(page.model().data(page.model().index(2, 4), Qt::UserRole).toBool(), true);
    QVERIFY(!page.model().data(page.model().index(0, 4), Qt::UserRole).isValid());
    auto* sorted = proxy(page);
    QVERIFY(sorted);
    page.table().sortByColumn(1, Qt::AscendingOrder);
    QList<int> types;
    for (int row = 0; row < sorted->rowCount(); ++row) {
      const auto value = sorted->data(sorted->index(row, 1), Qt::UserRole);
      if (value.isValid()) { types.push_back(value.toInt()); }
      const auto source = sorted->mapToSource(sorted->index(row, 0));
      const auto identity = page.model().sourceIndexAt(source.row());
      QVERIFY(identity.has_value());
      QCOMPARE(sorted->data(sorted->index(row, 0), ui::SavedSharesModel::SourceIndexRole).toULongLong(), static_cast<qulonglong>(*identity));
    }
    QCOMPARE(types, (QList<int>{1, 1, 2, 3, 99}));
    page.table().sortByColumn(3, Qt::AscendingOrder);
    QList<bool> persistent;
    for (int row = 0; row < sorted->rowCount(); ++row) {
      const auto value = sorted->data(sorted->index(row, 3), Qt::UserRole);
      if (value.isValid()) { persistent.push_back(value.toBool()); }
    }
    QCOMPARE(persistent, (QList<bool>{false, false, false, true}));
    QCOMPARE(page.table().editTriggers(), QAbstractItemView::NoEditTriggers);
    QCOMPARE(page.table().dragDropMode(), QAbstractItemView::NoDragDrop);
    verifyNoSensitiveRoles(page.model());
    QVERIFY(page.provenanceText().contains(QStringLiteral("Saving (.saving)")));
  }

  void distinctPageStates_data() {
    QTest::addColumn<bool>("sharing");
    QTest::newRow("hosting") << false;
    QTest::newRow("shares") << true;
  }
  void distinctPageStates() {
    QFETCH(bool, sharing);
    const auto exercise = [&](auto& page, auto empty, auto absent) {
      QVERIFY(empty);
      QVERIFY(absent);
      QCOMPARE(page.state(), ui::SavedPageState::NotLoaded);
      QCOMPARE(page.generation(), quint64{0});
      QVERIFY(!page.beginRequest(0));
      QVERIFY(page.beginRequest(1));
      QCOMPARE(page.state(), ui::SavedPageState::Loading);
      QVERIFY(page.setMissing(1));
      QCOMPARE(page.state(), ui::SavedPageState::Missing);
      const auto missingText = page.statusText();
      QVERIFY(page.beginRequest(2));
      QVERIFY(page.setReady(2, empty, ui::SavedPageSource::Primary));
      QCOMPARE(page.state(), ui::SavedPageState::Ready);
      QCOMPARE(page.model().rowCount(), 0);
      QVERIFY(page.statusText().contains(QStringLiteral("0 saved records")));
      QVERIFY(page.statusText() != missingText);
      QVERIFY(page.provenanceText().contains(QStringLiteral("Primary")));
      QVERIFY(page.beginRequest(3));
      QVERIFY(page.setReady(3, absent, ui::SavedPageSource::Backup));
      QCOMPARE(page.state(), ui::SavedPageState::Ready);
      QVERIFY(page.statusText().contains(QStringLiteral("absent")));
      QVERIFY(page.statusText() != missingText);
      QVERIFY(page.beginRequest(4));
      const auto message = QStringLiteral("<b>Untrusted failure</b><a href=file:///not-opened>text</a>");
      QVERIFY(page.setFailed(4, message));
      QCOMPARE(page.state(), ui::SavedPageState::Failed);
      QCOMPARE(page.statusText(), message);
      for (auto* label : page.template findChildren<QLabel*>()) {
        QCOMPARE(label->textFormat(), Qt::PlainText);
        QVERIFY(!label->openExternalLinks());
      }
      QVERIFY(page.beginRequest(5));
      QVERIFY(page.setFailed(5, QString(5000, QChar('x'))));
      QCOMPARE(page.statusText().size(), qsizetype{4096});
      QVERIFY(page.beginRequest(6));
      QVERIFY(page.setFailed(6, {}));
      QVERIFY(!page.statusText().isEmpty());
    };
    if (sharing) { ui::SavedSharesPage page; exercise(page, shares("d9:resourceslee"), shares("de")); }
    else { ui::SavedHostingPage page; exercise(page, hosting("d8:torrentslee"), hosting("de")); }
  }

  void staleNewerAndDuplicateCompletions_data() { distinctPageStates_data(); }
  void staleNewerAndDuplicateCompletions() {
    QFETCH(bool, sharing);
    const auto exercise = [&](auto& page, auto snapshot) {
      QVERIFY(snapshot);
      QVERIFY(page.beginRequest(1));
      QVERIFY(page.setReady(1, snapshot, ui::SavedPageSource::Primary));
      QVERIFY(page.beginRequest(2));
      QCOMPARE(page.model().rowCount(), 0);
      QVERIFY(!page.setReady(1, snapshot, ui::SavedPageSource::Saving));
      QCOMPARE(page.state(), ui::SavedPageState::Loading);
      QVERIFY(page.beginRequest(3));
      QVERIFY(!page.setFailed(2, QStringLiteral("old failure")));
      QVERIFY(!page.setMissing(2));
      QVERIFY(page.setReady(3, snapshot, ui::SavedPageSource::Backup));
      const auto status = page.statusText();
      const auto source = page.provenanceText();
      const auto count = page.model().rowCount();
      QSignalSpy resets(&page.model(), &QAbstractItemModel::modelReset);
      QVERIFY(!page.beginRequest(2));
      QVERIFY(!page.beginRequest(3));
      QVERIFY(!page.setReady(3, snapshot, ui::SavedPageSource::Saving));
      QVERIFY(!page.setReady(1, {}, static_cast<ui::SavedPageSource>(99)));
      QVERIFY(!page.setMissing(3));
      QVERIFY(!page.setFailed(3, QStringLiteral("duplicate failure")));
      QCOMPARE(page.state(), ui::SavedPageState::Ready);
      QCOMPARE(page.statusText(), status);
      QCOMPARE(page.provenanceText(), source);
      QCOMPARE(page.model().rowCount(), count);
      QCOMPARE(resets.count(), 0);
    };
    if (sharing) { ui::SavedSharesPage page; exercise(page, sharesSample()); }
    else { ui::SavedHostingPage page; exercise(page, hostingSample()); }
  }

  void modelRejectsInvalidSnapshotsAtomically_data() { distinctPageStates_data(); }
  void modelRejectsInvalidSnapshotsAtomically() {
    QFETCH(bool, sharing);
    if (sharing) {
      ui::SavedSharesModel model;
      QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
      invalidModelsPreservePrevious(model, sharesSample());
    } else {
      ui::SavedHostingModel model;
      QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
      invalidModelsPreservePrevious(model, hostingSample());
    }
  }

  void currentInvalidInputFailsInsteadOfShowingEmpty_data() { distinctPageStates_data(); }
  void currentInvalidInputFailsInsteadOfShowingEmpty() {
    QFETCH(bool, sharing);
    const auto exercise = [&](auto& page, auto snapshot) {
      using Snapshot = std::remove_const_t<typename decltype(snapshot)::element_type>;
      QVERIFY(snapshot);
      QVERIFY(page.beginRequest(1));
      QVERIFY(!page.setReady(1, {}, ui::SavedPageSource::Primary));
      QCOMPARE(page.state(), ui::SavedPageState::Failed);
      QCOMPARE(page.model().rowCount(), 0);
      QVERIFY(page.beginRequest(2));
      QVERIFY(!page.setReady(2, snapshot, static_cast<ui::SavedPageSource>(99)));
      QCOMPARE(page.state(), ui::SavedPageState::Failed);
      auto over = std::make_shared<Snapshot>(*snapshot);
      over->records.resize(core::SavedServiceLimits{}.rows + 1);
      QVERIFY(page.beginRequest(3));
      QVERIFY(!page.setReady(3, over, ui::SavedPageSource::Primary));
      QCOMPARE(page.state(), ui::SavedPageState::Failed);
      QVERIFY(!page.setReady(3, snapshot, ui::SavedPageSource::Primary));
      auto duplicate = std::make_shared<Snapshot>(*snapshot);
      duplicate->records[1].sourceIndex = duplicate->records[0].sourceIndex;
      QVERIFY(page.beginRequest(4));
      QVERIFY(!page.setReady(4, duplicate, ui::SavedPageSource::Primary));
      QCOMPARE(page.state(), ui::SavedPageState::Failed);
      QVERIFY(page.beginRequest(5));
      QVERIFY(page.setReady(5, snapshot, ui::SavedPageSource::Primary));
      QCOMPARE(page.state(), ui::SavedPageState::Ready);
    };
    if (sharing) { ui::SavedSharesPage page; exercise(page, sharesSample()); }
    else { ui::SavedHostingPage page; exercise(page, hostingSample()); }
  }

  void snapshotsRemainOwnedUntilCleared_data() { distinctPageStates_data(); }
  void snapshotsRemainOwnedUntilCleared() {
    QFETCH(bool, sharing);
    const auto exercise = [&](auto& page, auto snapshot) {
      QVERIFY(snapshot);
      std::weak_ptr weak = snapshot;
      QVERIFY(page.beginRequest(1));
      QVERIFY(page.setReady(1, snapshot, ui::SavedPageSource::Primary));
      snapshot.reset();
      QVERIFY(!weak.expired());
      QVERIFY(page.model().rowCount() > 0);
      QVERIFY(page.beginRequest(2));
      QVERIFY(weak.expired());
      QCOMPARE(page.model().rowCount(), 0);
    };
    if (sharing) { ui::SavedSharesPage page; exercise(page, sharesSample()); }
    else { ui::SavedHostingPage page; exercise(page, hostingSample()); }
  }

  void localQueuedDeliveryAfterPageDeletion_data() { distinctPageStates_data(); }
  void localQueuedDeliveryAfterPageDeletion() {
    QFETCH(bool, sharing);
    const auto exercise = [&](auto* rawPage, auto snapshot) {
      std::unique_ptr<std::remove_pointer_t<decltype(rawPage)>> owner(rawPage);
      QPointer guard(rawPage);
      QVERIFY(snapshot);
      std::weak_ptr weak = snapshot;
      QVERIFY(rawPage->beginRequest(1));
      auto delivered = std::make_shared<bool>(false);
      // This proves local Qt/QPointer delivery lifetime only. No service worker
      // or profile read is simulated or credited by this queued callback.
      QTimer::singleShot(0, qApp, [guard, snapshot = std::move(snapshot), delivered] {
        if (guard) { guard->setReady(1, snapshot, ui::SavedPageSource::Primary); }
        *delivered = true;
      });
      owner.reset();
      QVERIFY(guard.isNull());
      QVERIFY(!weak.expired());
      QTRY_VERIFY(*delivered);
      QTRY_VERIFY(weak.expired());
    };
    if (sharing) { exercise(new ui::SavedSharesPage, sharesSample()); }
    else { exercise(new ui::SavedHostingPage, hostingSample()); }
  }

  void identityUsesSuppliedSourceIndex_data() { distinctPageStates_data(); }
  void identityUsesSuppliedSourceIndex() {
    QFETCH(bool, sharing);
    const auto exercise = [&](auto& model, auto snapshot) {
      snapshot->listPresent = true;
      snapshot->records.resize(2);
      snapshot->records[0].sourceIndex = 99;
      snapshot->records[1].sourceIndex = 400;
      QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
      QVERIFY(model.setSnapshot(snapshot));
      QVERIFY(model.sourceIndexAt(0) == std::size_t{99});
      QVERIFY(model.sourceIndexAt(1) == std::size_t{400});
      QVERIFY(!model.sourceIndexAt(-1));
      QVERIFY(!model.sourceIndexAt(2));
      for (int column = 0; column < model.columnCount(); ++column) {
        QCOMPARE(model.data(model.index(0, column), Qt::UserRole + 1).toULongLong(), qulonglong{99});
        QCOMPARE(model.data(model.index(1, column), Qt::UserRole + 1).toULongLong(), qulonglong{400});
      }
    };
    if (sharing) { ui::SavedSharesModel model; exercise(model, std::make_shared<core::SavedShares>()); }
    else { ui::SavedHostingModel model; exercise(model, std::make_shared<core::SavedHosting>()); }
  }

  void maximumRequestGenerationDoesNotWrap_data() { distinctPageStates_data(); }
  void maximumRequestGenerationDoesNotWrap() {
    QFETCH(bool, sharing);
    const auto exercise = [&](auto& page) {
      constexpr auto maximum = std::numeric_limits<quint64>::max();
      QVERIFY(page.beginRequest(maximum));
      QVERIFY(!page.beginRequest(0));
      QVERIFY(!page.beginRequest(1));
      QVERIFY(!page.beginRequest(maximum));
      QVERIFY(!page.setMissing(maximum - 1));
      QCOMPARE(page.state(), ui::SavedPageState::Loading);
      QCOMPARE(page.generation(), maximum);
      QVERIFY(page.setMissing(maximum));
      QCOMPARE(page.state(), ui::SavedPageState::Missing);
      QVERIFY(!page.beginRequest(1));
      QCOMPARE(page.generation(), maximum);
    };
    if (sharing) { ui::SavedSharesPage page; exercise(page); }
    else { ui::SavedHostingPage page; exercise(page); }
  }
};

QTEST_MAIN(SavedServicePagesTests)
#include "saved_service_pages_tests.moc"
