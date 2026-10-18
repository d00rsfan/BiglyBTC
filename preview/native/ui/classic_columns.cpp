#include "classic_columns.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QSignalBlocker>
#include <QTableView>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string_view>

namespace biglybtc::ui {
namespace {
struct Correspondence { std::string_view id; int logical; };
constexpr std::array<Correspondence, 6> Whitelist{{
    {"name", 0}, {"status", 1}, {"downspeed", 4},
    {"upspeed", 5}, {"up", 7}, {"torrenthash", 8}}};
std::optional<std::size_t> matching(const core::ColumnLayoutText& text) {
  if (!text.validUtf8 || text.embeddedNul) { return std::nullopt; }
  for (std::size_t i = 0; i < Whitelist.size(); ++i) {
    if (text.bytes == Whitelist[i].id) { return i; }
  }
  return std::nullopt;
}
std::optional<qint32> numeric(const core::ColumnLayoutInteger& field) {
  if (field.presence != core::ColumnLayoutPresence::Present || !field.value) { return std::nullopt; }
  const auto low = static_cast<quint32>(field.value->raw);
  const auto value = low <= static_cast<quint32>(std::numeric_limits<qint32>::max())
      ? static_cast<qint32>(low)
      : -1 - static_cast<qint32>(std::numeric_limits<quint32>::max() - low);
  return value == field.value->value ? std::optional<qint32>{value} : std::nullopt;
}
struct PlannedColumn { std::optional<bool> visible; std::optional<int> width; };
struct Plan {
  std::array<PlannedColumn, 6> columns{};
  std::vector<int> order;
  std::optional<int> sort;
  Qt::SortOrder direction = Qt::AscendingOrder;
  bool applicable = false;
};
std::optional<Plan> plan(const core::SavedTableLayout& saved, const QTableView& table) {
  using Presence = core::ColumnLayoutPresence;
  const bool exact = saved.origin == core::ColumnLayoutOrigin::ExactTable && saved.exactPresence == Presence::Present;
  const bool defaults = saved.origin == core::ColumnLayoutOrigin::UserDefault && saved.exactPresence == Presence::Missing;
  if (saved.presence != Presence::Present || (!exact && !defaults)
      || saved.columns.size() > 1024 || saved.sort.size() > 32) { return std::nullopt; }
  const auto* header = table.horizontalHeader();
  const int count = header->count();
  if (count < 0 || count > 1024) { return std::nullopt; }
  Plan result;
  std::array<const core::SavedColumnLayout*, 6> records{};
  std::array<bool, 6> duplicate{};
  for (const auto& column : saved.columns) {
    const auto match = matching(column.id);
    if (!match) { continue; }
    if (records[*match]) { duplicate[*match] = true; }
    else { records[*match] = &column; }
  }
  struct Positioned { qint32 position; int logical; };
  std::vector<Positioned> positioned;
  positioned.reserve(Whitelist.size());
  for (std::size_t i = 0; i < Whitelist.size(); ++i) {
    const auto* record = records[i];
    const int logical = Whitelist[i].logical;
    if (duplicate[i] || !record || logical >= count || record->presence != Presence::Present) { continue; }
    if (record->listLength >= 1) {
      const auto raw = numeric(record->visibility);
      if (raw && record->visible && *record->visible == (*raw == 1)) {
        result.columns[i].visible = record->visible;
        result.applicable = true;
      }
    }
    if (record->listLength >= 3 && logical != 0) {
      const auto width = numeric(record->width);
      if (width && *width >= 16 && *width <= 8192) {
        result.columns[i].width = *width;
        result.applicable = true;
      }
    }
    if (record->listLength >= 2) {
      const auto position = numeric(record->position);
      if (position && *position >= 0) { positioned.push_back({*position, logical}); }
    }
  }
  std::sort(positioned.begin(), positioned.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
  const bool unique = std::adjacent_find(positioned.begin(), positioned.end(),
      [](const auto& a, const auto& b) { return a.position == b.position; }) == positioned.end();
  if (unique && !positioned.empty()) {
    result.order.reserve(static_cast<std::size_t>(count));
    for (const auto& item : positioned) { result.order.push_back(item.logical); }
    for (int visual = 0; visual < count; ++visual) {
      const int logical = header->logicalIndex(visual);
      if (std::find(result.order.begin(), result.order.end(), logical) == result.order.end()) { result.order.push_back(logical); }
    }
    result.applicable = true;
  }
  if (saved.sortSource == core::ColumnLayoutSortSource::SortColumns
      && saved.sortColumnsPresence == Presence::Present && saved.sort.size() == 1) {
    const auto& entry = saved.sort.front();
    if (entry.sourceIndex == 0 && entry.id.presence == Presence::Present && entry.id.value) {
      const auto match = matching(*entry.id.value);
      if (match && !duplicate[*match] && records[*match] && Whitelist[*match].logical < count) {
        const auto& record = *records[*match];
        const auto direction = numeric(record.sortOrder);
        if (record.presence == Presence::Present && record.listLength >= 5 && direction && *direction >= 0
            && record.direction && *record.direction == (*direction == 1 ? core::ColumnLayoutDirection::Ascending
                                                                         : core::ColumnLayoutDirection::Descending)) {
          result.sort = Whitelist[*match].logical;
          result.direction = *direction == 1 ? Qt::AscendingOrder : Qt::DescendingOrder;
          result.applicable = true;
        }
      }
    }
  }
  return result;
}
} // namespace

struct ClassicColumns::InternalChange {
  QPointer<ClassicColumns> owner;
  bool previous;
  explicit InternalChange(ClassicColumns& value) : owner(&value), previous(value.internalChange_) { value.internalChange_ = true; }
  ~InternalChange() { if (owner) { owner->internalChange_ = previous; } }
};

// Original reference: BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
// TableColumnCreator/TableViewSWT_Common. Only preview columns are offered.
ClassicColumns::ClassicColumns(QTableView& table, bool completed)
    : QObject(&table), table_(&table),
      menu_(new QMenu(completed ? tr("Complete Columns") : tr("Incomplete Columns"), &table)) {
  const auto prefix = completed ? QStringLiteral("classic-columns-complete") : QStringLiteral("classic-columns-incomplete");
  setObjectName(prefix + QStringLiteral("-controller"));
  menu_->setObjectName(prefix + QStringLiteral("-menu"));
  auto* header = table.horizontalHeader();
  const int count = header->count();
  columns_.reserve(static_cast<std::size_t>(count));
  order_.reserve(static_cast<std::size_t>(count));
  actions_.reserve(static_cast<std::size_t>(count));
  for (int visual = 0; visual < count; ++visual) { order_.push_back(header->logicalIndex(visual)); }
  for (int logical = 0; logical < count; ++logical) {
    bool hidden = table.isColumnHidden(logical);
    if (hidden) { header->showSection(logical); }
    const int width = header->sectionSize(logical);
    if (hidden) { header->hideSection(logical); }
    if (logical == 4) { hidden = completed; }
    if (logical == 5) { hidden = false; }
    if (logical == 8) { hidden = true; }
    columns_.push_back({logical, width, hidden});
    auto label = table.model()->headerData(logical, Qt::Horizontal, Qt::DisplayRole).toString();
    if (label.isEmpty()) { label = tr("Column %1").arg(logical + 1); }
    auto* action = menu_->addAction(label.replace(QStringLiteral("&"), QStringLiteral("&&")));
    action->setObjectName(prefix + QStringLiteral("-column-%1").arg(logical));
    action->setCheckable(true);
    actions_.push_back(action);
    connect(action, &QAction::toggled, this, [this, logical](bool visible) {
      if (!table_ || logical >= table_->horizontalHeader()->count()) { return; }
      markEdit(true);
      QPointer<ClassicColumns> guard(this);
      InternalChange change(*this);
      table_->setColumnHidden(logical, !visible);
      if (guard) { refreshActions(); }
    });
  }
  menu_->addSeparator();
  auto* restore = menu_->addAction(tr("Reset Columns"));
  restore->setObjectName(prefix + QStringLiteral("-reset"));
  connect(restore, &QAction::triggered, this, &ClassicColumns::reset);
  connect(menu_, &QMenu::aboutToShow, this, &ClassicColumns::refreshActions);
  header->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(header, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
    if (table_ && menu_) { menu_->popup(table_->horizontalHeader()->viewport()->mapToGlobal(point)); }
  });
  restoreBaseline(false);
  connect(header, &QHeaderView::sectionMoved, this, [this](int, int, int) { markEdit(); });
  connect(header, &QHeaderView::sectionResized, this, [this, header](int logical, int before, int after) {
    if (logical == 0 && before > 0 && after > 0
        && header->sectionResizeMode(0) == QHeaderView::Stretch) { return; }
    markEdit();
  });
  connect(header, &QHeaderView::sortIndicatorChanged, this, [this](int, Qt::SortOrder) { markEdit(); });
}
ClassicColumns::~ClassicColumns() { delete menu_.data(); }
QMenu* ClassicColumns::menu() const { return menu_.data(); }
std::optional<quint64> ClassicColumns::editRevision() const noexcept {
  return editRevisionExhausted_ ? std::nullopt : std::optional<quint64>{editRevision_};
}
void ClassicColumns::markEdit(bool explicitIntent) {
  if ((internalChange_ && !explicitIntent) || editRevisionExhausted_) { return; }
  if (editRevision_ == std::numeric_limits<quint64>::max()) { editRevisionExhausted_ = true; }
  else { ++editRevision_; }
}
void ClassicColumns::refreshActions() {
  if (!table_ || !menu_) { return; }
  const int count = table_->horizontalHeader()->count();
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    auto* action = actions_[i].data();
    if (!action) { continue; }
    const QSignalBlocker guard(action);
    const bool available = columns_[i].logical < count;
    action->setEnabled(available);
    action->setChecked(available && !table_->isColumnHidden(columns_[i].logical));
  }
}
void ClassicColumns::reset() { restoreBaseline(true); }
void ClassicColumns::restoreBaseline(bool localIntent) {
  if (!table_) { return; }
  if (localIntent) { markEdit(true); } // Includes a no-op Reset during import.
  const auto revision = editRevision();
  QPointer<ClassicColumns> guard(this);
  QPointer<QTableView> table = table_;
  QPointer<QHeaderView> header = table->horizontalHeader();
  const int count = header->count();
  const auto current = [&] {
    return guard && table && header && table->horizontalHeader() == header.data()
        && header->count() == count && guard->editRevision() == revision;
  };
  InternalChange change(*this);
  for (std::size_t visual = 0; visual < order_.size(); ++visual) {
    const int currentVisual = header->visualIndex(order_[visual]);
    if (currentVisual >= 0 && static_cast<int>(visual) < header->count()) {
      header->moveSection(currentVisual, static_cast<int>(visual));
      if (!current()) { return; }
    }
  }
  for (const auto& column : columns_) {
    if (column.logical >= header->count()) { continue; }
    table->setColumnHidden(column.logical, column.hidden);
    if (!current()) { return; }
    header->resizeSection(column.logical, column.width);
    if (!current()) { return; }
  }
  refreshActions();
}
bool ClassicColumns::applyLayout(const core::SavedTableLayout& saved, quint64 expectedRevision,
    const std::function<bool()>& stillCurrent) {
  if (!table_ || editRevision() != expectedRevision) { return false; }
  std::optional<Plan> admitted;
  try { admitted = plan(saved, *table_); }
  catch (const std::bad_alloc&) { return false; }
  if (!admitted || !admitted->applicable || editRevision() != expectedRevision) { return false; }
  const auto revision = editRevision();
  QPointer<ClassicColumns> guard(this);
  QPointer<QTableView> table = table_;
  QPointer<QHeaderView> header = table->horizontalHeader();
  const int count = header->count();
  const auto valid = [&] {
    return guard && table && header && table->horizontalHeader() == header.data()
        && header->count() == count && guard->editRevision() == revision;
  };
  const auto current = [&] {
    if (!valid() || (stillCurrent && !stillCurrent())) { return false; }
    // The borrowed public predicate may synchronously reset or delete its owner.
    return valid();
  };
  if (!current()) { return false; }
  InternalChange change(*this);
  for (std::size_t visual = 0; visual < admitted->order.size(); ++visual) {
    header->moveSection(header->visualIndex(admitted->order[visual]), static_cast<int>(visual));
    if (!current()) { return false; }
  }
  for (std::size_t i = 0; i < Whitelist.size(); ++i) {
    const int logical = Whitelist[i].logical;
    if (admitted->columns[i].visible) {
      table->setColumnHidden(logical, !*admitted->columns[i].visible);
      if (!current()) { return false; }
    }
    if (admitted->columns[i].width) {
      header->resizeSection(logical, *admitted->columns[i].width);
      if (!current()) { return false; }
    }
  }
  if (admitted->sort) {
    table->sortByColumn(*admitted->sort, admitted->direction);
    if (!current()) { return false; }
  }
  refreshActions();
  return true;
}


