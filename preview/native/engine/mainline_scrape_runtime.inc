// Included in MainlineDhtService::Impl. All cookies remain alive until session
// join; a cancellation consumes delivery authority, never reuses RPC storage.
struct ScrapeRound;
struct ScrapeProbe {
  ScrapeRound* round=nullptr;
  UdpEndpoint endpoint;
  lt::sha1_hash nodeId{};
  bool issued=false,finished=false;
};
struct ScrapeRound {
  std::uint64_t number=0;
  lt::sha1_hash hash{};
  services::mainline_scrape::Dialect dialect=services::mainline_scrape::Dialect::OriginalBFse;
  bool noSeeds=false,started=false,closed=false,cancelled=false,failed=false;
  std::chrono::steady_clock::time_point deadline{};
  std::size_t pendingTables=0,issued=0,finished=0;
  std::array<ScrapeProbe,16> probes{};
  services::mainline_scrape::Aggregate aggregate;
};
std::map<std::uint64_t,ScrapeRound> scrapeRounds;
std::uint64_t nextScrape=0,activeScrape=0;
std::vector<MainlineDhtScrape> completedScrapes;

void finishScrape(ScrapeRound& round,bool timedOut=false) {
  if(round.closed){return;}
  round.closed=true;
  if(activeScrape==round.number){activeScrape=0;}
  if(round.cancelled){round.aggregate.cancel();return;}
  auto summary=round.aggregate.finish();
  // A codec/resource failure is terminal too, with no positive publication.
  const auto counts=!summary||round.failed?services::mainline_scrape::Summary{}:*summary;
  completedScrapes.push_back({round.number,round.hash,round.dialect,counts,timedOut});
}
void issueScrape(ScrapeRound& round,const lt::dht_live_nodes_alert& table) {
  if(round.closed||round.cancelled){return;}
  unsigned v4=0,v6=0;
  for(const auto& prior:round.probes){if(prior.issued){++(prior.endpoint.address().is_v4()?v4:v6);}}
  for(const auto& [id,endpoint]:table.nodes()){
    if(id.is_all_zeros()||id==table.node_id||!permittedContact(endpoint,ipv4,ipv6,bindings)){continue;}
    auto& count=endpoint.address().is_v4()?v4:v6;
    if(count==8||round.issued==round.probes.size()){continue;}
    const bool duplicate=std::ranges::any_of(round.probes,[&](const auto& p){return p.issued&&p.endpoint==endpoint;});
    if(duplicate){continue;}
    auto& probe=round.probes[round.issued];probe.round=&round;probe.endpoint=endpoint;probe.nodeId=id;
    lt::entry query(lt::entry::dictionary_type{});query["q"]="get_peers";
    auto& args=query["a"];args["info_hash"]=round.hash.to_string();args["scrape"]=1;
    args["noseed"]=round.noSeeds?1:0;
    session->dht_direct_request(endpoint,query,lt::client_data_t(&probe));
    probe.issued=true;++round.issued;++count;
  }
}
bool acceptScrape(const lt::dht_direct_response_alert& response) {
  const auto* cookie=response.userdata.get<ScrapeProbe>();
  ScrapeProbe* matched=nullptr;
  for(auto& [number,round]:scrapeRounds){
    (void)number;
    for(auto& probe:round.probes){if(&probe==cookie){matched=&probe;break;}}
    if(matched){break;}
  }
  if(!matched){return false;}
  auto& probe=*matched;auto& round=*probe.round;
  if(!probe.issued||probe.finished){return true;}
  probe.finished=true;++round.finished;
  if(!round.closed&&!round.cancelled&&response.endpoint==probe.endpoint){
    const auto packet=response.response();
    if(packet.type()==lt::bdecode_node::dict_t&&packet.dict_find_string_value("y")=="r"){
      const auto body=packet.dict_find_dict("r");
      const auto id=body?body.dict_find_string("id"):lt::bdecode_node{};
      if(id&&id.string_length()==20&&lt::sha1_hash(id.string_ptr())==probe.nodeId){
        const auto encoded=body.data_section();
        auto decoded=services::mainline_scrape::decode({encoded.data(),static_cast<std::size_t>(encoded.size())},round.dialect);
        if(decoded&&!round.aggregate.add(*decoded)){round.failed=true;finishScrape(round);}
      }
    }
  }
  if(!round.pendingTables&&round.finished==round.issued){finishScrape(round);}
  return true;
}
