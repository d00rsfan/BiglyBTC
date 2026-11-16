// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/saved_peer_sets.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <new>
#include <set>
namespace biglybtc::core::tags::peer_sets {
namespace {
struct Failure {Error error;};
void need(bool value,Error error){if(!value){throw Failure{error};}}
std::string_view trim(std::string_view s){while(!s.empty()&&static_cast<unsigned char>(s.front())<=32){s.remove_prefix(1);}while(!s.empty()&&static_cast<unsigned char>(s.back())<=32){s.remove_suffix(1);}return s;}
std::string lower(std::string_view s){std::string out(s);for(char& c:out){if(c>='A'&&c<='Z'){c+=32;}}return out;}
bool utf8(std::string_view s){
 for(std::size_t i=0;i<s.size();){const auto first=static_cast<unsigned char>(s[i++]);if(first<128){continue;}const unsigned count=first>=0xc2&&first<=0xdf?1:first>=0xe0&&first<=0xef?2:first>=0xf0&&first<=0xf4?3:0;if(!count||count>s.size()-i){return false;}std::uint32_t code=first&((1U<<(6-count))-1);for(unsigned j=0;j<count;++j){const auto next=static_cast<unsigned char>(s[i++]);if((next&0xc0)!=0x80){return false;}code=(code<<6)|(next&63);}if(code<(count==1?128U:count==2?2048U:65536U)||code>0x10ffff||(code>=0xd800&&code<=0xdfff)){return false;}}
 return true;
}
// Java String.split(regex) drops all trailing empty pieces.
std::vector<std::string_view> split(std::string_view text,char delimiter){std::vector<std::string_view> out;std::size_t at=0;for(;;){const auto end=text.find(delimiter,at);out.push_back(text.substr(at,end==text.npos?text.npos:end-at));if(end==text.npos){break;}at=end+1;}while(!out.empty()&&out.back().empty()){out.pop_back();}return out;}
std::string identityKey(std::string_view text){constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out="speed.limit.handler.ipset_n.";unsigned buffer=0,bits=0;for(unsigned char c:text){buffer=(buffer<<8)|c;bits+=8;while(bits>=5){bits-=5;out+=alphabet[(buffer>>bits)&31];}}if(bits){out+=alphabet[(buffer<<(5-bits))&31];}return out;}
struct Pending {SavedSet value;std::vector<std::string> tokens;bool unknown=false,inverse=false;};
}
std::expected<SavedPeerSets,Error> SavedPeerSets::read(const Document& config,SavedLimits limit){
 try{
  const SavedLimits maximum;limit.lines=std::min(limit.lines,maximum.lines);limit.lineBytes=std::min(limit.lineBytes,maximum.lineBytes);limit.totalBytes=std::min(limit.totalBytes,maximum.totalBytes);limit.sets=std::min(limit.sets,maximum.sets);limit.nameBytes=std::min(limit.nameBytes,maximum.nameBytes);limit.ranges=std::min(limit.ranges,maximum.ranges);
  need(config.root().type==Node::Type::Dictionary,Error::Invalid);SavedPeerSets out;
  const auto* lines=config.find("speed.limit.handler.schedule.lines");if(!lines){return out;}need(lines->type==Node::Type::List,Error::Invalid);need(lines->children.size()<=limit.lines,Error::Bounds);
  std::vector<Pending> pending;std::map<std::string,std::size_t,std::less<>> names;std::size_t total=0;
  for(const auto& node:lines->children){
   need(node.type==Node::Type::Bytes,Error::Invalid);need(node.bytes.size()<=limit.lineBytes&&node.bytes.size()<=limit.totalBytes-total,Error::Bounds);total+=node.bytes.size();need(utf8(node.bytes),Error::Invalid);auto line=trim(node.bytes);if(line.empty()||line.starts_with('#')){continue;}const auto lc=lower(line);
   if(lc.starts_with("enable")){const auto fields=split(lc,'=');if(fields.size()==2&&(trim(fields[1])=="yes"||trim(fields[1])=="no")){out.enabled_=trim(fields[1])=="yes";}else{out.complete_=false;}continue;}
   if(!lc.starts_with("ip_set")&&!lc.starts_with("peer_set")){out.complete_=false;continue;}
   const auto args=split(line.substr(lc.find('_')+4),',');bool inverse=false,bad=false,unsupported=false;std::optional<std::size_t> last;std::set<std::size_t> touched;std::optional<std::string> group;
   for(const auto arg:args){
    const auto fields=split(arg,'=');if(fields.size()!=2){bad=true;break;}const auto lhs=trim(fields[0]),rhs=trim(fields[1]);const auto key=lower(lhs);
    if(key=="inverse"){inverse=lower(rhs)=="yes";continue;}
    if(key=="group"){need(rhs.size()<=limit.nameBytes,Error::Bounds);group=rhs;continue;}
    if(key=="up"||key=="down"||key=="peer_up"||key=="peer_down"||key=="cat"||key=="tag"||key=="client"||key=="intf"||key=="asn"){unsupported=true;continue;}
    need(!lhs.empty(),Error::Invalid);need(lhs.size()<=limit.nameBytes,Error::Bounds);auto found=names.find(lhs);std::size_t index;
    if(found==names.end()){need(pending.size()<limit.sets,Error::Bounds);index=pending.size();names.emplace(std::string(lhs),index);Pending value;value.value.name=lhs;pending.push_back(std::move(value));}else{index=found->second;}last=index;touched.insert(index);
    std::string definition(rhs);std::ranges::replace(definition,';',' ');
    for(auto token:split(definition,' ')){
     token=trim(token);if(token.empty()){continue;}auto ref=names.find(token);
     if(ref!=names.end()&&ref->second!=index){const auto& other=pending[ref->second];auto& current=pending[index];need(other.tokens.size()<=limit.ranges-current.tokens.size(),Error::Bounds);current.tokens.insert(current.tokens.end(),other.tokens.begin(),other.tokens.end());current.unknown=current.unknown||other.unknown;}
     else{auto& current=pending[index];need(current.tokens.size()<limit.ranges,Error::Bounds);current.tokens.emplace_back(token);const std::array one{std::string(token)};if(!Ipv4CidrSet::create(one)){current.unknown=true;}}
    }
   }
   if(!last||bad){out.complete_=false;for(auto index:touched){pending[index].unknown=true;}continue;}
   auto& current=pending[*last];current.inverse=inverse;if(group){current.value.group=std::move(group);}current.unknown=current.unknown||unsupported;if(unsupported){out.complete_=false;}
  }
  if(!out.enabled_){return out;}
  std::map<std::int32_t,std::size_t> identities;
  for(auto& value:pending){
   auto predicate=Ipv4CidrSet::create(value.tokens,value.inverse);if(!value.unknown&&predicate){value.value.predicate=std::move(*predicate);}else{out.complete_=false;}
   const auto* id=config.find(identityKey(value.value.name));if(id&&id->type==Node::Type::Integer){const auto narrowed=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(id->integer));if(narrowed!=-1){value.value.id=narrowed;}}
   if(!value.value.id){out.complete_=false;}else{auto [prior,unique]=identities.emplace(*value.value.id,out.sets_.size());if(!unique){out.sets_[prior->second].id.reset();value.value.id.reset();out.complete_=false;}}
   out.sets_.push_back(std::move(value.value));
  }
  return out;
 }catch(const Failure& failure){return std::unexpected(failure.error);}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
}
