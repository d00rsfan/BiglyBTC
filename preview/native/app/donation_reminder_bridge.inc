// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
constexpr auto DonationConfig = "biglybt.config";
constexpr auto DonationNextHours = "donations.nextAskHours";
constexpr auto DonationMinDate = "donations.minDate";
constexpr auto DonationMaxDate = "donations.maxDate";
constexpr auto DonationDonated = "donations.donated";

DonationReminder readDonationReminder(const core::Document& config) {
  const auto integer = [&config](const char* key) {
    const auto* value = config.find(key);
    return value && value->type == core::Node::Type::Integer ? value->integer : std::int64_t{0};
  };
  return {integer(DonationNextHours), integer(DonationMinDate), integer(DonationMaxDate), integer(DonationDonated) != 0};
}

template<class Update>
bool updateDonationReminder(const storage::ProfileLease& lease, Update update) {
  auto config = storage::ConfigFile::load(lease, DonationConfig);
  if (!config && config.error().code != storage::ErrorCode::Missing) { throw Failure{config.error()}; }
  auto document = config ? config->document() : *core::Document::parse("de");
  const auto previous = readDonationReminder(document);
  auto value = previous;
  const bool show = update(value);
  if (value == previous) { return show; }
  if (config && config->recoverySource() != storage::RecoverySource::Primary) {
    throw Failure{{storage::ErrorCode::Conflict, "Resolve the recovered profile settings before saving donation reminders"}};
  }
  for (const auto& [key, before, after] : {
      std::tuple{DonationNextHours, previous.nextHours, value.nextHours},
      std::tuple{DonationMinDate, previous.minDate, value.minDate},
      std::tuple{DonationMaxDate, previous.maxDate, value.maxDate},
      std::tuple{DonationDonated, std::int64_t{previous.donated}, std::int64_t{value.donated}}}) {
    if (before != after && !document.replaceValue(key, "i" + std::to_string(after) + "e")) {
      throw Failure{{storage::ErrorCode::InvalidFormat, "Invalid donation reminder settings"}};
    }
  }
  if (config) {
    config->document() = std::move(document);
    take(config->save(lease));
  } else {
    take(storage::ConfigFile::createMissing(lease, DonationConfig, document.serialize()));
  }
  return show;
}
}

void SessionBridge::publishDonationReminder() noexcept {
  if (!host_ || !std::exchange(host_->donationIntakePending, false) || !host_->overall
      || host_->overall->uncertified() || host_->fileRecoveryRequired || donationDialogOpen_.load()) { return; }
  try {
    const auto hours = host_->overall->observation().uptime / 3600;
    const auto now = wallMillis();
    if (!updateDonationReminder(host_->lease, [=](DonationReminder& value) { return value.onTorrentAdded(hours, now); })) { return; }
    QMetaObject::invokeMethod(this, [this, session = host_->columnLayoutSession] {
      if (!profileClosed_ && !closingSession_ && session == publishedColumnLayoutSession_ && !donationDialogOpen_.load()) {
        emit donationReminderReady(session);
      }
    }, Qt::QueuedConnection);
  } catch (...) {
    // Reminder storage is independent of the durable torrent import.
    qWarning("BiglyBTC could not update the donation reminder schedule");
  }
}

bool SessionBridge::updateDonationPreference(quint64 session, bool donated) {
  if (profileClosed_ || !session || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session, donated](std::stop_token) {
    if (!host_ || !host_->normalNetwork || host_->fileRecoveryRequired || host_->columnLayoutSession != session
        || (!donated && (!host_->overall || host_->overall->uncertified()))) {
      throw Failure{{storage::ErrorCode::Conflict, "The donation reminder profile is unavailable"}};
    }
    take(storage::requireNoPendingSubscriptionAcquisition(host_->lease));
    const auto now = wallMillis();
    updateDonationReminder(host_->lease, [&](DonationReminder& value) {
      if (donated) { value.markDonated(now); }
      else { value.postpone(host_->overall->observation().uptime / 3600, now); }
      return false;
    });
    QMetaObject::invokeMethod(this, [this] {
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false);
}
