// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
constexpr int MaximumTransferKiB = std::numeric_limits<int>::max() / 1024;

int savedTransferLimit(const OptionsValues& values, core::SettingId id) {
  const auto field = std::ranges::find(values.fields, id, &OptionsField::id);
  const auto* number = field != values.fields.end() && field->value
      ? std::get_if<std::int64_t>(&*field->value) : nullptr;
  return number ? static_cast<int>(std::clamp(*number, std::int64_t{0}, std::int64_t{MaximumTransferKiB})) : 0;
}

std::set<int> transferMenuPresets(const TransferLimitMenuValues& values, qint64 anchor) {
  std::set<int> result;
  const auto add = [&](qint64 value) {
    if (value >= (values.classicMenu ? 5 : 1) && value <= MaximumTransferKiB) {
      result.insert(static_cast<int>(value));
    }
  };
  if (values.customPresets) {
    const auto entries = QStringView(values.presets).trimmed().split(u',', Qt::SkipEmptyParts);
    bool valid = !entries.empty();
    for (const auto entry : entries) {
      bool numeric = false;
      const auto value = entry.trimmed().toLongLong(&numeric);
      valid = valid && numeric;
      add(value);
    }
    if (valid) { return result; }
    result.clear();
    if (!values.classicMenu) { return result; }
  }

  // Original SelectableSpeedMenu partitions around the configured or observed rate.
  constexpr std::array steps{5, 10, 35, 50, 50, 50, 100};
  const auto below = anchor <= 1024 ? 1 : anchor / 256;
  const auto above = anchor < 1024 ? 1 : anchor / 256;
  int offset = 0;
  for (int i = 0; i < (values.classicMenu ? 12 : 6); ++i) {
    add(anchor - offset * below);
    add(anchor + offset * above);
    offset += steps[std::min(static_cast<std::size_t>(i), steps.size() - 1)];
  }
  return result;
}

int validatedClassicLimit(core::SettingId id, int value, const OptionsValues& values) {
  using Id = core::SettingId;
  if (id == Id::UploadLimit && value > 0 && value < 5) {
    const auto download = savedTransferLimit(values, Id::DownloadLimit);
    if (download == 0 || download > 2 * value) { return (download + 1) / 2; }
  }
  if (id == Id::DownloadLimit) {
    const auto upload = savedTransferLimit(values, Id::UploadLimit);
    if (upload > 0 && upload < 5 && (value == 0 || value > 2 * upload)) { return 2 * upload; }
  }
  return value;
}
} // namespace

void LibraryWindow::openTransferLimit(bool upload, QPoint position) {
  if (!columnSession_ || closing_ || offlinePreview_ || busy_ || optionsDialog_ || activeOptionsLoad_
      || activeOptionsSave_ || transferLimitRequest_ == std::numeric_limits<quint64>::max()) { return; }
  if (transferLimitMenu_) { transferLimitMenu_->close(); }
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("status-transfer-limit-menu"));
  connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
  menu->addAction(tr("Loading transfer limit…"))->setEnabled(false);
  transferLimitMenu_ = menu;
  menu->popup(position);
  const OptionsRequest request{columnSession_, ++transferLimitRequest_,
      upload ? TransferLimitDirection::Upload : TransferLimitDirection::Download};
  activeOptionsLoad_ = request;
  emit optionsRequested(request);
}

