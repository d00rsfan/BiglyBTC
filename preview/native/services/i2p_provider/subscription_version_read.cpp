// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_version_read.hpp"

#include "subscription_seed_runtime.hpp"
#include <QCryptographicHash>

#include <algorithm>
#include <string_view>

namespace biglybtc::services::i2p::provider {
namespace {
constexpr std::size_t MaximumReporters=8;
constexpr std::size_t MaximumSignedVersions=4;

dht::Id sha1(std::string_view input) {
  const auto digest=QCryptographicHash::hash(QByteArrayView(input.data(),static_cast<qsizetype>(input.size())),
      QCryptographicHash::Sha1);
  dht::Id result{};
  std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()),result.size(),result.begin());
  return result;
}

unsigned remaining(SubscriptionVersionRead::Clock::time_point deadline,unsigned limit) {
  const auto milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline-SubscriptionVersionRead::Clock::now()).count();
  return static_cast<unsigned>(std::clamp<std::int64_t>(milliseconds,0,limit));
}

dht::Id versionKey(std::string_view publicKey,std::uint32_t version) {
  const auto hash=sha1(publicKey);
  const auto sid=QByteArray(reinterpret_cast<const char*>(hash.data()),10).toHex().toUpper();
  return sha1("subscription:publish:"+sid.toStdString()+':'+std::to_string(version));
}
}

SubscriptionVersionRead::~SubscriptionVersionRead() { cancel(); }

bool SubscriptionVersionRead::startFromAssociation(std::string publicKey,std::uint32_t currentVersion,
    const dht::Id& key,Clock::time_point deadline) {
  if (active()||outcome_||(publicKey.size()!=25&&publicKey.size()!=49)||
      currentVersion>=0x00ffffff) { return false; }
  deadline_=deadline;
  if (!beginLookup(key,30,Phase::Association)) { return false; }
  publicKey_=std::move(publicKey);
  version_=currentVersion;
  return true;
}

bool SubscriptionVersionRead::beginLookup(const dht::Id& key,std::uint8_t maximum,Phase phase) {
  const auto time=remaining(deadline_,120000);
  if (!time) { return false; }
  const auto operation=actor_.lookupAzureusValues(key,{maximum,0},time);
  if (!operation) { return false; }
  key_=key;
  phase_=phase;
  operation_=*operation;
  return true;
}

bool SubscriptionVersionRead::selectVersion(const dht::Snapshot& snapshot) {
  const auto hash=sha1(publicKey_);
  auto newest=version_;
  for (const auto& value:snapshot.azureusValues) {
    const std::string_view bytes(reinterpret_cast<const char*>(value.bytes.data()),value.bytes.size());
    const auto hint=search::decodeSubscriptionAssociationHint(bytes);
    if (!hint||hint->version<=newest) { continue; }
    if (std::equal(hint->shortId.begin(),hint->shortId.end(),hash.begin(),[](char first,auto second) {
      return static_cast<unsigned char>(first)==second;
    })) { newest=hint->version; }
  }
  if (newest==version_||!beginLookup(versionKey(publicKey_,newest),12,Phase::Presence)) { return false; }
  version_=newest;
  return true;
}

void SubscriptionVersionRead::selectReporters(const dht::Snapshot& snapshot) {
  const auto now=Clock::now();
  reporterDeadline_=deadline_-std::min((deadline_-now)/3,
      std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(30)));
  std::vector<std::optional<search::SignedSubscriptionPresence>> verified;
  verified.reserve(snapshot.azureusValues.size());
  for (const auto& value:snapshot.azureusValues) {
    const std::string_view bytes(reinterpret_cast<const char*>(value.bytes.data()),value.bytes.size());
    auto presence=search::verifySubscriptionPresence(bytes,publicKey_,version_);
    if(presence&&signedVersions_.size()<MaximumSignedVersions&&std::ranges::none_of(signedVersions_,[&](const auto& found){
      return found.publicationHash==presence->publicationHash;
    })) { signedVersions_.push_back(*presence); }
    verified.push_back(presence?std::optional{std::move(*presence)}:std::nullopt);
  }
  // A replica may have the signed version record and no corresponding file.
  // The compact origin is a route hint. DATA still needs a signed reply from
  // the resolved Destination, and the final torrent payload needs its signature.
  for (std::size_t index=0;index<verified.size()&&reporters_.size()<MaximumReporters/2;++index) {
    const auto& origin=snapshot.azureusValues[index].origin;
    if (verified[index]&&origin&&origin->protocol>=dht::MinimumAzureusProtocol&&dht::validContact(origin->contact)) {
      rememberReporter(origin->contact,*verified[index]);
    }
  }
  for (const auto& match:snapshot.azureusReporters) {
    if (!match.contact.destination||!dht::validContact(match.contact)) { continue; }
    for (const auto index:match.valueIndexes) {
      if (index>=verified.size()||!verified[index]) { continue; }
      rememberReporter(match.contact,*verified[index]);
    }
  }
}

