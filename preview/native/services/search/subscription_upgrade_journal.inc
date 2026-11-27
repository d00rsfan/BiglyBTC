// SPDX-License-Identifier: GPL-2.0-or-later
constexpr std::string_view PendingSubscriptionUpgrade=".subscriptions-native-upgrade";
constexpr std::size_t UpgradeJournalBytes=16*1024*1024;

struct UpgradeIntent {
  std::string oldConfiguration,oldDefinition;
  std::optional<std::string> newDefinition;
  std::optional<std::string> configurationBackup,definitionBackup;
  search::I2pSubscriptionUpgrade update;
};

std::string serializeUpgrade(const UpgradeIntent& intent,std::string_view signedWire) {
  const auto backup=[](const auto& value){return value?bString(*value):"i0e";};
  return bDictionary({{"schema",bString("BiglyBTC.Subscription.Upgrade.V1")},
      {"configuration",bString(intent.oldConfiguration)},{"old_definition",bString(intent.oldDefinition)},
      {"new_definition",backup(intent.newDefinition)},{"configuration_backup",backup(intent.configurationBackup)},
      {"definition_backup",backup(intent.definitionBackup)},{"public_key",bString(intent.update.publicKey)},
      {"previous",bString({intent.update.previous.data(),intent.update.previous.size()})},
      {"version",bInteger(intent.update.presence.version)},{"publication",bString(signedWire)}});
}

std::expected<UpgradeIntent,E> parseUpgrade(std::string payload) {
  auto document=core::Document::parse(std::move(payload),{UpgradeJournalBytes,MetaBytes,64,4});
  if (!document||document->root().type!=core::Node::Type::Dictionary||!unique(document->root())||
      document->root().children.size()!=20) { return std::unexpected(E::Malformed); }
  const auto read=[&](std::string_view name)->std::optional<std::string_view> {
    const auto* value=document->find(name);
    return value&&value->type==core::Node::Type::Bytes?std::optional{value->bytes}:std::nullopt;
  };
  const auto schema=read("schema"),previous=read("previous"),key=read("public_key"),wire=read("publication");
  const auto configuration=read("configuration"),oldBody=read("old_definition");
  const auto* newBody=document->find("new_definition");
  const auto* version=document->find("version");
  if (schema!="BiglyBTC.Subscription.Upgrade.V1"||!previous||previous->size()!=20||!key||!wire||
      !configuration||!oldBody||!newBody||
      (newBody->type!=core::Node::Type::Bytes&&(newBody->type!=core::Node::Type::Integer||newBody->integer!=0))||
      !version||version->type!=core::Node::Type::Integer||
      version->integer<=0||version->integer>0x00ffffff) { return std::unexpected(E::Malformed); }
  auto publication=search::verifySubscriptionPresence(*wire,*key,static_cast<std::uint32_t>(version->integer));
  if (!publication) { return std::unexpected(E::Malformed); }

  UpgradeIntent intent;
  const auto readBackup=[&](std::string_view field,std::optional<std::string>& output) {
    const auto* value=document->find(field);
    if (!value) { return false; }
    if (value->type==core::Node::Type::Bytes&&value->bytes.size()==64&&
        value->bytes.find_first_not_of("0123456789abcdef")==std::string_view::npos) {
      output=value->bytes;
      return true;
    }
    return value->type==core::Node::Type::Integer&&value->integer==0;
  };
  if (!readBackup("configuration_backup",intent.configurationBackup)||!readBackup("definition_backup",intent.definitionBackup)) {
    return std::unexpected(E::Malformed);
  }
  intent.oldConfiguration=*configuration;
  intent.oldDefinition=*oldBody;
  if (newBody->type==core::Node::Type::Bytes) { intent.newDefinition=newBody->bytes; }
  std::copy_n(previous->begin(),intent.update.previous.size(),intent.update.previous.begin());
  intent.update.publicKey=*key;
  intent.update.presence=std::move(*publication);
  return intent;
}

std::optional<std::string> fileDigest(const File& file) {
  return file.present?std::optional{sha256(file.bytes)}:std::nullopt;
}