void LibraryWindow::populateTransferLimitMenu(OptionsPresentation packet) {
  auto* menu = transferLimitMenu_.data();
  if (!menu) { return; }
  menu->clear();
  if (!packet->transferLimit || !packet->snapshot || !packet->revision || !packet->failure.isEmpty()) {
    const auto action = menu->addAction(packet->failure.isEmpty() ? tr("Transfer limit unavailable") : packet->failure);
    action->setEnabled(false);
    return;
  }

  using Id = core::SettingId;
  const auto& values = *packet->transferLimit;
  const auto id = values.setting;
  const bool upload = id != Id::DownloadLimit;
  const auto title = id == Id::SeedingUploadLimit ? tr("Seeding upload limit") :
      upload ? tr("Upload limit") : tr("Download limit");
  const auto current = savedTransferLimit(*packet->snapshot, id);
  auto anchor = qint64{current};
  if (!upload && current == 0) {
    const auto liveKiB = latestNativeSession_ && latestNativeSession_->sourceRates
        ? latestNativeSession_->sourceRates->payloadReceived / 1024 : 0;
    anchor = liveKiB >= 1 ? std::clamp<qint64>(liveKiB, 0, MaximumTransferKiB) : values.classicMenu ? 275 : 0;
  }
  const auto apply = [this, packet, id, classic = values.classicMenu](int value) {
    if (busy_ || closing_ || offlinePreview_ || !columnSession_ || packet->session != columnSession_
        || activeOptionsLoad_ || activeOptionsSave_) { return; }
    if (classic) { value = validatedClassicLimit(id, value, *packet->snapshot); }
    OptionsSubmission submission{packet->session, packet->request, packet->revision, {}, id};
    submission.changes.emplace_back(id, core::SettingValue(std::in_place_type<std::int64_t>, value));
    activeOptionsSave_ = OptionsSubmission{packet->session, packet->request, packet->revision, {}, id};
    emit optionsSaveRequested(std::move(submission));
  };
  const auto addRate = [&](const QString& name, int rate) {
    auto* action = menu->addAction(name);
    action->setObjectName(QStringLiteral("status-transfer-limit-%1").arg(rate));
    action->setCheckable(true);
    action->setChecked(!values.automatic && current == rate);
    connect(action, &QAction::triggered, this, [apply, rate] { apply(rate); });
  };

  menu->addSection(title);
  addRate(tr("Unlimited"), 0);
  auto presets = transferMenuPresets(values, anchor);
  if (!values.classicMenu && values.last > 0 && values.last <= MaximumTransferKiB) {
    presets.insert(static_cast<int>(values.last));
  }
  for (const auto rate : presets) { addRate(transferBytes(qint64{rate} * 1024, classicStatus_->transferUnits(), true), rate); }

  if (upload || !values.classicMenu) {
    auto* automatic = menu->addAction(values.autoSetting ? tr("Auto (Classic)") : tr("Auto (unavailable)"));
    automatic->setObjectName(QStringLiteral("status-transfer-limit-auto"));
    automatic->setEnabled(values.autoSetting.has_value());
    if(values.autoSetting){
      automatic->setCheckable(true);automatic->setChecked(values.automatic);
      connect(automatic,&QAction::triggered,this,[this,packet,id=*values.autoSetting](bool enabled){
        if(busy_||closing_||offlinePreview_||!columnSession_||packet->session!=columnSession_
            ||activeOptionsLoad_||activeOptionsSave_){return;}
        OptionsSubmission submission{packet->session,packet->request,packet->revision,{}, {}};
        submission.changes.emplace_back(id,enabled);
        activeOptionsSave_=submission;
        emit optionsSaveRequested(std::move(submission));
      });
    }
  }
  menu->addSeparator();
  if (values.classicMenu) {
    auto* custom = menu->addAction(tr("Set limit…"));
    custom->setObjectName(QStringLiteral("status-transfer-limit-custom"));
    connect(custom, &QAction::triggered, this, [this, title, current, apply] {
      QPointer<LibraryWindow> guard(this);
      bool accepted = false;
      const int value = QInputDialog::getInt(this, title, tr("Limit (KiB/s)"), current,
          0, MaximumTransferKiB, 1, &accepted);
      if (guard && accepted) { apply(value); }
    });
    return;
  }

  auto* editor = new QWidget(menu);
  auto* layout = new QHBoxLayout(editor);
  auto* slider = new QSlider(Qt::Horizontal, editor);
  slider->setObjectName(QStringLiteral("status-transfer-limit-slider"));
  slider->setAccessibleName(title);
  slider->setRange(0, current == 0 ? (upload ? 100 : 800) : std::clamp(current * 5, 50, MaximumTransferKiB));
  slider->setMinimumWidth(100);
  slider->setValue(current);
  auto* value = new QSpinBox(editor);
  value->setObjectName(QStringLiteral("status-transfer-limit-value"));
  value->setAccessibleName(title + tr(" (KiB/s)"));
  value->setRange(0, MaximumTransferKiB);
  value->setSuffix(tr(" KiB/s"));
  value->setSpecialValueText(tr("Unlimited"));
  value->setValue(current);
  auto* accept = new QPushButton(tr("Apply"), editor);
  accept->setObjectName(QStringLiteral("status-transfer-limit-apply"));
  connect(slider, &QSlider::valueChanged, value, &QSpinBox::setValue);
  connect(value, &QSpinBox::valueChanged, slider, [slider](int amount) {
    const QSignalBlocker blocker(slider);
    slider->setValue(amount);
  });
  const auto submit = [menu, value, apply] {
    const int amount = value->value();
    menu->close();
    apply(amount);
  };
  connect(accept, &QPushButton::clicked, this, submit);
  layout->addWidget(slider);
  layout->addWidget(value);
  layout->addWidget(accept);
  auto* action = new QWidgetAction(menu);
  action->setDefaultWidget(editor);
  menu->addAction(action);
}
