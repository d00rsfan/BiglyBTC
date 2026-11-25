// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
// The original BiglyBT donation window opens this page. Keep the labels and
// addresses together so a copy always uses the selected currency's address.
constexpr auto OfficialDonationPage = "https://www.biglybt.com/donation/donate.php";
constexpr struct { const char* currency; const char* address; } DonationWallets[] = {
    {"BTC / BCH / BSG / BSV", "1BiGLYBT38ttJhvZkjGc5mCw5uKoRHcUmr"},
    {"ADA", "addr1qyw8ya4uhe2ngcdv8rc8nq75wxvu6vjcdawql7kg8n7tf7fhldadzmdf6ex62pldggtwv5myj70cqkrlm8vsh7j8cg8qyffsev"},
    {"BEP20 (BSC) Tokens", "0x4e609B5EF88C8aA8Ab73945fD1ba68c9E27faC75"},
    {"BNB", "bnb1vn2cnxccmdhyfa27nqcnpvrhqpqayxl9z8wnal"},
    {"DASH", "XjDwmSrDPQBaLzCkuRHZaFzHf7mTVxkW9K"},
    {"DOGE", "DMXWdEtPUJc5p2sbHGo77SvqFXKTR8Vff1"},
    {"DOT", "13VhHqT2KrgZGDAD2vwB3P48J3aSFbd5pYAaCnEPYZDXXEe3"},
    {"ERC-20 Tokens", "0x4e609B5EF88C8aA8Ab73945fD1ba68c9E27faC75"},
    {"ETH/ETC", "0x4e609B5EF88C8aA8Ab73945fD1ba68c9E27faC75"},
    {"LTC", "LKGc2utCrGfojpYsX3naT9n1AxjLiZ5MMG"},
    {"TRX/BTT", "TAbsb7pjEEWNpXFvPf49rfVhFSB2e3dAM7"},
    {"XLM", "GD663TOEKQIA2C6J6JCCHUD5DAVYJ7EJKNL4VYNI47I2XC2TTMMDBQ67"},
    {"XRP", "rPFKfbx2tuzMMaz7Zy99M6CquHKgp9srSb"},
    {"XMR", "43pYoLDNKw4GdUxpJ8D2ETSdCxbSeFob1PiH1Jfg6Jgg2AHsY9URMBw8WGvL2ZS8ER4amP66RFVjtZXsY864BzaC5urU3ND"},
    {"XNO (Nano)", "nano_1po9mn4ahiuizjruajijozohtgfz7ebofqoqsatespdsabx85njzji86bqf6"},
};
}

void LibraryWindow::showDonationReminder(quint64 session) {
  if (session && session == columnSession_ && !offlinePreview_ && !closing_) {
    showDonationDialog();
  }
}

void LibraryWindow::showDonationDialog() {
  if (closing_) { return; }
  for (auto* existing : findChildren<QDialog*>(QStringLiteral("cryptocurrency-donation-dialog"))) {
    if (existing->isVisible()) { existing->raise(); existing->activateWindow(); return; }
  }

  auto* dialog = new QDialog(this);
  dialog->setObjectName(QStringLiteral("cryptocurrency-donation-dialog"));
  dialog->setWindowTitle(tr("Support the original BiglyBT"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  auto* layout = new QVBoxLayout(dialog);
  auto* description = new QLabel(tr("If BiglyBTC has been useful to you, consider supporting the developers who created and maintain the original BiglyBT. Donations go directly to the BiglyBT team."), dialog);
  description->setWordWrap(true);
  layout->addWidget(description);

  auto* form = new QFormLayout;
  auto* currency = new QComboBox(dialog);
  currency->setObjectName(QStringLiteral("donation-currency"));
  for (const auto& wallet : DonationWallets) {
    currency->addItem(QString::fromLatin1(wallet.currency), QString::fromLatin1(wallet.address));
  }
  form->addRow(tr("&Currency"), currency);
  auto* address = new QLineEdit(currency->currentData().toString(), dialog);
  address->setObjectName(QStringLiteral("donation-address"));
  address->setAccessibleName(tr("Wallet address"));
  address->setLayoutDirection(Qt::LeftToRight);
  address->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  address->setReadOnly(true);
  auto* copy = new QPushButton(tr("Copy address"), dialog);
  copy->setObjectName(QStringLiteral("donation-copy-address"));
  copy->setAutoDefault(false);
  auto* addressRow = new QHBoxLayout;
  addressRow->addWidget(address, 1);
  addressRow->addWidget(copy);
  form->addRow(tr("Address"), addressRow);
  layout->addLayout(form);

  auto* source = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>")
      .arg(QString::fromLatin1(OfficialDonationPage), tr("Official BiglyBT donation page").toHtmlEscaped()), dialog);
  source->setOpenExternalLinks(true);
  source->setTextInteractionFlags(Qt::TextBrowserInteraction);
  layout->addWidget(source);
  auto* copied = new QLabel(dialog);
  layout->addWidget(copied);
  connect(currency, &QComboBox::currentIndexChanged, dialog, [currency, address, copied] {
    address->setText(currency->currentData().toString());
    address->setCursorPosition(0);
    copied->clear();
  });
  connect(copy, &QPushButton::clicked, dialog, [address, copied] {
    QApplication::clipboard()->setText(address->text());
    copied->setText(tr("Address copied"));
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  if (columnSession_ && !offlinePreview_ && !busy_) {
    const auto session = columnSession_;
    auto* postpone = buttons->addButton(tr("Not now"), QDialogButtonBox::ActionRole);
    auto* donated = buttons->addButton(tr("I’ve already donated"), QDialogButtonBox::ActionRole);
    postpone->setObjectName(QStringLiteral("donation-postpone"));
    donated->setObjectName(QStringLiteral("donation-mark-donated"));
    const auto saveChoice = [this, dialog, session](bool alreadyDonated) {
      const QPointer<QDialog> guard(dialog);
      emit donationPreferenceRequested(session, alreadyDonated);
      if (guard) { dialog->accept(); }
    };
    connect(postpone, &QPushButton::clicked, dialog, [saveChoice] { saveChoice(false); });
    connect(donated, &QPushButton::clicked, dialog, [saveChoice] { saveChoice(true); });
  }
  layout->addWidget(buttons);
  connect(dialog, &QDialog::finished, this, [this] { emit donationDialogVisibilityChanged(false); });
  dialog->resize(570, dialog->sizeHint().height());
  currency->setFocus();
  const QPointer<QDialog> guard(dialog);
  emit donationDialogVisibilityChanged(true);
  if (guard) { dialog->open(); }
}
