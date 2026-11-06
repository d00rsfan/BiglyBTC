// Included inside intake's private namespace. Fixed typed values, not authority.
std::string fixedPreparationBytes(const FixedSubscriptionDownloadPreparation& value){
  require(value.rawShortId.size()==10,ErrorCode::InvalidFormat,"Subscription preparation needs actual raw10 SID");std::map<std::string,std::string> fields{{"newAssociation",i(value.newAssociation?1:0)},{"sid",b(value.rawShortId)}};
  if(value.rawCounts.seeds){fields["seeds"]=i(*value.rawCounts.seeds);}if(value.rawCounts.peers){fields["peers"]=i(*value.rawCounts.peers);}std::string out="d";for(const auto& [key,encoded]:fields){out+=b(key)+encoded;}return out+'e';
}
FixedSubscriptionDownloadPreparation fixedPreparationValue(const Node& node){
  require(node.type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Malformed fixed subscription recipe");uniqueKeys(node);const auto* sid=node.find("sid"),*changed=node.find("newAssociation"),*seeds=node.find("seeds"),*peers=node.find("peers");
  require(sid&&sid->type==Node::Type::Bytes&&sid->bytes.size()==10&&changed&&changed->type==Node::Type::Integer&&(changed->integer==0||changed->integer==1)&&node.children.size()==4U+(seeds?2U:0U)+(peers?2U:0U),ErrorCode::InvalidFormat,"Unknown fixed subscription recipe field");
  FixedSubscriptionDownloadPreparation out;out.rawShortId=std::string(sid->bytes);out.newAssociation=changed->integer==1;if(seeds){require(seeds->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid fixed subscription seeds");out.rawCounts.seeds=seeds->integer;}if(peers){require(peers->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid fixed subscription peers");out.rawCounts.peers=peers->integer;}return out;
}
std::string fixedFreshRecipe(std::string_view journal,const FixedSubscriptionDownloadPreparation& value){
  return "d"+b("journal")+b(journal)+b("preparation")+fixedPreparationBytes(value)+b("schema")+b("BiglyBTC.StoppedPublicSubscriptionIntake.V2")+'e';
}
struct SubscriptionStateImages {std::string base,prepared,associated;};
std::string subscriptionStateAttributes(std::string_view bytes){
  auto envelope=EncodedDocument::parse(std::string(bytes),compatibleConfigLimits());require(envelope.has_value(),ErrorCode::InvalidFormat,"Cannot decode subscription state");
  uniqueKeys(envelope->document().root());const auto* node=envelope->document().find("attributes");require(!node||node->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Subscription state attributes are malformed");return node?raw(envelope->document(),*node):std::string("de");
}
std::string withSubscriptionStateAttributes(const std::string& before,const std::string& attributes){
  if(subscriptionStateAttributes(before)==attributes){return before;}
  auto envelope=EncodedDocument::parse(before,compatibleConfigLimits());require(envelope.has_value(),ErrorCode::InvalidFormat,"Cannot decode subscription state");const auto original=envelope->document();
  require(envelope->replaceValue("attributes",attributes).has_value(),ErrorCode::InvalidFormat,"Subscription state edit exceeds bound");const auto encoded=envelope->serialize();require(encoded.has_value(),ErrorCode::InvalidFormat,"Cannot encode subscription state");
  auto checked=EncodedDocument::parse(*encoded,compatibleConfigLimits());require(checked.has_value(),ErrorCode::InvalidFormat,"Cannot reparse subscription state");
  require(take(originalTorrentInfoSubject(original))==take(originalTorrentInfoSubject(checked->document())),ErrorCode::Conflict,"Subscription edit changed raw torrent info");
  for(std::size_t n=0;n<original.root().children.size();n+=2){const auto& key=original.root().children[n];if(key.bytes=="attributes"){continue;}const auto* next=checked->document().find(key.bytes);require(next&&raw(original,original.root().children[n+1])==raw(checked->document(),*next),ErrorCode::Conflict,"Subscription edit changed unrelated state");}
  return *encoded;
}
std::string subscriptionAttributesValue(std::string attributes,const FixedSubscriptionDownloadPreparation& preparation,bool callback,search::detail::SubscriptionRawScrapeCounts counts){
  require(preparation.rawShortId.size()==10,ErrorCode::InvalidFormat,"Subscription preparation needs actual raw10 SID");
  auto value=search::detail::planSubscriptionDownloadValues(std::move(attributes),preparation.rawShortId,callback,counts);
  if(!value){const auto code=value.error()==search::detail::SubscriptionDownloadValueError::Memory?ErrorCode::Io:value.error()==search::detail::SubscriptionDownloadValueError::Limit?ErrorCode::Unsupported:ErrorCode::InvalidFormat;fail(code,"Subscription download preparation refused");}
  return std::move(value->attributes);
}
SubscriptionStateImages fixedSubscriptionStateImages(std::string base,const FixedSubscriptionDownloadPreparation& preparation,bool cacheAlreadyPrepared){
  const auto original=subscriptionStateAttributes(base);const auto cached=cacheAlreadyPrepared?original:subscriptionAttributesValue(original,preparation,false,preparation.rawCounts);
  auto prepared=withSubscriptionStateAttributes(base,cached);const auto callback=subscriptionAttributesValue(cached,preparation,preparation.newAssociation,{});auto associated=withSubscriptionStateAttributes(prepared,callback);
  return {std::move(base),std::move(prepared),std::move(associated)};
}
struct FixedStateObservation {File primary;std::optional<File> saving;std::uint8_t primaryMatches=0;std::optional<std::uint8_t> savingMatches;};
std::uint8_t fixedStateMatches(std::string_view bytes,const std::array<std::string_view,3>& images){
  std::uint8_t matches=0;for(unsigned n=0;n<images.size();++n){if(bytes==images[n]){matches=static_cast<std::uint8_t>(matches|(1U<<n));}}require(matches!=0,ErrorCode::Conflict,"Subscription state is not an owned fixed image");return matches;
}
FixedStateObservation observeFixedSubscriptionState(int active,const std::string& name,const std::optional<File>& backup,const std::array<std::string_view,3>& images){
  absent(active,name+".bak.saving");absent(active,name+".saving.bak");absent(active,name+".import");
  const auto actualBackup=read(active,name+".bak",MaximumInventory);require(bool(actualBackup)==bool(backup)&&(!backup||(actualBackup->bytes==backup->bytes&&unchanged(actualBackup->info,backup->info))),ErrorCode::Conflict,"Frozen subscription state backup changed");
  auto current=read(active,name,MaximumInventory);require(current.has_value(),ErrorCode::Conflict,"Subscription state primary is absent");auto stage=read(active,name+".saving",MaximumInventory);
  const auto index=fixedStateMatches(current->bytes,images);std::optional<std::uint8_t> staged;if(stage){staged=fixedStateMatches(stage->bytes,images);}return {std::move(*current),std::move(stage),index,staged};
}
void retireFixedSubscriptionStage(int active,const std::string& name,const File& expected){
  const auto now=read(active,name,MaximumInventory);require(now&&now->bytes==expected.bytes&&unchanged(now->info,expected.info),ErrorCode::Conflict,"Owned subscription state stage changed");
  require(::unlinkat(active,name.c_str(),0)==0,ErrorCode::Io,"Cannot retire owned subscription state stage");sync(active);
}
void publishFixedSubscriptionState(const ProfileLease& lease,int active,const std::string& name,const std::optional<File>& backup,const std::array<std::string_view,3>& images,unsigned desired,std::string_view owner,void(*observer)(SubscriptionDownloadStateStage)=nullptr){
  require(desired<images.size()&&!owner.empty(),ErrorCode::Conflict,"Missing fixed subscription state recipe or owner");acquisitionOwnerCurrent(lease,owner);
  const auto before=observeFixedSubscriptionState(active,name,backup,images);const auto savingName=name+".saving";auto stage=before.saving;
  if(before.primary.bytes==images[desired]){if(stage){acquisitionOwnerCurrent(lease,owner);retireFixedSubscriptionStage(active,savingName,*stage);}return;}
  if(stage&&stage->bytes!=images[desired]){acquisitionOwnerCurrent(lease,owner);retireFixedSubscriptionStage(active,savingName,*stage);stage.reset();}
  if(!stage){acquisitionOwnerCurrent(lease,owner);auto made=take(detail::writeExclusiveOwned(active,savingName,images[desired],static_cast<mode_t>(before.primary.info.st_mode&0777)));struct stat madeInfo{};require(::fstat(made.get(),&madeInfo)==0,ErrorCode::Io,"Cannot inspect new fixed state stage");if(madeInfo.st_gid!=before.primary.info.st_gid){require(::fchown(made.get(),static_cast<uid_t>(-1),before.primary.info.st_gid)==0,ErrorCode::Io,"Cannot retain fixed state group");}require(::fchmod(made.get(),static_cast<mode_t>(before.primary.info.st_mode&0777))==0,ErrorCode::Io,"Cannot retain fixed state permissions");sync(made.get());struct stat held{};
    stage=read(active,savingName,MaximumInventory);require(stage&&::fstat(made.get(),&held)==0&&unchanged(held,stage->info)&&stage->bytes==images[desired],ErrorCode::Conflict,"Created subscription state stage changed");}
  Descriptor held(::openat(active,savingName.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));struct stat heldInfo{};
  require(held.get()>=0&&::fstat(held.get(),&heldInfo)==0&&unchanged(heldInfo,stage->info),ErrorCode::Conflict,"Subscription state stage identity changed");
  if(heldInfo.st_gid!=before.primary.info.st_gid||(heldInfo.st_mode&0777)!=(before.primary.info.st_mode&0777)){
    if(heldInfo.st_gid!=before.primary.info.st_gid){require(::fchown(held.get(),static_cast<uid_t>(-1),before.primary.info.st_gid)==0,ErrorCode::Io,"Cannot retain resumed state group");}
    require(::fchmod(held.get(),static_cast<mode_t>(before.primary.info.st_mode&0777))==0,ErrorCode::Io,"Cannot retain resumed state permissions");stage=read(active,savingName,MaximumInventory);
    require(stage&&::fstat(held.get(),&heldInfo)==0&&unchanged(heldInfo,stage->info)&&stage->bytes==images[desired],ErrorCode::Conflict,"Resumed state stage changed");
  }
  sync(held.get());sync(active);
  if(observer){observer(SubscriptionDownloadStateStage::SavingSynced);}
  const auto ready=observeFixedSubscriptionState(active,name,backup,images);require(ready.primary.bytes==before.primary.bytes&&unchanged(ready.primary.info,before.primary.info)&&ready.saving&&ready.saving->bytes==stage->bytes&&unchanged(ready.saving->info,stage->info),ErrorCode::Conflict,"Subscription state CAS preimage changed");
  acquisitionOwnerCurrent(lease,owner);require(::renameat(active,savingName.c_str(),active,name.c_str())==0,ErrorCode::Io,"Subscription state atomic publication failed");sync(active);
  const auto after=observeFixedSubscriptionState(active,name,backup,images);require(after.primary.bytes==images[desired]&&!after.saving,ErrorCode::Conflict,"Subscription state postimage is not verified");acquisitionOwnerCurrent(lease,owner);
}
