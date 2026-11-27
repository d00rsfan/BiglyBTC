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
using Network=search::SubscriptionSeedNetwork;

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

void checkFixture(const core::Node& fixture,Network route) {
  const auto config=core::Document::parse(std::string(field(fixture,"config")));
  const auto* subs=config?config->find("subs"):nullptr;
  require(subs&&subs->children.size()==1,"Java source needs a single subscription row");
  const auto& row=subs->children.front();
  const auto original=field(fixture,"metainfo");
  auto payload=std::string(field(fixture,"payload"));
  auto seed=search::makeSubscriptionSeed(row,payload,1000);
  require(seed.has_value(),"Java signature, including legacy az_version fallback");
  require(seed->anonymous==(route==Network::I2P),"Use the signed Java publication route");
  require(seed->fileName==field(fixture,"file"),"Original versioned filename");
  require(std::ranges::equal(seed->publicationHash,field(fixture,"hash")),"Signed publication hash");
  require(std::ranges::equal(seed->lookupKey,field(fixture,"lookup")),"Original lookup key");
  require(encodedInfo(seed->metainfo)==encodedInfo(std::string(original)),"Exact Java info fields and payload pieces");
  const auto split=seed->fileName.find('.');
  const std::string expectedKey="subscription:publish:"+seed->fileName.substr(0,split)+":4";
  require(seed->presence.has_value(),"The Java fixture permits public publication");
  const auto publication=search::encodeSubscriptionPresence(*seed->presence,-315);
  require(seed->presence->applicationKey==expectedKey&&publication&&publication->front()=='\0',
      "Encoded signed presence under the original application key");
  const auto data=core::Document::parse(publication->substr(1));
  require(data&&data->find("!")&&data->find("!")->integer==-315
      &&data->find("v")&&data->find("v")->integer==4
      &&data->find("z")&&data->find("z")->integer==row.find("sig_data_size")->integer
      &&field(data->root(),"h")==field(fixture,"hash")&&field(data->root(),"s")==field(row,"sig"),
      "Keep exactly the hash, version, size, and signature supplied by Java");
  const auto publicKey=field(row,"public_key");
  const auto verified=search::verifySubscriptionPresence(*publication,publicKey,4);
  require(verified&&verified->applicationKey==expectedKey&&verified->publicationHash==seed->publicationHash,
      "Verify a signed record against the expected subscription and version");
  require(!search::verifySubscriptionPresence(*publication,publicKey,5),"Reject a version mismatch");
  auto forgedPublication=*publication;
  const auto hashOffset=forgedPublication.find(field(fixture,"hash"));
  require(hashOffset!=std::string::npos,"Publication contains the signed hash");
  forgedPublication[hashOffset]^=1;
  const auto forgedSignature=search::verifySubscriptionPresence(forgedPublication,publicKey,4);
  require(!forgedSignature&&forgedSignature.error()==search::SubscriptionSeedError::Signature,
      "Reject a substituted hash even when the record has the expected version");

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
  auto privateSource=core::Document::parse(*rawRow);
  require(privateSource&&privateSource->replaceValue("is_public","i0e"),"Mark the profile subscription private");
  const auto privateSeed=search::makeSubscriptionSeed(privateSource->root(),std::string(field(fixture,"payload")),1000);
  require(privateSeed&&!privateSeed->presence,"A locally private subscription cannot supply a presence record");
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

void checkSavedSource(const core::Node& fixture,Network route) {
  QTemporaryDir temporary;
  require(temporary.isValid(),"Isolated profile directory");
  const std::filesystem::path root(temporary.path().toStdU16String());
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
  const auto other=route==Network::Public?Network::I2P:Network::Public;
  const auto candidates=search::savedSubscriptionSeedCandidates(*lease,route);
  const auto excluded=search::savedSubscriptionSeedCandidates(*lease,other);
  require(candidates&&candidates->size()==1&&excluded&&excluded->empty(),"Watch only the saved subscription's route");
  const auto& candidate=candidates->front().hash;
  require(std::string_view(candidate.data(),candidate.size())==field(fixture,"hash"),"Watch the signed publication identity");
  const auto byHash=[&](Network network) {
    return search::readSavedSubscriptionSeed(*lease,field(fixture,"hash"),Key::Publication,network,1000);
  };
  const auto byLookup=[&] {
    return search::readSavedSubscriptionSeed(*lease,field(fixture,"lookup"),Key::DdbLookup,route,1000);
  };
  const auto seed=byHash(route),dhtSeed=byLookup(),otherRoute=byHash(other);
  const auto otherLookup=search::readSavedSubscriptionSeed(*lease,field(fixture,"lookup"),Key::DdbLookup,other,1000);
  require(seed&&*seed&&dhtSeed&&*dhtSeed&&otherRoute&&!*otherRoute&&otherLookup&&!*otherLookup,
      "Both lookup keys stay on their signed route");
  {
    auto access=lease->sourceAccess();
    require(access.has_value(),"Obtain the broker's outer read access");
    const auto authorized=search::readSavedSubscriptionSeed(*lease,*access,field(fixture,"lookup"),
        Key::DdbLookup,route,1000);
    require(authorized&&*authorized&&!byLookup(),"An outer permit authorizes read without admitting a nested observer");
    QTemporaryDir unrelatedProfile;
    require(unrelatedProfile.isValid(),"Isolated unrelated profile");
    auto unrelated=storage::ProfileLease::acquire(std::filesystem::path(unrelatedProfile.path().toStdU16String()));
    require(unrelated.has_value(),"Own unrelated profile");
    const auto unrelatedAccess=unrelated->sourceAccess();
    require(unrelatedAccess.has_value(),"Read access for the unrelated lock");
    const auto denied=search::readSavedSubscriptionSeed(*lease,*unrelatedAccess,field(fixture,"lookup"),
        Key::DdbLookup,route,1000);
    require(!denied,"Source access belongs to its original lock");
  }
  require((**seed).payload==body&&(**seed).metainfo.fileName==fileName,"Original profile's signed body and versioned name");
  require((**seed).metainfo.metainfo==(**dhtSeed).metainfo.metainfo,"Both identities select the same versioned bundle");

  const auto originalConfig=core::Document::parse(std::string(field(fixture,"config")));
  require(originalConfig&&originalConfig->find("subs"),"Read original subscription configuration");
  const auto& row=originalConfig->find("subs")->children.front();
  const auto rawRow=core::encodeValue(row);
  require(rawRow.has_value(),"Encode original subscription row");
  auto switched=core::Document::parse(*rawRow);
  require(switched&&switched->replaceValue("is_anonymous",route==Network::I2P?"i0e":"i1e"),"Change only the saved route");
  write(root/"subscriptions.config","d4:subsl"+switched->serialize()+"ee");
  const auto forged=byHash(other);
  require(!forged&&forged.error()==search::SavedRssSourceError::Malformed,
      "An unsigned route change cannot override the signed body");
  write(root/"subscriptions.config",field(fixture,"config"));

  auto settings=core::Document::parse("de");
  require(settings&&settings->replaceValue("Plugin.DHT.dht.torrent_xfer.enable","i0e"),"Disable original DDB setting");
  write(root/"biglybt.config",settings->serialize());
  const auto disabled=byLookup();
  require(disabled&&!*disabled,"Disabled DDB cannot authorize a transfer");
  const auto tracked=byHash(route);
  require(tracked&&*tracked,"The DDB switch leaves the subscribed seed source available");

  using FileOperation=search::SubscriptionSeedFile;
  const auto resolve=[&](FileOperation operation) {
    return search::resolveVersionedSubscriptionSeed(*lease,field(fixture,"hash"),route,1000,operation);
  };
  const auto versioned=root/"subs"/std::string(fileName);
  const auto missing=resolve(FileOperation::Verify);
  require(!missing&&missing.error()==search::SavedRssSourceError::SourceChanged&&!std::filesystem::exists(versioned),
      "Revalidation does not create a missing payload");
  {
    auto access=lease->sourceAccess();
    require(bool(access),"Hold one actual observation permit");
    const auto denied=search::resolveVersionedSubscriptionSeed(*lease,*access,field(fixture,"hash"),route,1000,
        FileOperation::Prepare);
    require(!denied&&!std::filesystem::exists(versioned),"Read-only permit cannot publish a versioned file");
  }
  {
    auto access=lease->sourceAccess(true);
    require(bool(access),"Hold the exact mutation permit used by the private worker");
    const auto prepared=search::resolveVersionedSubscriptionSeed(*lease,*access,field(fixture,"hash"),route,1000,
        FileOperation::Prepare);
    const auto verified=search::resolveVersionedSubscriptionSeed(*lease,*access,field(fixture,"hash"),route,1000,
        FileOperation::Verify);
    require(prepared&&*prepared&&verified&&*verified&&(**prepared).directoryHandle,
        "One owned worker permit stages and revalidates the signed version");
  }
  const auto ready=resolve(FileOperation::Prepare);
  require(ready&&*ready&&(**ready).directory==root/"subs"&&(**ready).directoryHandle,
      "Prepared file retains the owned subscriptions directory");
  const auto readVersioned=[&] {
    std::ifstream file(versioned,std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
  };
  require(readVersioned()==body,"Publish the signed source bytes under its versioned name");
  const auto checked=resolve(FileOperation::Verify);
  require(checked&&*checked&&(**checked).directory==(**ready).directory&&!(**checked).directoryHandle
      &&(**checked).metainfo.metainfo==(**ready).metainfo.metainfo,"Confirm the prepared version under the same profile");
  require(!(**checked).ddbTransferAllowed,"The disabled DDB switch excludes the held metadata response");
  require(settings->replaceValue("Plugin.DHT.dht.torrent_xfer.enable","i1e").has_value(),"Enable the saved transfer option");
  write(root/"biglybt.config",settings->serialize());
  const auto transfer=resolve(FileOperation::Verify);
  require(transfer&&*transfer&&(**transfer).ddbTransferAllowed==(route==Network::I2P),
      "The held I2P projection reflects the enabled transfer option");
  const auto repeated=resolve(FileOperation::Prepare);
  require(repeated&&*repeated&&readVersioned()==body,"Retry reuses the identical versioned file");
  write(versioned,"sentinel");
  const auto modified=resolve(FileOperation::Verify);
  require(!modified&&modified.error()==search::SavedRssSourceError::SourceChanged&&readVersioned()=="sentinel",
      "A modified versioned payload revokes read-only authorization");
  const auto collision=resolve(FileOperation::Prepare);
  require(!collision&&collision.error()==search::SavedRssSourceError::SourceChanged&&readVersioned()=="sentinel",
      "A conflicting Java versioned file cannot be replaced");

  write(root/"subscriptions.config.saving","de");
  const auto pending=byHash(route);
  require(!pending&&pending.error()==search::SavedRssSourceError::SourceChanged,"Partial profile state cannot authorize a new seed");
  require(!search::savedSubscriptionSeedCandidates(*lease,route),"Withdraw candidates when a saved write is pending");
}
} // namespace

int main(int argc,char** argv) try {
  if(argc!=2) { return 2; }
  std::ifstream file(argv[1],std::ios::binary);
  require(bool(file),"Java fixture input");
  const auto fixture=core::Document::parse({std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()});
  require(fixture&&fixture->find("new")&&fixture->find("old")&&fixture->find("i2p"),"Public and I2P Java fixtures");
  checkFixture(*fixture->find("new"),Network::Public);
  checkFixture(*fixture->find("old"),Network::Public);
  checkFixture(*fixture->find("i2p"),Network::I2P);
  checkSavedSource(*fixture->find("new"),Network::Public);
  checkSavedSource(*fixture->find("i2p"),Network::I2P);
  std::cout<<"Java subscription source routing, metainfo, signatures and DDB encryption passed\n";
  return 0;
} catch(const std::exception& error) {
  std::cerr<<error.what()<<'\n';
  return 1;
}
