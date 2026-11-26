// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/bencode.hpp"
#include "saved_subscription_seed.hpp"
#include "subscription_seed_values.hpp"

#include <QTemporaryDir>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace biglybtc;

void require(bool pass,const char* message) {
  if(!pass) { throw std::runtime_error(message); }
}

std::string_view field(const core::Node& value,std::string_view name) {
  const auto* entry=value.find(name);
  if(!entry||entry->type!=core::Node::Type::Bytes) { throw std::runtime_error("Missing Java fixture value"); }
  return entry->bytes;
}

std::string encodedInfo(std::string bytes) {
  const auto torrent=core::Document::parse(std::move(bytes));
  require(torrent&&torrent->find("info"),"Metainfo must contain info fields");
  const auto result=core::encodeValue(*torrent->find("info"));
  require(result.has_value(),"Metainfo encoding");
  return *result;
}

void checkFixture(const core::Node& fixture) {
  const auto config=core::Document::parse(std::string(field(fixture,"config")));
  const auto* subs=config?config->find("subs"):nullptr;
  require(subs&&subs->children.size()==1,"Java source needs a single subscription row");
  const auto& row=subs->children.front();
  const auto original=field(fixture,"metainfo");
  auto payload=std::string(field(fixture,"payload"));
  auto seed=search::makeSubscriptionSeed(row,payload,1000);
  require(seed.has_value(),"Java signature, including legacy az_version fallback");
  require(seed->fileName==field(fixture,"file"),"Original versioned filename");
  require(std::ranges::equal(seed->publicationHash,field(fixture,"hash")),"Signed publication hash");
  require(std::ranges::equal(seed->lookupKey,field(fixture,"lookup")),"Original lookup key");
  require(encodedInfo(seed->metainfo)==encodedInfo(std::string(original)),"Exact Java info fields and payload pieces");

  seed->metainfo=original;
  const auto encrypted=search::subscriptionSeedDdbResponse(*seed);
  require(encrypted&&*encrypted==field(fixture,"response"),"Exact Java DDB response bytes");

  const auto altered=payload.find("aaaaaaaa");
  require(altered!=std::string::npos,"Fixture has signed contents to alter");
  payload[altered]='b';
  const auto changed=search::makeSubscriptionSeed(row,payload,1000);
  require(!changed&&changed.error()==search::SubscriptionSeedError::Signature,"Reject altered signed details");

  const auto rawRow=core::encodeValue(row);
  require(rawRow.has_value(),"Original source row");
  auto unsubscribed=core::Document::parse(*rawRow);
  require(unsubscribed&&unsubscribed->replaceValue("subscribed","i0e"),"Test unsubscribe");
  const auto declined=search::makeSubscriptionSeed(unsubscribed->root(),std::string(field(fixture,"payload")),1000);
  require(!declined&&declined.error()==search::SubscriptionSeedError::Ineligible,"Unsubscribed source cannot seed");
}

void write(const std::filesystem::path& path,std::string_view bytes) {
  std::ofstream file(path,std::ios::binary);
  file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
  require(bool(file),"Write generated profile fixture");
}

void checkSavedSource(const core::Node& fixture) {
  QTemporaryDir temporary;
  require(temporary.isValid(),"Isolated profile directory");
  const std::filesystem::path root(temporary.path().toStdString());
  std::filesystem::create_directory(root/"subs");
  auto lease=storage::ProfileLease::acquire(root);
  require(lease.has_value(),"Own isolated profile");
  const auto fileName=field(fixture,"file");
  const auto unversioned=fileName.substr(0,fileName.rfind('.'));
  const auto sourceFile=root/"subs"/std::string(unversioned);
  const auto body=field(fixture,"payload");
  write(root/"subscriptions.config",field(fixture,"config"));
  write(sourceFile,body);
  using Key=search::SubscriptionSeedKey;
  using Network=search::SubscriptionSeedNetwork;
  const auto candidates=search::savedSubscriptionSeedCandidates(*lease,Network::Public);
  const auto anonymous=search::savedSubscriptionSeedCandidates(*lease,Network::I2P);
  require(candidates&&candidates->size()==1&&anonymous&&anonymous->empty(),"Watch only the saved subscription's route");
  const auto& candidate=candidates->front().hash;
  require(std::string_view(candidate.data(),candidate.size())==field(fixture,"hash"),"Watch the signed publication identity");
  const auto byHash=[&](Network network) {
    return search::readSavedSubscriptionSeed(*lease,field(fixture,"hash"),Key::Publication,network,1000);
  };
  const auto byLookup=[&] {
    return search::readSavedSubscriptionSeed(*lease,field(fixture,"lookup"),Key::DdbLookup,Network::Public,1000);
  };
  const auto publicSeed=byHash(Network::Public),dhtSeed=byLookup(),otherRoute=byHash(Network::I2P);
  require(publicSeed&&*publicSeed&&dhtSeed&&*dhtSeed&&otherRoute&&!*otherRoute,"Lookup routes stay linked to profile permission");
  require((**publicSeed).payload==body&&(**publicSeed).metainfo.fileName==fileName,"Original profile's signed body and versioned name");
  require((**publicSeed).metainfo.metainfo==(**dhtSeed).metainfo.metainfo,"Both identities select the same versioned bundle");

  auto config=core::Document::parse("de");
  require(config&&config->replaceValue("Plugin.DHT.dht.torrent_xfer.enable","i0e"),"Disable original DDB setting");
  write(root/"biglybt.config",config->serialize());
  const auto disabled=byLookup();
  require(disabled&&!*disabled,"Disabled DDB cannot authorize a transfer");
  const auto tracked=byHash(Network::Public);
  require(tracked&&*tracked,"The DDB switch leaves the subscribed seed source available");

  const auto prepare=[&] {
    return search::prepareSavedSubscriptionSeed(*lease,field(fixture,"hash"),Network::Public,1000);
  };
  const auto ready=prepare();
  require(ready&&*ready&&(**ready).directory==root/"subs","Prepared file lives in the held subscriptions directory");
  const auto versioned=(**ready).directory/std::string(fileName);
  const auto readVersioned=[&] {
    std::ifstream file(versioned,std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
  };
  require(readVersioned()==body,"Publish the signed source bytes under its versioned name");
  const auto repeated=prepare();
  require(repeated&&*repeated&&readVersioned()==body,"Retry reuses the identical versioned file");
  write(versioned,"sentinel");
  const auto collision=prepare();
  require(!collision&&collision.error()==search::SavedRssSourceError::SourceChanged&&readVersioned()=="sentinel",
      "A conflicting Java versioned file cannot be replaced");

  write(root/"subscriptions.config.saving","de");
  const auto pending=byHash(Network::Public);
  require(!pending&&pending.error()==search::SavedRssSourceError::SourceChanged,"Partial profile state cannot authorize a new seed");
  require(!search::savedSubscriptionSeedCandidates(*lease,Network::Public),"Withdraw candidates when a saved write is pending");
}
} // namespace

int main(int argc,char** argv) try {
  if(argc!=2) { return 2; }
  std::ifstream file(argv[1],std::ios::binary);
  require(bool(file),"Java fixture input");
  const auto fixture=core::Document::parse({std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()});
  require(fixture&&fixture->find("new")&&fixture->find("old"),"Current and legacy Java fixtures");
  checkFixture(*fixture->find("new"));
  checkFixture(*fixture->find("old"));
  checkSavedSource(*fixture->find("new"));
  std::cout<<"Java subscription seed metainfo, signatures and DDB encryption passed\n";
  return 0;
} catch(const std::exception& error) {
  std::cerr<<error.what()<<'\n';
  return 1;
}