std::optional<core::ClassicPanePreferences> ClassicColumns::capturePreferences() const {
  QPointer<const ClassicColumns> guard(this);
  QPointer<QTableView> table = table_;
  if (!table || !editRevision()) { return std::nullopt; }
  QPointer<QHeaderView> header = table->horizontalHeader();
  QPointer<QAbstractItemModel> model = table->model();
  const auto revision = editRevision();
  const auto count = static_cast<int>(core::ClassicPreferenceIds.size());
  const auto current = [&] {
    return guard && table && header && model && table->model() == model.data()
        && table->horizontalHeader() == header.data() && header->count() == count
        && guard->editRevision() == revision;
  };
  if (!current()) { return std::nullopt; }
  try {
    // sectionSize() is zero while hidden, and sectionSizeHint() does not retain
    // manual widths. Qt's public state roundtrip preserves its hidden-size map.
    // Only this detached header is unhidden; the live header is never changed.
    const auto state = header->saveState();
    if (!current()) { return std::nullopt; }
    QHeaderView copied(Qt::Horizontal);
    copied.setModel(model.data());
    if (!current() || !copied.restoreState(state) || copied.count() != count) { return std::nullopt; }
    copied.setSectionResizeMode(QHeaderView::Interactive);
    core::ClassicPanePreferences result;
    for (int logical = 0; logical < count; ++logical) {
      const auto index = static_cast<std::size_t>(logical);
      result.columns[index].visible = !header->isSectionHidden(logical);
      if (logical != 0) {
        if (copied.isSectionHidden(logical)) { copied.showSection(logical); }
        if (!current()) { return std::nullopt; }
        const auto width = copied.sectionSize(logical);
        if (width < 16 || width > 8192) { return std::nullopt; }
        result.columns[index].width = width;
      }
      const int ordered = header->logicalIndex(logical);
      if (ordered < 0 || ordered >= count) { return std::nullopt; }
      result.order[index] = core::ClassicPreferenceIds[static_cast<std::size_t>(ordered)];
    }
    const int sort = header->sortIndicatorSection();
    if (sort < 0 || sort >= count) { return std::nullopt; }
    result.sortId = core::ClassicPreferenceIds[static_cast<std::size_t>(sort)];
    result.ascending = header->sortIndicatorOrder() == Qt::AscendingOrder;
    if (!current() || !core::validateClassicPreferences({result, result})) { return std::nullopt; }
    return result;
  } catch (const std::bad_alloc&) { return std::nullopt; }
}

