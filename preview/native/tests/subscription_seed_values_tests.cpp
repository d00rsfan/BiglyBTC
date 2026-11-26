// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/bencode.hpp"
#include "subscription_seed_values.hpp"

#include <algorithm>
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
} // namespace

int main(int argc,char** argv) try {
  if(argc!=2) { return 2; }
  std::ifstream file(argv[1],std::ios::binary);
  require(bool(file),"Java fixture input");
  const auto fixture=core::Document::parse({std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()});
  require(fixture&&fixture->find("new")&&fixture->find("old"),"Current and legacy Java fixtures");
  checkFixture(*fixture->find("new"));
  checkFixture(*fixture->find("old"));
  std::cout<<"Java subscription seed metainfo, signatures and DDB encryption passed\n";
  return 0;
} catch(const std::exception& error) {
  std::cerr<<error.what()<<'\n';
  return 1;
}
