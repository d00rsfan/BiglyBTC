// Included inside the existing serialized constraint worker. Values only;
// actual SDK queries are done by Host AFTER step releases its source gate.
  std::optional<core::tags::peer_sets::SavedPeerSets> peerSets;
  QByteArray peerDefinitionsDigest;
  std::uint64_t peerDefinitionsRevision=0;
  void loadPeerDefinitions(std::uint64_t revision){
    if(peerDefinitionsRevision==revision){return;}peerSets.reset();peerDefinitionsDigest.clear();peerDefinitionsRevision=revision;
    auto config=storage::ConfigFile::load(*lease,"biglybt.config");
    if(!config||config->recoverySource()!=storage::RecoverySource::Primary){return;}
    auto parsed=core::tags::peer_sets::SavedPeerSets::read(config->document());if(!parsed){return;}
    const auto& raw=config->document().originalBytes();peerDefinitionsDigest=QCryptographicHash::hash(QByteArray::fromRawData(raw.data(),qsizetype(raw.size())),QCryptographicHash::Sha256);peerSets=std::move(*parsed);
  }
  std::optional<core::tags::runtime::Text> peerName(const std::string& name)const{
    auto value=QString::fromUtf8(name.data(),qsizetype(name.size())).toStdU16String();
    if(!value.starts_with(u"tag.")){return value;}if(!stockTagLocale){return {};}
    if(auto stock=core::tags::stockTagName(*stockTagLocale,value)){return core::tags::runtime::Text(*stock);}return {};
  }
  ConstraintPeerSetSource peerSource(const ConstraintFactStamp& stamp,const ConstraintWorkerFrame& frame,bool& observed)const{
    namespace runtime=core::tags::runtime;ConstraintPeerSetSource source;source.stamp=stamp;source.type.id=4;
    const ConstraintPeerObservation* receipt=nullptr;const auto now=std::chrono::steady_clock::now();
    for(const auto& candidate:frame.peerObservations){
      if(candidate.request.stamp.binding!=stamp.binding||candidate.request.stamp.sequence>=stamp.sequence||candidate.request.sourceRevision!=frame.sourceRevision||candidate.request.definitionsDigest!=peerDefinitionsDigest||candidate.sampled>now||now-candidate.sampled>std::chrono::seconds(3)){continue;}
      if(receipt){receipt=nullptr;break;}receipt=&candidate;
    }
    observed=receipt!=nullptr;
    const auto* endpoints=receipt&&receipt->value?&*receipt->value:nullptr;
    if(endpoints&&(!frame.peerEngineIdentity||endpoints->identity!=*frame.peerEngineIdentity||!endpoints->identity.applicationRun||!endpoints->identity.engineEpoch||endpoints->identity.profileSession!=session||endpoints->hash.hex()!=stamp.binding.target.hash.toStdString()||endpoints->admission!=stamp.binding.providerAdmission||endpoints->endpoints.size()>512)){endpoints=nullptr;}
    if(!peerSets){return source;}
    for(const auto& set:peerSets->sets()){
      if(!set.id){continue;}runtime::Tag tag;tag.id=*set.id;tag.taggableTypes=4;tag.name=peerName(set.name);tag.membership=runtime::Membership::Absent;
      if(set.group){tag.group=QString::fromUtf8(set.group->data(),qsizetype(set.group->size())).toStdU16String();}
      if(endpoints&&tag.name&&set.predicate){for(const auto& address:endpoints->endpoints){if(set.predicate->match(address)==runtime::Truth::Yes){source.present.push_back(*tag.name);break;}}}
      source.type.tags.push_back(std::move(tag));
    }
    return source;
  }
  bool needsPeerEndpoints(const c::Expression& expression)const{
    if(!peerSets){return false;}
    for(const auto& set:peerSets->sets()){if(!set.id||!set.predicate){continue;}const auto name=peerName(set.name);if(!name){continue;}for(const auto& dependency:expression.hasTagNames()){if(dependency==*name){return true;}}}
    return false;
  }
  bool peerDefinitionsUnchanged()const{
    if(peerDefinitionsDigest.isEmpty()){return true;}auto config=storage::ConfigFile::load(*lease,"biglybt.config");if(!config||config->recoverySource()!=storage::RecoverySource::Primary){return false;}
    const auto& raw=config->document().originalBytes();return peerDefinitionsDigest==QCryptographicHash::hash(QByteArray::fromRawData(raw.data(),qsizetype(raw.size())),QCryptographicHash::Sha256);
  }