bool ClassicColumns::applyPreferences(const core::ClassicPanePreferences& preferences,
    quint64 expectedRevision, const std::function<bool()>& stillCurrent) {
  if (!table_ || editRevision() != expectedRevision) { return false; }
  // Own every requested value before invoking any borrowed callback or Qt signal:
  // a reentrant owner deletion must not invalidate the caller's input reference.
  std::optional<core::ClassicPanePreferences> admitted;
  try {
    if (!core::validateClassicPreferences({preferences, preferences})) { return false; }
    admitted = preferences;
  } catch (const std::bad_alloc&) { return false; }
  QPointer<ClassicColumns> guard(this);
  QPointer<QTableView> table = table_;
  QPointer<QHeaderView> header = table->horizontalHeader();
  const auto revision = editRevision();
  const int count = static_cast<int>(core::ClassicPreferenceIds.size());
  const auto valid = [&] {
    return guard && table && header && table->horizontalHeader() == header.data()
        && header->count() == count && guard->editRevision() == revision
        && revision == expectedRevision;
  };
  const auto current = [&] {
    if (!valid() || (stillCurrent && !stillCurrent())) { return false; }
    return valid(); // The borrowed predicate may reset or delete this controller.
  };
  if (!current()) { return false; }
  InternalChange change(*this);
  header->setSectionResizeMode(0, QHeaderView::Stretch);
  if (!current()) { return false; }
  for (int visual = 0; visual < count; ++visual) {
    const auto& id = admitted->order[static_cast<std::size_t>(visual)];
    const auto found = std::find(core::ClassicPreferenceIds.begin(), core::ClassicPreferenceIds.end(), id);
    const int logical = static_cast<int>(found - core::ClassicPreferenceIds.begin());
    header->moveSection(header->visualIndex(logical), visual);
    if (!current()) { return false; }
  }
  for (int logical = 0; logical < count; ++logical) {
    const auto& column = admitted->columns[static_cast<std::size_t>(logical)];
    table->setColumnHidden(logical, !column.visible);
    if (!current()) { return false; }
    if (column.width) {
      header->resizeSection(logical, *column.width);
      if (!current()) { return false; }
    }
  }
  const auto sort = std::find(core::ClassicPreferenceIds.begin(), core::ClassicPreferenceIds.end(), admitted->sortId);
  table->sortByColumn(static_cast<int>(sort - core::ClassicPreferenceIds.begin()),
      admitted->ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
  if (!current()) { return false; }
  refreshActions();
  return true;
}

} // namespace biglybtc::ui
