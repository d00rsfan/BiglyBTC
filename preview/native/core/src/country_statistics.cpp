#include "biglybtc/country_statistics.hpp"
#include <algorithm>
#include <limits>

namespace biglybtc::core {
namespace {
template<std::size_t N> bool empty(const std::array<char,N>& value) noexcept {
  return std::all_of(value.begin(),value.end(),[](char c){return c==0;});
}
bool validLocation(const CountryLookup& value) noexcept {
  if(value.kind==CountryLookupKind::Unavailable||value.kind==CountryLookupKind::Unknown){return empty(value.code)&&empty(value.name);}
  return value.kind==CountryLookupKind::Country && value.code[0]>='A'&&value.code[0]<='Z'
      && value.code[1]>='A'&&value.code[1]<='Z'&&value.code[2]==0
      && std::find(value.name.begin(),value.name.end(),'\0')!=value.name.end();
}
bool retired(PeerPayloadPhase value) noexcept {
  return value==PeerPayloadPhase::Disconnected||value==PeerPayloadPhase::Released;
}
bool validPhase(PeerPayloadPhase value) noexcept {
  return value==PeerPayloadPhase::Attached||value==PeerPayloadPhase::Connected||retired(value);
}
void addBytes(std::optional<std::int64_t>& total, std::size_t& peers, bool& overflow,
    const std::optional<std::int64_t>& amount) noexcept {
  if(!amount){return;}++peers;if(overflow){return;}if(!total){total=0;}
  if(*amount>std::numeric_limits<std::int64_t>::max()-*total){total.reset();overflow=true;return;}*total+=*amount;
}
bool validPrefix(const std::optional<std::int64_t>& value,const PeerPayloadRecord& p,bool received) noexcept {
  return !value||(p.lastObserved&&*value>=0&&*value<=(received?p.lastObserved->received:p.lastObserved->sent));
}
bool validRowLocation(const CountryRow& row) noexcept {
  if(row.kind==CountryRowKind::Country){return validLocation({CountryLookupKind::Country,row.code,row.name});}
  return (row.kind==CountryRowKind::Local||row.kind==CountryRowKind::Unknown||row.kind==CountryRowKind::Unavailable)
      && empty(row.code)&&empty(row.name);
}
} // namespace

std::expected<CountryStatistics,CountryStatisticsError> projectCountryStatistics(
    const PeerPayloadSnapshot& source,std::span<const CountryResolvedPeer> locations,bool available) noexcept {
  if(!source.identity.applicationRun||!source.identity.profileSession||!source.identity.engineEpoch
      ||source.count>MaximumPeerPayloadRecords||(source.count&&!source.sequence)||locations.size()>MaximumPeerPayloadRecords){
    return std::unexpected(CountryStatisticsError::InvalidLedger);
  }
  for(std::size_t i=0;i<source.count;++i){
    const auto& p=source.records[i];
    if(!p.connection||!p.operation||!validPhase(p.phase)||!p.tailUncertain
        ||(p.latestSampleAvailable&&!p.lastObserved)||!validPrefix(p.observedReceivedBytes,p,true)
        ||!validPrefix(p.observedSentBytes,p,false)
        ||(p.byteBaselineAvailable&&(!p.lastObserved||!p.latestSampleAvailable))
        ||(hasPeerPayloadIssue(p.issues,PeerPayloadIssue::ReceivedPrefixOverflow)&&p.observedReceivedBytes)
        ||(hasPeerPayloadIssue(p.issues,PeerPayloadIssue::SentPrefixOverflow)&&p.observedSentBytes)){
      return std::unexpected(CountryStatisticsError::InvalidLedger);}
    for(std::size_t j=0;j<i;++j){if(source.records[j].connection==p.connection){return std::unexpected(CountryStatisticsError::InvalidLedger);}}
  }
  for(std::size_t i=0;i<locations.size();++i){
    if(!locations[i].connection||!validLocation(locations[i].location)){return std::unexpected(CountryStatisticsError::InvalidResolution);}
    bool found=false;
    for(std::size_t j=0;j<source.count;++j){found|=source.records[j].connection==locations[i].connection;}
    if(!found){return std::unexpected(CountryStatisticsError::InvalidResolution);}
    for(std::size_t j=0;j<i;++j){if(locations[j].connection==locations[i].connection){return std::unexpected(CountryStatisticsError::InvalidResolution);}}
  }
  CountryStatistics result;result.identity=source.identity;result.sequence=source.sequence;
  result.providerAvailable=available;result.ledgerIssues=source.issues;result.retainedConnections=source.count;
  result.partial=source.issues!=0||!available;
  for(std::size_t i=0;i<source.count;++i){
    const auto& peer=source.records[i];
    const bool wasRetired=retired(peer.phase);
    if(wasRetired){++result.retiredConnections;}else{++result.includedConnections;}
    CountryRowKind kind=CountryRowKind::Unavailable;CountryLookup location;
    if(peer.network==PeerNetworkClass::Local){kind=CountryRowKind::Local;}
    else if(available&&peer.network==PeerNetworkClass::NonLocal
        &&(peer.remote.family==PeerAddressFamily::V4||peer.remote.family==PeerAddressFamily::V6)){
      for(const auto& candidate:locations){if(candidate.connection==peer.connection){location=candidate.location;break;}}
      if(location.kind==CountryLookupKind::Country){kind=CountryRowKind::Country;}
      else if(location.kind==CountryLookupKind::Unknown){kind=CountryRowKind::Unknown;}
    }
    std::size_t row=0;
    for(;row<result.rowCount;++row){const auto& old=result.rows[row];if(old.kind==kind
        &&(kind!=CountryRowKind::Country||old.code==location.code)){break;}}
    if(row==result.rowCount){
      if(row==MaximumCountryRows){return std::unexpected(CountryStatisticsError::InvalidLedger);}
      auto& added=result.rows[result.rowCount++];added.kind=kind;added.partial=false;
      if(kind==CountryRowKind::Country){added.code=location.code;added.name=location.name;}
    }
    auto& group=result.rows[row];++group.recordedPeers;if(wasRetired){++group.retiredPeers;}
    addBytes(group.observedDownloadBytes,group.downloadBytePeers,group.downloadBytesOverflow,peer.observedReceivedBytes);
    addBytes(group.observedUploadBytes,group.uploadBytePeers,group.uploadBytesOverflow,peer.observedSentBytes);
    const bool known=!wasRetired&&peer.latestSampleAvailable&&peer.lastObserved
        &&peer.lastObserved->receivedRate>=0&&peer.lastObserved->sentRate>=0;
    if(known){
      const auto down=peer.lastObserved->receivedRate,up=peer.lastObserved->sentRate;
      const auto oldDown=group.observedDownloadRate.value_or(0),oldUp=group.observedUploadRate.value_or(0);
      if(oldDown>std::numeric_limits<std::int64_t>::max()-down||oldUp>std::numeric_limits<std::int64_t>::max()-up){
        return std::unexpected(CountryStatisticsError::InvalidLedger);
      }
      group.observedDownloadRate=oldDown+down;group.observedUploadRate=oldUp+up;++group.rateObservedPeers;
    }else if(!wasRetired){++group.rateMissingPeers;}
    group.partial|=source.issues!=0||peer.issues!=0||wasRetired||!known
        ||!peer.observedReceivedBytes||!peer.observedSentBytes||group.downloadBytesOverflow||group.uploadBytesOverflow
        ||kind==CountryRowKind::Unknown||kind==CountryRowKind::Unavailable;
    result.partial|=group.partial;
  }
  if(!validCountryStatistics(result)){return std::unexpected(CountryStatisticsError::InvalidLedger);}
  return result;
}
bool validCountryStatistics(const CountryStatistics& value) noexcept {
  if(!value.identity.applicationRun||!value.identity.profileSession||!value.identity.engineEpoch
      ||!value.tailUncertain||(value.retainedConnections&&!value.sequence)||value.retainedConnections>MaximumPeerPayloadRecords
      ||value.includedConnections>value.retainedConnections||value.retiredConnections>value.retainedConnections
      ||value.includedConnections!=value.retainedConnections-value.retiredConnections
      ||value.rowCount>MaximumCountryRows||value.rowCount>value.retainedConnections
      ||((value.ledgerIssues||!value.providerAvailable)&&!value.partial)){return false;}
  std::size_t peers=0,retiredPeers=0;
  for(std::size_t i=0;i<value.rowCount;++i){
    const auto& row=value.rows[i];
    if(!validRowLocation(row)||!row.recordedPeers||row.recordedPeers>MaximumPeerPayloadRecords||row.retiredPeers>row.recordedPeers){return false;}
    const auto active=row.recordedPeers-row.retiredPeers;
    if(row.rateObservedPeers>active||row.rateMissingPeers!=active-row.rateObservedPeers
        ||row.observedDownloadRate.has_value()!=(row.rateObservedPeers!=0)
        ||row.observedUploadRate.has_value()!=(row.rateObservedPeers!=0)
        ||(!value.providerAvailable&&(row.kind==CountryRowKind::Country||row.kind==CountryRowKind::Unknown))){return false;}
    const auto maximum=static_cast<std::int64_t>(row.rateObservedPeers)*std::numeric_limits<std::int32_t>::max();
    if(row.observedDownloadRate&&(*row.observedDownloadRate<0||*row.observedUploadRate<0
        ||*row.observedDownloadRate>maximum||*row.observedUploadRate>maximum)){return false;}
    const auto validBytes=[&](const std::optional<std::int64_t>& bytes,std::size_t known,bool overflow){
      return known<=row.recordedPeers&&bytes.has_value()==(known!=0&&!overflow)
          &&(!bytes||*bytes>=0)&&(!overflow||(known!=0&&row.partial));
    };
    if(!validBytes(row.observedDownloadBytes,row.downloadBytePeers,row.downloadBytesOverflow)
        ||!validBytes(row.observedUploadBytes,row.uploadBytePeers,row.uploadBytesOverflow)){return false;}
    if((row.rateMissingPeers||row.retiredPeers||value.ledgerIssues||row.kind==CountryRowKind::Unknown||row.kind==CountryRowKind::Unavailable
        ||row.downloadBytePeers<row.recordedPeers||row.uploadBytePeers<row.recordedPeers)&&!row.partial){return false;}
    if(row.partial&&!value.partial){return false;}
    if(peers>MaximumPeerPayloadRecords-row.recordedPeers||retiredPeers>MaximumPeerPayloadRecords-row.retiredPeers){return false;}
    peers+=row.recordedPeers;retiredPeers+=row.retiredPeers;
    for(std::size_t j=0;j<i;++j){if(value.rows[j].kind==row.kind
        &&(row.kind!=CountryRowKind::Country||value.rows[j].code==row.code)){return false;}}
  }
  return peers==value.retainedConnections&&retiredPeers==value.retiredConnections;
}
} // namespace biglybtc::core
