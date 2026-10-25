#include "original_torrent_identity.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace biglybtc::storage {
namespace {
using Node=core::Node;
struct Failure{Error error;};
void require(bool yes,ErrorCode code,const char* message){if(!yes){throw Failure{{code,message}};}}
template<class F>auto guarded(F&& f)->decltype(f()){try{return f();}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Original identity allocation failed"});}}
const Node* one(const Node& dict,std::string_view key){require(dict.type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Original identity section is not a dictionary");const Node* result=nullptr;for(std::size_t n=0;n<dict.children.size();n+=2){if(dict.children[n].bytes==key){require(!result,ErrorCode::InvalidFormat,"Duplicate original identity field");result=&dict.children[n+1];}}return result;}
bool prefixCollision(const OriginalFullInfoHashes& h){return h.v1&&h.v2&&std::equal(h.v1->begin(),h.v1->end(),h.v2->begin());}
void identityShape(const OriginalTorrentIdentity& identity){require(identity.selected==OriginalSelectedHash::V1||identity.selected==OriginalSelectedHash::V2,ErrorCode::InvalidFormat,"Invalid selected original hash type");require(identity.originalKey.bytes().size()==20&&!prefixCollision(identity.full),ErrorCode::Conflict,"Ambiguous original 20-byte hash prefix");const std::byte* expected=nullptr;if(identity.selected==OriginalSelectedHash::V1){require(identity.full.v1.has_value(),ErrorCode::InvalidFormat,"Selected v1 hash is unavailable");expected=identity.full.v1->data();}else{require(identity.full.v2.has_value(),ErrorCode::InvalidFormat,"Selected v2 hash is unavailable");expected=identity.full.v2->data();}require(std::equal(identity.originalKey.bytes().begin(),identity.originalKey.bytes().end(),expected),ErrorCode::Conflict,"Selected original key differs from full hash evidence");}
}
std::expected<std::string_view,Error> originalTorrentInfoSubject(const core::Document& document){return guarded([&]()->std::expected<std::string_view,Error>{const auto* info=one(document.root(),"info");const auto& bytes=document.originalBytes();require(info&&info->type==Node::Type::Dictionary&&!info->changed&&!info->origin&&info->begin<info->end&&info->end<=bytes.size(),ErrorCode::InvalidFormat,"Identity requires unrewritten original info bytes");return std::string_view(bytes).substr(info->begin,info->end-info->begin);});}
std::expected<OriginalTorrentIdentity,Error> selectOriginalTorrentIdentity(const core::Document& document,const ComputedOriginalInfoHashes& computed){return guarded([&]()->std::expected<OriginalTorrentIdentity,Error>{
  const auto subject=originalTorrentInfoSubject(document);if(!subject){return std::unexpected(subject.error());}require(*subject==computed.rawInfo,ErrorCode::Conflict,"Computed hashes are bound to different raw info bytes");const auto* info=one(document.root(),"info");require(!one(*info,"hash-override"),ErrorCode::Unsupported,"Metadata-pending hash override is not ordinary torrent identity");
  const auto* pieces=one(*info,"pieces"),*tree=one(*info,"file tree"),*version=one(*info,"meta version");require((!pieces||pieces->type==Node::Type::Bytes)&&(!tree||(tree->type==Node::Type::Dictionary&&version&&version->type==Node::Type::Integer&&version->integer==2)),ErrorCode::InvalidFormat,"Full hash evidence requires validated v1/v2 metadata shape");const bool v1=pieces!=nullptr,v2=tree!=nullptr;require((v1||v2)&&computed.hashes.v1.has_value()==v1&&computed.hashes.v2.has_value()==v2,ErrorCode::InvalidFormat,"Computed full hashes disagree with actual metadata versions");require(!prefixCollision(computed.hashes),ErrorCode::Conflict,"v1 and truncated v2 identities collide");
  bool selectedV2=false;if(const auto* props=one(document.root(),"azureus_private_properties")){require(props->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Original private properties are malformed");if(const auto* selector=one(*props,"hybrid_hash_v2")){require(selector->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Original hybrid selector must be an integer");selectedV2=selector->integer==1;}}
  const auto selected=v2&&(!v1||selectedV2)?OriginalSelectedHash::V2:OriginalSelectedHash::V1;const auto* digest=selected==OriginalSelectedHash::V1?computed.hashes.v1->data():computed.hashes.v2->data();auto key=core::TorrentHash::parse(std::string_view(reinterpret_cast<const char*>(digest),20));require(key.has_value(),ErrorCode::InvalidFormat,"Invalid original selected digest");return OriginalTorrentIdentity{std::move(*key),selected,computed.hashes};
});}
std::expected<void,Error> validateOriginalTorrentKey(const OriginalTorrentIdentity& identity,const core::TorrentHash& key,bool fullV2){return guarded([&]()->std::expected<void,Error>{identityShape(identity);if(key==identity.originalKey){return {};}const auto bytes=key.bytes();require(fullV2&&bytes.size()==32&&identity.full.v2&&std::equal(bytes.begin(),bytes.end(),identity.full.v2->begin()),ErrorCode::Conflict,"Saved key differs from selected original/full-v2 policy");return {};});}
std::expected<void,Error> matchOriginalFullHashes(const OriginalFullInfoHashes& actual,const OriginalFullInfoHashes& requested){return guarded([&]()->std::expected<void,Error>{require(actual.v1||actual.v2,ErrorCode::InvalidFormat,"Actual full identity is unavailable");require(requested.v1||requested.v2,ErrorCode::InvalidFormat,"Requested full identity is unavailable");require((!requested.v1||(actual.v1&&*actual.v1==*requested.v1))&&(!requested.v2||(actual.v2&&*actual.v2==*requested.v2)),ErrorCode::Conflict,"Metadata does not match every requested full hash");return {};});}
std::expected<OriginalIdentityRelation,Error> compareOriginalTorrentIdentities(const OriginalTorrentIdentity& a,const OriginalTorrentIdentity& b){return guarded([&]()->std::expected<OriginalIdentityRelation,Error>{identityShape(a);identityShape(b);if(a.originalKey==b.originalKey){require(a.full==b.full&&a.selected==b.selected,ErrorCode::Conflict,"Selected identity collision has conflicting full hash evidence");return OriginalIdentityRelation::SameSelected;}const bool sameV1=a.full.v1&&b.full.v1&&*a.full.v1==*b.full.v1,sameV2=a.full.v2&&b.full.v2&&*a.full.v2==*b.full.v2;if(sameV1||sameV2){require(a.full==b.full,ErrorCode::Conflict,"Shared swarm hash has conflicting complete identity evidence");return OriginalIdentityRelation::AlternateSelected;}return OriginalIdentityRelation::Distinct;});}
namespace {
bool utf16Key(std::string_view bytes, std::u16string& out) {
  out.clear();
  for (std::size_t at=0;at<bytes.size();) {
    const auto first=static_cast<unsigned char>(bytes[at++]);
    std::uint32_t scalar=0, minimum=0; unsigned continuation=0;
    if (first<0x80) { scalar=first; }
    else if ((first&0xe0)==0xc0) { scalar=first&0x1f; continuation=1; minimum=0x80; }
    else if ((first&0xf0)==0xe0) { scalar=first&0x0f; continuation=2; minimum=0x800; }
    else if ((first&0xf8)==0xf0) { scalar=first&0x07; continuation=3; minimum=0x10000; }
    else { return false; }
    if (continuation>bytes.size()-at) { return false; }
    for (unsigned i=0;i<continuation;++i) {
      const auto next=static_cast<unsigned char>(bytes[at++]);
      if ((next&0xc0)!=0x80) { return false; }
      scalar=(scalar<<6)|(next&0x3f);
    }
    if (scalar<minimum||scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff)) { return false; }
    if (scalar<0x10000) { out.push_back(static_cast<char16_t>(scalar)); }
    else { scalar-=0x10000; out.push_back(static_cast<char16_t>(0xd800+(scalar>>10))); out.push_back(static_cast<char16_t>(0xdc00+(scalar&0x3ff))); }
  }
  return true;
}
struct InfoOrderState { bool byteSorted=true,javaSorted=true; std::size_t nodes=0; };
void infoOrderNode(const Node& node,const Node* tree,bool inTree,std::size_t depth,InfoOrderState& state) {
  require(depth<=128&&++state.nodes<=2000000&&!node.changed&&!node.origin,
      ErrorCode::InvalidFormat,"Original info order requires bounded pristine nodes");
  if (node.type==Node::Type::Dictionary) {
    require(node.children.size()%2==0,ErrorCode::InvalidFormat,"Incomplete original info dictionary");
    std::string_view previous; std::u16string priorUnits; bool priorValid=true;
    for (std::size_t i=0;i<node.children.size();i+=2) {
      const auto& key=node.children[i];
      require(++state.nodes<=2000000&&key.type==Node::Type::Bytes&&!key.changed&&!key.origin,
          ErrorCode::InvalidFormat,"Invalid original info dictionary key");
      if (inTree) {
        state.byteSorted=state.byteSorted&&(i==0||previous<key.bytes);
        std::u16string units; const bool valid=utf16Key(key.bytes,units);
        state.javaSorted=state.javaSorted&&valid&&(i==0||(priorValid&&priorUnits<units));
        priorValid=valid; priorUnits=std::move(units);
      } else {
        require(i==0||previous<key.bytes,ErrorCode::InvalidFormat,"Noncanonical original info keys outside v2 file tree");
      }
      previous=key.bytes; const auto& value=node.children[i+1];
      infoOrderNode(value,tree,inTree||&value==tree,depth+1,state);
    }
  } else if (node.type==Node::Type::List) {
    for (const auto& value:node.children) { infoOrderNode(value,tree,inTree,depth+1,state); }
  }
}
}
std::expected<OriginalInfoOrder,Error> originalTorrentInfoOrder(const core::Document& document) {
  return guarded([&]()->std::expected<OriginalInfoOrder,Error> {
    auto subject=originalTorrentInfoSubject(document); if (!subject) { return std::unexpected(subject.error()); }
    require(subject->size()<=32*1024*1024,ErrorCode::InvalidFormat,"Original info exceeds metadata bound");
    const auto* info=one(document.root(),"info"); const auto* tree=one(*info,"file tree");
    const auto* version=one(*info,"meta version");
    if (!tree||tree->type!=Node::Type::Dictionary||!version||version->type!=Node::Type::Integer||version->integer!=2) { tree=nullptr; }
    InfoOrderState state; infoOrderNode(*info,tree,false,0,state);
    if (state.byteSorted) { return OriginalInfoOrder::ByteSorted; }
    require(tree&&state.javaSorted,ErrorCode::InvalidFormat,"Original v2 tree is neither byte-sorted nor consistently Java UTF16-sorted");
    return OriginalInfoOrder::JavaUtf16FileTree;
  });
}
}
