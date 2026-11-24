// Included inside the existing serialized constraint worker. Values only;
// actual SDK queries are done by Host AFTER step releases its source gate.
  std::optional<core::tags::peer_sets::SavedPeerSets> peerSets;
  QByteArray peerDefinitionsDigest,peerPolicyDigest;
  std::set<std::int32_t> actionFreePeerSets;
  std::uint64_t peerDefinitionsRevision=0;
  void loadPeerDefinitions(std::uint64_t revision){
    if(peerDefinitionsRevision==revision){return;}peerSets.reset();peerDefinitionsDigest.clear();peerPolicyDigest.clear();
    actionFreePeerSets.clear();peerDefinitionsRevision=revision;
    auto config=storage::ConfigFile::load(*lease,"biglybt.config");
    if(!config||config->recoverySource()!=storage::RecoverySource::Primary){return;}
    auto parsed=core::tags::peer_sets::SavedPeerSets::read(config->document());if(!parsed){return;}
    for(const auto& set:parsed->sets()){
      if(!set.predicate||!set.id){continue;}
      const auto key="speed.limit.handler.ipset_n."+std::to_string(*set.id)+".eos";
      const auto* actions=config->document().find(key);
      if(!actions||(actions->type==core::Node::Type::Integer&&(actions->integer==0||actions->integer==-1))){
        actionFreePeerSets.insert(*set.id);
      }
    }
    const auto& raw=config->document().originalBytes();
    peerDefinitionsDigest=QCryptographicHash::hash(QByteArray::fromRawData(raw.data(),qsizetype(raw.size())),QCryptographicHash::Sha256);
    QCryptographicHash policy(QCryptographicHash::Sha256);
    constexpr std::string_view prefix="speed.limit.handler.ipset_n.";
    const auto& fields=config->document().root().children;
    for(std::size_t index=0;index<fields.size();index+=2){
      const auto& key=fields[index];
      const auto name=key.bytes;
      bool relevant=name=="speed.limit.handler.schedule.lines";
      if(name.starts_with(prefix)){
        const auto tail=name.substr(prefix.size());
        relevant=tail.ends_with(".eos")||(!tail.empty()&&std::ranges::all_of(tail,[](char c){
          return (c>='A'&&c<='Z')||(c>='2'&&c<='7');
        }));
      }
      if(relevant){
        const auto length=fields[index+1].end-key.begin;
        policy.addData(QByteArrayView(raw.data()+key.begin,qsizetype(length)));
      }
    }
    peerPolicyDigest=policy.result();
    peerSets=std::move(*parsed);
  }
  std::optional<core::tags::runtime::Text> peerName(const std::string& name)const{
    auto value=QString::fromUtf8(name.data(),qsizetype(name.size())).toStdU16String();
    if(!value.starts_with(u"tag.")){return value;}if(!stockTagLocale){return {};}
    if(auto stock=core::tags::stockTagName(*stockTagLocale,value)){return core::tags::runtime::Text(*stock);}return {};
  }
  ConstraintPeerSetSource peerSource(const ConstraintFactStamp& stamp,const ConstraintWorkerFrame& frame,
      std::optional<ConstraintPeerRequest::Scope> scope,bool& observed)const{
    namespace runtime=core::tags::runtime;ConstraintPeerSetSource source;source.stamp=stamp;source.type.id=4;
    const ConstraintPeerObservation* receipt=nullptr;const auto now=std::chrono::steady_clock::now();
    for(const auto& candidate:frame.peerObservations){
      if(candidate.request.scope!=scope||candidate.request.stamp.binding!=stamp.binding||candidate.request.stamp.sequence>=stamp.sequence||candidate.request.sourceRevision!=frame.sourceRevision||candidate.request.definitionsDigest!=peerDefinitionsDigest||candidate.sampled>now||now-candidate.sampled>std::chrono::seconds(3)){continue;}
      if(receipt){receipt=nullptr;break;}receipt=&candidate;
    }
    observed=receipt!=nullptr;
    const auto* endpoints=receipt&&scope==ConstraintPeerRequest::Scope::Selected&&receipt->value?&*receipt->value:nullptr;
    if(endpoints&&(!frame.peerEngineIdentity||endpoints->identity!=*frame.peerEngineIdentity||!endpoints->identity.applicationRun||!endpoints->identity.engineEpoch||endpoints->identity.profileSession!=session||endpoints->hash.hex()!=stamp.binding.target.hash.toStdString()||endpoints->admission!=stamp.binding.providerAdmission||endpoints->endpoints.size()>512)){endpoints=nullptr;}
    const auto privateCount=receipt&&scope==ConstraintPeerRequest::Scope::SelectedI2p&&
        receipt->privateI2pCount&&*receipt->privateI2pCount<=512
        ?receipt->privateI2pCount:std::nullopt;
    source.observationComplete=endpoints||privateCount.has_value();
    namespace sets=core::tags::peer_sets;
    const sets::Endpoint privateEndpoint{sets::Network::I2p,sets::Family::Unknown,{}};
    if(!peerSets){return source;}
    source.type.complete=peerSets->complete();
    for(const auto& set:peerSets->sets()){
      if(!set.id){continue;}runtime::Tag tag;tag.id=*set.id;tag.taggableTypes=4;tag.name=peerName(set.name);tag.membership=runtime::Membership::Absent;
      if(set.group){tag.group=QString::fromUtf8(set.group->data(),qsizetype(set.group->size())).toStdU16String();}
      if(tag.name&&set.predicate){
        const auto matches=[&](const auto& address){return set.predicate->matchLivePeer(address)==runtime::Truth::Yes;};
        if((endpoints&&std::ranges::any_of(endpoints->endpoints,matches))||
            (privateCount&&*privateCount&&matches(privateEndpoint))){source.present.push_back(*tag.name);}
      }
      source.type.tags.push_back(std::move(tag));
    }
    return source;
  }
  bool needsPeerEndpoints(const c::Expression& expression)const{
    if(!peerSets){return false;}
    for(const auto& set:peerSets->sets()){if(!set.id||!set.predicate){continue;}const auto name=peerName(set.name);if(!name){continue;}for(const auto& dependency:expression.hasTagNames()){if(dependency==*name){return true;}}}
    return false;
  }
  const ConstraintPeerCensusObservation* peerCensusReceipt(
      const ConstraintFactStamp& stamp,const ConstraintWorkerFrame& frame)const{
    if(!frame.peerCensus||!frame.peerEngineIdentity||!frame.i2p.empty()){return nullptr;}
    const auto& candidate=*frame.peerCensus;
    const auto& request=candidate.request;
    const auto now=std::chrono::steady_clock::now();
    if(request.scope!=ConstraintPeerRequest::Scope::Complete||
        request.stamp.binding.session!=stamp.binding.session||
        request.stamp.binding.target.generation!=stamp.binding.target.generation||
        request.stamp.sequence>=stamp.sequence||request.sourceRevision!=frame.sourceRevision||
        request.definitionsDigest!=peerDefinitionsDigest||candidate.sampled>now||
        now-candidate.sampled>std::chrono::seconds(3)){return nullptr;}
    return &candidate;
  }
  bool peerSetHasNoSavedActions(const core::Document& tagConfig,std::int32_t id)const{
    const auto read=[](const core::Node& dict,std::string_view key)->std::optional<const core::Node*>{
      if(dict.type!=core::Node::Type::Dictionary){return std::nullopt;}
      const core::Node* value=nullptr;
      for(std::size_t index=0;index<dict.children.size();index+=2){
        if(dict.children[index].bytes==key){
          if(value){return std::nullopt;}
          value=&dict.children[index+1];
        }
      }
      return value;
    };
    const auto type=read(tagConfig.root(),"4");
    if(!type||!*type){return type.has_value();}
    const auto entry=read(**type,std::to_string(id));
    if(!entry||!*entry){return entry.has_value();}
    const auto attrs=read(**entry,"c");
    if(!attrs||!*attrs){return attrs.has_value();}
    for(const auto* key:{"pp.1","pp.65536"}){
      const auto action=read(**attrs,key);
      if(!action||(*action&&((*action)->type!=core::Node::Type::Integer||(*action)->integer!=0))){return false;}
    }
    return true;
  }
  using PeerMemberships=std::map<QString,std::vector<std::int32_t>>;
  struct PeerCensusProjection {
    std::vector<std::pair<std::int32_t,std::int32_t>> counts;
    PeerMemberships membership;
  };
  std::optional<PeerCensusProjection> projectPeerCensus(
      const ConstraintPeerCensusObservation& receipt,const ConstraintWorkerFrame& frame,
      const core::Document& tagConfig)const{
    if(!receipt.value||receipt.value->identity!=frame.peerEngineIdentity||
        receipt.value->torrents.size()>64||!peerSets||!frame.library){return {};}
    const auto& census=*receipt.value;
    std::map<std::string,std::uint64_t> members;
    std::size_t endpoints=0;
    for(const auto& torrent:census.torrents){
      if(torrent.identity!=census.identity||!torrent.admission||torrent.endpoints.size()>512-endpoints||
          !members.emplace(torrent.hash.hex(),torrent.admission).second){return {};}
      endpoints+=torrent.endpoints.size();
    }
    if(frame.managed.size()!=members.size()){return {};}
    for(const auto& managed:frame.managed){
      const auto found=members.find(managed.hash.hex());
      if(!managed.retained||managed.failure||found==members.end()||found->second!=managed.admission){return {};}
    }
    for(const auto& row:frame.library->items){
      const auto found=members.find(row.hash.toStdString());
      if(found==members.end()){
        if(row.providerAdmission||row.providerGeneration){return {};}
      }else{
        if(row.providerGeneration!=frame.session||row.providerAdmission!=found->second){return {};}
        members.erase(found);
      }
    }
    if(!members.empty()){return {};}

    PeerCensusProjection result;
    namespace runtime=core::tags::runtime;
    for(const auto& set:peerSets->sets()){
      if(!set.predicate||!set.id||!actionFreePeerSets.contains(*set.id)||
          !peerSetHasNoSavedActions(tagConfig,*set.id)){continue;}
      std::int32_t count=0;
      bool complete=true;
      std::vector<const core::TorrentHash*> matching;
      for(const auto& torrent:census.torrents){
        bool present=false;
        for(const auto& endpoint:torrent.endpoints){
          const auto fact=set.predicate->matchLivePeer(endpoint);
          if(fact==runtime::Truth::Unknown){complete=false;break;}
          if(fact==runtime::Truth::Yes){++count;present=true;}
        }
        if(!complete){break;}
        if(present){matching.push_back(&torrent.hash);}
      }
      if(!complete){continue;}
      result.counts.emplace_back(*set.id,count);
      for(const auto* hash:matching){result.membership[QString::fromStdString(hash->hex())].push_back(*set.id);}
    }
    for(auto& [hash,ids]:result.membership){static_cast<void>(hash);std::ranges::sort(ids);}
    return result;
  }
  bool peerDefinitionsUnchanged()const{
    if(peerDefinitionsDigest.isEmpty()){return true;}auto config=storage::ConfigFile::load(*lease,"biglybt.config");if(!config||config->recoverySource()!=storage::RecoverySource::Primary){return false;}
    const auto& raw=config->document().originalBytes();return peerDefinitionsDigest==QCryptographicHash::hash(QByteArray::fromRawData(raw.data(),qsizetype(raw.size())),QCryptographicHash::Sha256);
  }
