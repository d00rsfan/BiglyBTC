// SPDX-License-Identifier: GPL-2.0-or-later
constexpr std::string_view PendingSubscriptionUpgrade=".subscriptions-native-upgrade";
constexpr std::size_t UpgradeJournalBytes=16*1024*1024;

struct UpgradeIntent {
  std::string oldConfiguration,oldDefinition,newDefinition;
  std::optional<std::string> configurationBackup,definitionBackup;
  search::I2pSubscriptionUpgrade update;
};

std::string serializeUpgrade(const UpgradeIntent& intent,std::string_view signedWire) {
  const auto backup=[](const auto& value){return value?bString(*value):"i0e";};
  return bDictionary({{"schema",bString("BiglyBTC.Subscription.Upgrade.V1")},
      {"configuration",bString(intent.oldConfiguration)},{"old_definition",bString(intent.oldDefinition)},
      {"new_definition",bString(intent.newDefinition)},{"configuration_backup",backup(intent.configurationBackup)},
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
  const auto configuration=read("configuration"),oldBody=read("old_definition"),newBody=read("new_definition");
  const auto* version=document->find("version");
  if (schema!="BiglyBTC.Subscription.Upgrade.V1"||!previous||previous->size()!=20||!key||!wire||
      !configuration||!oldBody||!newBody||!version||version->type!=core::Node::Type::Integer||
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
  intent.newDefinition=*newBody;
  std::copy_n(previous->begin(),intent.update.previous.size(),intent.update.previous.begin());
  intent.update.publicKey=*key;
  intent.update.presence=std::move(*publication);
  return intent;
}

std::optional<std::string> fileDigest(const File& file) {
  return file.present?std::optional{sha256(file.bytes)}:std::nullopt;
}

std::expected<void,E> checkUpgradeFile(StateHandle directory,const std::string& leaf,std::string_view before,
    std::string_view after,const std::optional<std::string>& initialBackup) {
  const auto file=named(directory,leaf,MetaBytes);
  const auto backup=named(directory,leaf+".bak",MetaBytes);
  const auto saving=named(directory,leaf+".saving",MetaBytes);
  if (!file||!backup||!saving) { return std::unexpected(E::SourceChanged); }
  if ((saving->present&&saving->bytes!=after)||
      (fileDigest(*backup)!=initialBackup&&(!backup->present||backup->bytes!=before))) {
    return std::unexpected(E::SourceChanged);
  }
  const bool renamed=!file->present&&backup->present&&backup->bytes==before&&saving->present;
  if (!renamed&&(!file->present||(file->bytes!=before&&file->bytes!=after))) {
    return std::unexpected(E::SourceChanged);
  }
  if (!stillNamed(directory,*file)||!stillNamed(directory,*backup)||!stillNamed(directory,*saving)) {
    return std::unexpected(E::SourceChanged);
  }
  return {};
}

std::expected<void,E> recoverUpgrade(const storage::ProfileLease& lease,const UpgradeIntent& intent,File& journal) {
  const auto values=search::planI2pSubscriptionUpgrade(intent.oldConfiguration,intent.oldDefinition,intent.newDefinition,intent.update);
  if (!values) { return std::unexpected(E::Malformed); }
  StateStamp stamp;
  auto subs=subscriptionDirectory(lease,stamp);
  if (!subs) { return std::unexpected(subs.error()); }
  const auto root=lease.directoryHandle();
  const auto definition=checkUpgradeFile(subs->get(),values->definitionName,intent.oldDefinition,values->definition,
      intent.definitionBackup);
  const auto configuration=checkUpgradeFile(root,"subscriptions.config",intent.oldConfiguration,values->configuration,
      intent.configurationBackup);
  if (!definition||!configuration||!stillNamed(root,journal)) { return std::unexpected(E::SourceChanged); }
  const auto observed=state::stateStampAt(root,"subs");
  if (!observed||!*observed||!sameDirectory(**observed,stamp)) { return std::unexpected(E::SourceChanged); }
  if (!writeAt(subs->get(),values->definitionName,std::optional{values->definition})||
      !writeAt(root,"subscriptions.config",std::optional{values->configuration})) {
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
