// Included by original_storage_defaults.cpp inside biglybtc::storage.
std::expected<std::string,Error> readOriginalFilenameConversions(const ProfileLease& lease){
  return guarded([&]() -> std::expected<std::string,Error> {
    require(lease.valid(),ErrorCode::UnsafePath,"Filename policy requires a held profile lease");
    auto config=ConfigFile::load(lease,"biglybt.config");
    if(!config){require(config.error().code==ErrorCode::Missing,config.error().code,"Cannot read original filename policy");
      for(const auto* name:{"biglybt.config","biglybt.config.bak","biglybt.config.saving"}){struct stat candidate{};require(::fstatat(lease.directoryHandle(),name,&candidate,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT,ErrorCode::InvalidFormat,"Present original filename settings require recovery");}
      return std::string(compat::OriginalDefaultFilenameConversions);
    }
    require(config->recoverySource()!=RecoverySource::Saving,ErrorCode::Conflict,"Original filename policy needs recovery");
    const core::Node* found=nullptr;const auto& root=config->document().root();
    require(root.type==Node::Type::Dictionary&&root.children.size()%2==0,ErrorCode::InvalidFormat,"Invalid original filename settings dictionary");
    for(std::size_t n=0;n<root.children.size();n+=2){if(root.children[n].bytes=="File.Character.Conversions"){require(!found,ErrorCode::InvalidFormat,"Duplicate original filename conversion policy");found=&root.children[n+1];}}
    require(!found||(found->type==Node::Type::Bytes&&found->bytes.size()<=16384),ErrorCode::InvalidFormat,"Invalid bounded original filename conversion string");
    const std::string result=found?std::string(found->bytes):std::string(compat::OriginalDefaultFilenameConversions);
    const auto checked=compat::originalFilenameComponent("safe",result);require(checked.has_value(),ErrorCode::Unsupported,"Unsupported original filename conversion policy");return result;
  });
}
std::expected<compat::OriginalLayout,Error> readOriginalLayout(const ProfileLease& lease,const core::Document& document,std::size_t maximumFiles){
  const auto policy=readOriginalFilenameConversions(lease);if(!policy){return std::unexpected(policy.error());}
  const auto layout=compat::originalLayout(document,maximumFiles,*policy);if(!layout){return std::unexpected(Error{ErrorCode::Unsupported,layout.error().message});}return *layout;
}