auto upgradePreimage(std::string_view before,std::string_view after,const std::optional<std::string>& initialBackup) {
  return [before,after,&initialBackup](const File& file,const File& backup,const File& saving) {
    const auto digest=fileDigest(backup);
    if (before==after) { return file.present&&file.bytes==before&&!saving.present&&digest==initialBackup; }
    if ((saving.present&&saving.bytes!=after)||(digest!=initialBackup&&(!backup.present||backup.bytes!=before))) {
      return false;
    }
    return file.present?(file.bytes==before||file.bytes==after):backup.present&&backup.bytes==before&&saving.present;
  };
}

bool checkUpgradeFile(StateHandle directory,const std::string& leaf,
    const std::function<bool(const File&,const File&,const File&)>& matches) {
  const auto file=named(directory,leaf,MetaBytes);
  const auto backup=named(directory,leaf+".bak",MetaBytes);
  const auto saving=named(directory,leaf+".saving",MetaBytes);
  return file&&backup&&saving&&matches(*file,*backup,*saving)&&
      stillNamed(directory,*file)&&stillNamed(directory,*backup)&&stillNamed(directory,*saving);
}

std::expected<void,E> recoverUpgrade(const storage::ProfileLease& lease,const UpgradeIntent& intent,File& journal) {
  const auto values=search::planI2pSubscriptionUpgrade(intent.oldConfiguration,intent.oldDefinition,intent.newDefinition,intent.update);
  if (!values) { return std::unexpected(E::Malformed); }
  StateStamp stamp;
  auto subs=subscriptionDirectory(lease,stamp);
  if (!subs) { return std::unexpected(subs.error()); }
  const auto root=lease.directoryHandle();
  const auto definition=upgradePreimage(intent.oldDefinition,values->definition,intent.definitionBackup);
  const auto configuration=upgradePreimage(intent.oldConfiguration,values->configuration,intent.configurationBackup);
  if (!checkUpgradeFile(subs->get(),values->definitionName,definition)||!checkUpgradeFile(root,"subscriptions.config",configuration)
      ||!stillNamed(root,journal)) { return std::unexpected(E::SourceChanged); }
  const auto observed=state::stateStampAt(root,"subs");
  if (!observed||!*observed||!sameDirectory(**observed,stamp)) { return std::unexpected(E::SourceChanged); }
  if ((intent.newDefinition&&!writeAt(subs->get(),values->definitionName,std::optional{values->definition},definition))||
      !writeAt(root,"subscriptions.config",std::optional{values->configuration},configuration)) {
    return std::unexpected(E::SourceChanged);
  }
  if (!unlinkOwned(root,journal)||!state::syncStateDirectory(root)) { return std::unexpected(E::SourceChanged); }
  return {};
}

bool pendingSubscriptionUpgrade(StateHandle root) {
  for (const auto* suffix:{"",".saving"}) {
    const auto file=state::stateStampAt(root,std::string(PendingSubscriptionUpgrade)+suffix);
    if (!file||*file) { return true; }
  }
  return false;
}

std::expected<File,E> publishUpgradeIntent(StateHandle root,const UpgradeIntent& intent,std::string_view signedWire) {
  const std::string path(PendingSubscriptionUpgrade),staging=path+".saving";
  const auto bytes=serializeUpgrade(intent,signedWire);
  if (bytes.size()>UpgradeJournalBytes) { return std::unexpected(E::Limit); }
  if (!writePrivate(root,staging,bytes)) { return std::unexpected(E::SourceChanged); }
  auto stage=named(root,staging,UpgradeJournalBytes);
  if (!stage||!stage->present||stage->bytes!=bytes||!stillNamed(root,*stage)) { return std::unexpected(E::SourceChanged); }
#ifdef _WIN32
  stage->witness.releaseWitness();
#endif
  if (!state::publishStateFile(root,staging,path,state::InvalidStateHandle,false,&*stage->witness.stamp)||
      !state::syncStateDirectory(root)) { return std::unexpected(E::SourceChanged); }
  auto journal=named(root,path,UpgradeJournalBytes);
  if (!journal||!journal->present||journal->bytes!=bytes) { return std::unexpected(E::SourceChanged); }
  return journal;
}
