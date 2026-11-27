// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/bencode.hpp"
#include "biglybtc/library.hpp"
#include "saved_subscription_seed.hpp"
#include "subscription_seed_values.hpp"
#include "subscription_upgrade_values.hpp"

#include <QByteArray>
#include <QTemporaryDir>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace biglybtc;
using Network=search::SubscriptionSeedNetwork;

void require(bool pass,const char* message) {
  if(!pass) { throw std::runtime_error(message); }
}

void checkSubscriptionAssociationHints() {
  // Original publishAssociation and DHTControlImpl.encodeKey, with generated
  // SHA-1 and SHA-256 torrent identities under the same Java key format.
  const std::pair<QByteArray,QByteArray> hashes[]{
    {"808182838485868788898A8B8C8D8E8F90919293","1E95202254A5E09E2FE777771E398CA87FE4D7B0"},
    {"808182838485868788898A8B8C8D8E8F909192939495969798999A9B9C9D9E9F","A4D715F532FE4FD1911B3568009733E570B0700E"},
  };
  const std::pair<std::uint32_t,QByteArray> versions[]{
    {1,"000001E700FF0203040506070880"},
    {0x89abcd,"89ABCDE700FF0203040506070880"},
    {0xffffff,"FFFFFFE700FF0203040506070880"},
  };
  search::SubscriptionAssociationHint hint;
  const auto shortId=QByteArray::fromHex("00FF0203040506070880");
  require(shortId.size()==static_cast<qsizetype>(hint.shortId.size()),"Generated short ID length");
  std::copy_n(shortId.cbegin(),hint.shortId.size(),hint.shortId.begin());
  hint.fixedRandom=0xe7;
  for (const auto& [version,expected] : versions) {
    hint.version=version;
    const auto wire=search::encodeSubscriptionAssociationHint(hint);
    require(wire&&QByteArray(wire->data(),wire->size()).toHex().toUpper()==expected,
        "Original Java association version, nonce, and SID bytes");
    const auto parsed=search::decodeSubscriptionAssociationHint({wire->data(),wire->size()});
    require(parsed&&*parsed==hint,"Reading an association preserves all hint fields");
  }
  for (const auto& [hash,expected] : hashes) {
    const auto torrent=core::TorrentHash::parse(QByteArray::fromHex(hash).toStdString());
    require(torrent.has_value(),"Generated torrent hash");
    const auto key=search::subscriptionAssociationDhtKey(*torrent);
    require(key&&QByteArray(key->data(),key->size()).toHex().toUpper()==expected,"Original Java association DHT key");
  }
  require(!search::encodeSubscriptionAssociationHint({}),"Ignore a zero association version");
  hint.version=0x1000000;
  require(!search::encodeSubscriptionAssociationHint(hint),"Reject an association version that would wrap");
  require(!search::decodeSubscriptionAssociationHint(std::string(14,'\0'))&&
      !search::decodeSubscriptionAssociationHint(std::string(13,'\1'))&&
      !search::decodeSubscriptionAssociationHint(std::string(15,'\1')),"Ignore unusable association hints");
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
  const auto decoded=search::openSubscriptionSeedDdbResponse(*encrypted,*verified);
  require(decoded&&*decoded==original,"Validate the original encrypted metainfo against its signed version");
  const std::string clear=std::string("\x01\0",2)+std::string(original);
  require(search::openSubscriptionSeedDdbResponse(clear,*verified).has_value(),"Original plain DDB fallback");
  auto wrongHash=*verified;
  wrongHash.publicationHash.front()^=1;
  require(!search::openSubscriptionSeedDdbResponse(clear,wrongHash),"The expected publication hash must match the metainfo");
  auto withWebSeed=core::Document::parse(std::string(original));
  require(withWebSeed&&withWebSeed->replaceValue("url-list","1:x"),"Create a different network route in the fixture");
  require(!search::openSubscriptionSeedDdbResponse(std::string("\x01\0",2)+withWebSeed->serialize(),*verified),
      "Remote metadata cannot add a clear-network source");

  const auto anonymous=route==Network::I2P;
  require(search::validateSubscriptionSeedPayload(payload,*verified,publicKey,anonymous).has_value(),
      "Verify the complete downloaded body under the pinned subscription key");
  require(!search::validateSubscriptionSeedPayload(payload,*verified,publicKey,!anonymous),
      "Reject the same signed payload on the wrong route");
  constexpr std::string_view formatField="10:az_versioni1e";
  const auto formatOffset=payload.find(formatField);
  require(formatOffset!=std::string::npos,"Reference body declares the original format");
  auto futureFormat=payload;
  futureFormat[formatOffset+formatField.size()-2]='2';
  require(!search::validateSubscriptionSeedPayload(futureFormat,*verified,publicKey,anonymous),
      "Reject an unsupported format even for legacy signatures");
  auto wrongKey=*verified;
  wrongKey.dhtKey.front()^=1;
  require(!search::validateSubscriptionSeedPayload(payload,wrongKey,publicKey,anonymous),
      "Reject a signed payload associated with a different DHT key");

  const auto altered=payload.find("aaaaaaaa");
  require(altered!=std::string::npos,"Fixture has signed contents to alter");
  payload[altered]='b';
  const auto alteredPayload=search::validateSubscriptionSeedPayload(payload,*verified,publicKey,anonymous);
  require(!alteredPayload&&alteredPayload.error()==search::SubscriptionSeedError::Signature,
      "The metainfo cannot authorize modified signed details");
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
  {
    const std::string previousHash(20,'\x81'), latestHash(20,'\x82');
    auto associated=core::Document::parse(*rawRow);
    require(associated&&associated->replaceValue("is_public","i1e")&&associated->replaceValue("rand","i-25e")&&
        associated->replaceValue("assoc","ld1:h20:"+previousHash+"1:wi1eed1:h20:"+latestHash+"1:wi2eee"),
        "Bind standard Java association fields to the original signed source");
    const auto publish=[&](Network lane) {
      auto access=lease->sourceAccess();
      require(access.has_value(),"Acquire the current association permit");
      return search::savedSubscriptionAssociationPublications(*lease,*access,field(fixture,"hash"),lane,1000);
    };
    const auto save=[&] { write(root/"subscriptions.config","d4:subsl"+associated->serialize()+"ee"); };
    save();
    const auto records=publish(route), wrongRoute=publish(other);
    require(records&&records->size()==2&&wrongRoute&&wrongRoute->empty(),"Keep association publications on the signed route");
    const auto newest=core::TorrentHash::parse(latestHash);
    const auto oldest=core::TorrentHash::parse(previousHash);
    require(newest&&oldest,"Valid Java association torrent identities");
    const auto newestKey=search::subscriptionAssociationDhtKey(*newest);
    const auto oldestKey=search::subscriptionAssociationDhtKey(*oldest);
    require(newestKey&&oldestKey&&records->at(0).dhtKey==*newestKey&&records->at(1).dhtKey==*oldestKey,
        "Prefer the most recent eligible association");
    require(records->at(0).applicationKey=="subscription:assoc:"+newest->hex()&&
        records->at(1).applicationKey=="subscription:assoc:"+oldest->hex(),"Keep both original unhashed keys for the relay");
    const auto& wire=records->front().value;
    const auto hint=search::decodeSubscriptionAssociationHint({wire.data(),wire.size()});
    require(hint&&(**seed).metainfo.presence&&hint->version==(**seed).metainfo.presence->version&&hint->fixedRandom==0xe7,
        "Association uses the original fixed random byte and the verified version");
    require(QByteArray(hint->shortId.data(),hint->shortId.size()).toHex().toUpper().toStdString()==unversioned.substr(0,20),
        "Association SID derives from the verified subscription key");

    require(associated->replaceValue("is_public","i0e").has_value(),"Withdraw publication for a nonpublic subscription");
    save();
    const auto privateRows=publish(route);
    require(privateRows&&privateRows->empty(),"Private subscription cannot advertise association hints");
    require(associated->replaceValue("is_public","i1e")&&associated->replaceValue("assoc","li0ee"),
        "Generate unusable association metadata without changing the signed body");
    save();
    require(!publish(route),"Malformed associations cannot authorize a partial publication");
  }
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
  {
    const std::string associationHash(20,'\x82');
    auto associated=core::Document::parse(*rawRow);
    require(associated&&associated->eraseValue("private_key").has_value()&&associated->replaceValue("is_public","i1e")&&
        associated->replaceValue("rand","i-25e")&&associated->replaceValue("hupv","i6e")&&
        associated->replaceValue("assoc","ld1:h20:"+associationHash+"1:wi1eee"),"Save a previously prompted version");
    const auto save=[&]{write(root/"subscriptions.config","d4:subsl"+associated->serialize()+"ee");};
    save();
    const auto state=resolve(FileOperation::Verify);
    require(state&&*state&&bool((**state).upgrade)==(route==Network::I2P),"Use the prepared upgrade only on the selected I2P source");
    require((**state).associations.size()==(route==Network::I2P?1:0),"Only I2P owners need held association relay records");
    if (route==Network::I2P) {
      const auto key=search::subscriptionAssociationKey(*core::TorrentHash::parse(associationHash));
      const auto& probe=*(**state).upgrade;
      const auto& publication=(**state).associations.front();
      require(key&&probe.associations==std::vector{key->dht}&&probe.publicKey==field(row,"public_key")&&probe.minimumVersion==6,
          "Upgrade request retains the pinned key, association, and prompted version boundary");
      require(publication.applicationKey==key->application&&publication.dhtKey==key->dht,
          "The read and write routes use the same association identity");
    }
    require(associated->replaceValue("private_key","4:mine").has_value(),"Capture the original owned-subscription marker");
    save();
    const auto self=resolve(FileOperation::Verify);
    require(self&&*self&&!(**self).upgrade&&readVersioned()==body,"Own source continues seeding without an upgrade request");
    require((**self).associations==(**state).associations,"Owning the signing key leaves association publication enabled");
    require(associated->replaceValue("assoc","li0ee").has_value(),"Generate malformed association data");
    save();
    const auto malformed=resolve(FileOperation::Verify);
    require(malformed&&*malformed&&(**malformed).associations.empty()&&readVersioned()==body,
        "Malformed associations leave the independent signed version available");
    write(root/"subscriptions.config",field(fixture,"config"));
  }
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

void checkUpgradeJournal(const std::filesystem::path& fixtureDirectory) {
  const auto read=[](const auto& path) {
    std::ifstream file(path,std::ios::binary);
    require(bool(file),"Read owned upgrade file");
    return std::string(std::istreambuf_iterator<char>(file),{});
  };
  const auto fixture=core::Document::parse(read(fixtureDirectory/"subscription-upgrade-reference.dat"));
  require(fixture&&fixture->find("v4")&&fixture->find("v5"),"Two signed versions of the same I2P subscription");
  const auto& before=*fixture->find("v4");
  const auto& after=*fixture->find("v5");
  const auto oldBody=std::string(field(before,"definition"));
  const auto newBody=std::string(field(after,"definition"));
  const auto oldConfig=std::string(field(fixture->root(),"configuration"));
  const auto oldSeed=search::makeSubscriptionSeed(*before.find("row"),oldBody,1000);
  const auto newSeed=search::makeSubscriptionSeed(*after.find("row"),newBody,1000);
  require(oldSeed&&oldSeed->presence&&newSeed&&newSeed->presence,"Verified original subscription definitions");
  const search::I2pSubscriptionUpgrade candidate{oldSeed->publicationHash,std::string(field(*after.find("row"),"public_key")),
      *newSeed->presence,newSeed->metainfo};
  const auto goal=search::planI2pSubscriptionUpgrade(oldConfig,oldBody,newBody,candidate);
  require(goal&&goal->definition==newBody,"Project the signed update before publication");
  const auto declined=search::planI2pSubscriptionUpgrade(oldConfig,oldBody,std::nullopt,candidate);
  require(declined&&declined->definition==oldBody,"Decline only records the offered version");
  const auto oldCatalogue=core::Document::parse(oldConfig);
  const auto newCatalogue=core::Document::parse(goal->configuration);
  require(oldCatalogue&&newCatalogue&&oldCatalogue->find("subs")&&newCatalogue->find("subs"),"Read unchanged opaque values");
  const auto& oldRows=oldCatalogue->find("subs")->children;
  const auto& newRows=newCatalogue->find("subs")->children;
  require(oldRows.size()==2&&newRows.size()==2&&core::encodeValue(oldRows[1]).value()==core::encodeValue(newRows[1]).value(),
      "Upgrade leaves the other row byte-exact");
  for (const auto* key:{"assoc","history","rand","subscribed","fixture_future"}) {
    const auto* original=oldRows[0].find(key);
    const auto* saved=newRows[0].find(key);
    require(original&&saved&&core::encodeValue(*original).value()==core::encodeValue(*saved).value(),"Keep local subscription fields");
  }
  require(newRows[0].find("version")->integer==5&&newRows[0].find("hupv")->integer==5,"Accept and remember the exact newer version");

  const auto encoded=search::encodeSubscriptionPresence(candidate.presence,0);
  require(encoded.has_value(),"Build a recoverable signed source record");
  const auto bencoded=[](std::string_view bytes){return std::to_string(bytes.size())+':'+std::string(bytes);};
  const std::map<std::string,std::string> fields{{"schema",bencoded("BiglyBTC.Subscription.Upgrade.V1")},
      {"configuration",bencoded(oldConfig)},{"old_definition",bencoded(oldBody)},{"new_definition",bencoded(newBody)},
      {"configuration_backup","i0e"},{"definition_backup","i0e"},{"public_key",bencoded(candidate.publicKey)},
      {"previous",bencoded({candidate.previous.data(),candidate.previous.size()})},{"version","i5e"},{"publication",bencoded(*encoded)}};
  const auto journalFor=[&](bool declining) {
    std::string journal="d";
    for (const auto& [key,value]:fields) { journal+=bencoded(key)+(declining&&key=="new_definition"?"i0e":value); }
    return journal+'e';
  };

  enum class Point {Fresh,PartialIntent,Intent,DefinitionRenamed,DefinitionWritten,ConfigurationRenamed,Diverged,
                    Declined,DeclineInterrupted};
  for (const auto step:{Point::Fresh,Point::PartialIntent,Point::Intent,Point::DefinitionRenamed,
                        Point::DefinitionWritten,Point::ConfigurationRenamed,Point::Diverged,
                        Point::Declined,Point::DeclineInterrupted}) {
    const bool declining=step==Point::Declined||step==Point::DeclineInterrupted;
    const auto journal=journalFor(declining);
    QTemporaryDir temporary;
    require(temporary.isValid(),"Recover in an isolated profile");
    const std::filesystem::path root(temporary.path().toStdU16String());
    std::filesystem::create_directory(root/"subs");
    const auto definition=root/"subs"/goal->definitionName;
    const auto configuration=root/"subscriptions.config";
    const auto marker=root/".subscriptions-native-upgrade";
    const auto staged=root/".subscriptions-native-upgrade.saving";
    const auto definitionBackup=root/"subs"/(goal->definitionName+".bak");
    const auto configurationBackup=root/"subscriptions.config.bak";
    const auto opaque=root/"subs"/"retained.results";
    write(definition,oldBody);
    write(configuration,oldConfig);
    write(opaque,"d7:resultslee");
    if (step==Point::Fresh) {
      write(definitionBackup,"Previous definition backup");
      write(configurationBackup,"Previous configuration backup");
    }
    auto lease=storage::ProfileLease::acquire(root);
    require(lease.has_value(),"Own the generated upgrade profile");
    if (step==Point::Fresh) {
      auto wrong=newBody;
      wrong.back()='x';
      require(!search::installI2pSubscriptionUpgrade(*lease,candidate,wrong),"Reject corrupted definition before creating an intent");
      require(!std::filesystem::exists(marker)&&read(definition)==oldBody&&read(configuration)==oldConfig,"Invalid upgrade preserves both files");
      require(search::installI2pSubscriptionUpgrade(*lease,candidate,newBody).has_value(),"Publish approved signed upgrade");
      require(!search::installI2pSubscriptionUpgrade(*lease,candidate,newBody),"Reject replay against the changed source");
    } else if (step==Point::Declined) {
      require(search::rememberI2pSubscriptionUpgrade(*lease,candidate).has_value(),"Remember a declined signed update");
      require(search::rememberI2pSubscriptionUpgrade(*lease,candidate).has_value(),"Repeated decline is harmless");
    } else {
      if (step==Point::PartialIntent) { write(staged,"d"); }
      else { write(marker,journal); }
      if (step==Point::DefinitionRenamed||step==Point::DefinitionWritten||step==Point::ConfigurationRenamed) {
        std::filesystem::rename(definition,definitionBackup);
        if (step==Point::DefinitionRenamed) { write(root/"subs"/(goal->definitionName+".saving"),newBody); }
        else { write(definition,newBody); }
      }
      if (step==Point::ConfigurationRenamed||step==Point::DeclineInterrupted) {
        std::filesystem::rename(configuration,configurationBackup);
        write(root/"subscriptions.config.saving",declining?declined->configuration:goal->configuration);
      }
      if (step==Point::Diverged) {
        write(configuration,"de");
        require(!search::recoverI2pSubscriptionUpgrade(*lease)&&read(marker)==journal&&read(definition)==oldBody&&
            read(configuration)=="de","Changed external files prevent any recovery write");
        continue;
      }
      require(!search::savedSubscriptionSeedCandidates(*lease,Network::I2P),"Pending journal fences signed seed readers");
      require(search::recoverI2pSubscriptionUpgrade(*lease).has_value(),"Recover an interrupted upgrade");
    }
    require(search::recoverI2pSubscriptionUpgrade(*lease).has_value()&&!std::filesystem::exists(marker)&&!std::filesystem::exists(staged),
        "Completed recovery is idempotent");
    if (step==Point::PartialIntent) {
      require(read(definition)==oldBody&&read(configuration)==oldConfig,"Incomplete intent leaves the original pair intact");
    } else if (declining) {
      require(read(definition)==oldBody&&read(configuration)==declined->configuration&&read(configurationBackup)==oldConfig&&
          !std::filesystem::exists(definitionBackup),"Decline retains the original version and definition after reopening");
    } else {
      require(read(definition)==newBody&&read(configuration)==goal->configuration&&read(configurationBackup)==oldConfig&&
          read(definitionBackup)==oldBody,"Reopen with matching signed bytes and original backups");
      const auto agreed=search::readSavedSubscriptionSeed(*lease,{newSeed->publicationHash.data(),newSeed->publicationHash.size()},
          search::SubscriptionSeedKey::Publication,Network::I2P,1000);
      require(agreed&&*agreed&&(**agreed).payload==newBody,"New version enters the normal signed-source reader");
    }
    require(read(opaque)=="d7:resultslee","Saved result bytes survive the entire upgrade");
  }
}
} // namespace

int main(int argc,char** argv) try {
  if(argc!=2) { return 2; }
  checkSubscriptionAssociationHints();
  std::ifstream file(argv[1],std::ios::binary);
  require(bool(file),"Java fixture input");
  const auto fixture=core::Document::parse({std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()});
  require(fixture&&fixture->find("new")&&fixture->find("old")&&fixture->find("i2p"),"Public and I2P Java fixtures");
  checkFixture(*fixture->find("new"),Network::Public);
  checkFixture(*fixture->find("old"),Network::Public);
  checkFixture(*fixture->find("i2p"),Network::I2P);
  checkSavedSource(*fixture->find("new"),Network::Public);
  checkSavedSource(*fixture->find("i2p"),Network::I2P);
  checkUpgradeJournal(std::filesystem::path(argv[1]).parent_path());
  std::cout<<"Java subscription source routing, metainfo, signatures and DDB encryption passed\n";
  return 0;
} catch(const std::exception& error) {
  std::cerr<<error.what()<<'\n';
  return 1;
}