void SubscriptionVersionRead::rememberReporter(const dht::Contact& contact,
    const search::SignedSubscriptionPresence& presence) {
  if (!dht::validContact(contact)) { return; }
  const auto same=[&](const Candidate& candidate) {
    return candidate.reporter.id==contact.id&&candidate.reporter.hash==contact.hash&&
        candidate.reporter.port==contact.port&&candidate.presence.publicationHash==presence.publicationHash;
  };
  if (std::ranges::any_of(attempted_,same)) { return; }
  const auto existing=std::ranges::find_if(reporters_,same);
  if (existing!=reporters_.end()) {
    if (!existing->reporter.destination&&contact.destination) { existing->reporter=contact; }
  } else if (reporters_.size()<MaximumReporters) { reporters_.push_back({contact,presence}); }
}

bool SubscriptionVersionRead::beginTorrentContactLookup() {
  if (nextTorrentLookup_>=signedVersions_.size()) { return false; }
  const auto& hash=signedVersions_[nextTorrentLookup_].publicationHash;
  if (!beginLookup(sha1({hash.data(),hash.size()}),30,Phase::TorrentContacts)) { return false; }
  ++nextTorrentLookup_;
  reporters_.clear();
  nextReporter_=0;
  return true;
}

void SubscriptionVersionRead::selectTorrentReporters(const dht::Snapshot& snapshot) {
  // MagnetPlugin reads the DDB using the torrent hash as the application key.
  // These contacts only direct a signed transfer for the authenticated hash.
  const auto& presence=signedVersions_[nextTorrentLookup_-1];
  for (const auto& value:snapshot.azureusValues) {
    if (reporters_.size()>=MaximumReporters/2) { break; }
    if (value.origin&&value.origin->protocol>=dht::MinimumAzureusProtocol) {
      rememberReporter(value.origin->contact,presence);
    }
  }
  for (const auto& reporter:snapshot.azureusReporters) {
    if (reporter.contact.destination&&std::ranges::any_of(reporter.valueIndexes,[&](auto index){
      return index<snapshot.azureusValues.size();
    })) { rememberReporter(reporter.contact,presence); }
  }
}

bool SubscriptionVersionRead::beginMetainfo() {
  static const auto handler=sha1(engine::SubscriptionSeedTransferType);
  while (nextReporter_<reporters_.size()) {
    const auto time=remaining(nextTorrentLookup_?deadline_:reporterDeadline_,30000);
    if (!time) { break; }
    const auto& selected=reporters_[nextReporter_++];
    attempted_.push_back(selected);
    const auto& hash=selected.presence.publicationHash;
    const auto key=sha1({hash.data(),hash.size()});
    const auto operation=actor_.readAzureusMetainfo(selected.reporter,handler,key,time);
    if (operation) {
      operation_=*operation;
      phase_=Phase::Metainfo;
      return true;
    }
  }
  return false;
}

void SubscriptionVersionRead::advance() try {
  if (!active()) { return; }
  if (Clock::now()>=deadline_) { cancel();return; }
  if (phase_==Phase::Metainfo) {
    const auto observed=actor_.metainfoObservation();
    if (observed.operation!=operation_) { cancel();return; }
    if (observed.phase==dht::Phase::Running) { return; }
    if (observed.phase==dht::Phase::Complete) {
      auto& presence=reporters_[nextReporter_-1].presence;
      const std::string_view bytes(reinterpret_cast<const char*>(observed.data.data()),observed.data.size());
      if (auto metainfo=search::openSubscriptionSeedDdbResponse(bytes,presence)) {
        Outcome result{std::move(presence),std::move(*metainfo)};
        cancel();
        outcome_=std::move(result);
        return;
      }
    }
  } else {
    const auto observed=actor_.observation();
    if (observed.operation!=operation_||observed.target!=key_) { cancel();return; }
    if (observed.phase==dht::Phase::Running) { return; }
    if (observed.phase!=dht::Phase::Complete&&observed.phase!=dht::Phase::TimedOut) { cancel();return; }
    if (phase_==Phase::Association) {
      if (!selectVersion(observed)) { cancel(); }
      return;
    }
    if (phase_==Phase::Presence) { selectReporters(observed); }
    else { selectTorrentReporters(observed); }
  }
  if (!beginMetainfo()&&!beginTorrentContactLookup()) { cancel(); }
} catch (...) {
  cancel();
}

void SubscriptionVersionRead::cancel() noexcept {
  if (const auto operation=std::exchange(operation_,0)) { static_cast<void>(actor_.cancel(operation)); }
  version_=0;
  publicKey_.clear();
  reporters_.clear();
  attempted_.clear();
  signedVersions_.clear();
  nextReporter_=0;
  nextTorrentLookup_=0;
  phase_=Phase::Presence;
  outcome_.reset();
}

} // namespace biglybtc::services::i2p::provider
