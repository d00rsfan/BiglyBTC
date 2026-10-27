// Included by original_storage_defaults.cpp inside biglybtc::storage.
std::expected<OriginalFilenameContext,Error> readOriginalFilenameContext(const ProfileLease& lease){
  return guarded([&]() -> std::expected<OriginalFilenameContext,Error> {
    require(lease.valid(),ErrorCode::UnsafePath,"Filename context requires a held profile lease");
    auto config=ConfigFile::load(lease,"biglybt.config");
    if(!config){require(config.error().code==ErrorCode::Missing,config.error().code,"Cannot read original filename context");
      for(const auto* name:{"biglybt.config","biglybt.config.bak","biglybt.config.saving"}){struct stat candidate{};require(::fstatat(lease.directoryHandle(),name,&candidate,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT,ErrorCode::InvalidFormat,"Present original filename settings require recovery");}
      return OriginalFilenameContext{std::string(compat::OriginalDefaultFilenameConversions),false};
    }
    require(config->recoverySource()!=RecoverySource::Saving,ErrorCode::Conflict,"Original filename context needs recovery");
    const core::Node* conversions=nullptr;const core::Node* showAll=nullptr;const auto& root=config->document().root();
    require(root.type==Node::Type::Dictionary&&root.children.size()%2==0,ErrorCode::InvalidFormat,"Invalid original filename settings dictionary");
    for(std::size_t n=0;n<root.children.size();n+=2){if(root.children[n].bytes=="File.Character.Conversions"){require(!conversions,ErrorCode::InvalidFormat,"Duplicate original filename conversion policy");conversions=&root.children[n+1];}
      else if(root.children[n].bytes=="File.Decoder.ShowAll"){require(!showAll,ErrorCode::InvalidFormat,"Duplicate original decoder registration setting");showAll=&root.children[n+1];}}
    require(!conversions||(conversions->type==Node::Type::Bytes&&conversions->bytes.size()<=16384),ErrorCode::InvalidFormat,"Invalid bounded original filename conversion string");
    require(!showAll||showAll->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid original decoder registration setting");
    OriginalFilenameContext result{conversions?std::string(conversions->bytes):std::string(compat::OriginalDefaultFilenameConversions),showAll&&static_cast<std::uint32_t>(showAll->integer)!=0};
    const auto checked=compat::originalFilenameComponent("safe",result.conversions);require(checked.has_value(),ErrorCode::Unsupported,"Unsupported original filename conversion policy");return result;
  });
}
std::expected<std::string,Error> readOriginalFilenameConversions(const ProfileLease& lease){const auto context=readOriginalFilenameContext(lease);if(!context){return std::unexpected(context.error());}return context->conversions;}
std::expected<bool,Error> readOriginalDecoderShowAll(const ProfileLease& lease){const auto context=readOriginalFilenameContext(lease);if(!context){return std::unexpected(context.error());}return context->decoderShowAll;}
std::expected<compat::OriginalLayout,Error> readOriginalLayout(const ProfileLease& lease,const core::Document& document,std::size_t maximumFiles){
  const auto context=readOriginalFilenameContext(lease);if(!context){return std::unexpected(context.error());}
  const auto layout=compat::originalLayout(document,maximumFiles,context->conversions,context->decoderShowAll);if(!layout){return std::unexpected(Error{ErrorCode::Unsupported,layout.error().message});}return *layout;
}
