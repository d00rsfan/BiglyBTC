// SPDX-License-Identifier: GPL-2.0-or-later
#include "sharing_values.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>
namespace biglybtc::sharing {
namespace {
using E=ShareValueError;using N=core::Node;struct Failure{E error;};
void require(bool ok,E e){if(!ok){throw Failure{e};}}
constexpr core::Limits Limits{16*1024*1024,16*1024*1024,200000,64};
std::string b(std::string_view x){return std::to_string(x.size())+":"+std::string(x);}
bool unique(const N& n){if(n.type==N::Type::Dictionary){std::set<std::string_view> keys;for(std::size_t i=0;i<n.children.size();i+=2){if(i+1>=n.children.size()||!keys.insert(n.children[i].bytes).second){return false;}}}return std::ranges::all_of(n.children,[](const auto& c){return unique(c);});}
const N* resources(const SharingValues& values){return values.source->source->find("resources");}
std::string raw(const SharingValues& values,std::size_t index){const auto* list=resources(values);require(list&&index<list->children.size(),E::Malformed);const auto& row=list->children[index];return values.source->source->originalBytes().substr(row.begin,row.end-row.begin);}
void writableShape(const SharingValues& values){const auto* list=resources(values);require(!list||list->children.size()==values.rows.size(),E::Unsupported);for(std::size_t i=0;i<values.rows.size();++i){require(list->children[i].type==N::Type::Dictionary&&values.rows[i].selected.sourceIndex==i,E::Unsupported);}}
std::string rewrite(const SharingValues& values,std::string list){auto doc=core::Document::parse(values.source->source->originalBytes(),Limits);require(bool(doc),E::Malformed);require(bool(doc->replaceValue("resources",std::move(list))),E::Malformed);return doc->serialize();}
}
bool supportedShareCacheName(std::string_view value)noexcept{
  if(!value.starts_with("cache")||!value.ends_with(".torrent")){return false;}const auto slash=value.find('/');if(slash==value.npos||slash<=5||value.find('/',slash+1)!=value.npos){return false;}
  const auto directory=value.substr(5,slash-5),file=value.substr(slash+1,value.size()-slash-9);if(directory.empty()||file.empty()||directory.size()>4||file.size()>19||directory.front()=='0'){return false;}
  if(!std::ranges::all_of(directory,[](char c){return c>='0'&&c<='9';})||!std::ranges::all_of(file,[](char c){return c>='0'&&c<='9';})){return false;}
  unsigned part=0;std::uint64_t number=0;const auto a=std::from_chars(directory.data(),directory.data()+directory.size(),part),z=std::from_chars(file.data(),file.data()+file.size(),number);return a.ec==std::errc{}&&z.ec==std::errc{}&&part>=1&&part<=1000&&number<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}
std::expected<SharingValues,E> readSharingValues(std::string bytes){try{
 auto doc=core::Document::parse(std::move(bytes),Limits);require(bool(doc)&&unique(doc->root()),E::Malformed);auto passive=core::readSavedShares(*doc);require(bool(passive),E::Malformed);SharingValues result;result.source=std::make_shared<const core::SavedShares>(std::move(*passive));const auto* list=resources(result);if(!list){return result;}std::map<std::string_view,std::size_t> identities;for(const auto& entry:list->children){const auto* file=entry.find("file");if(file&&file->type==N::Type::Bytes){++identities[file->bytes];}}
 for(const auto& row:result.source->records){SharingValueRow value;value.selected.sourceIndex=row.sourceIndex;value.kind=row.kind;if(row.file){value.selected.file=row.file->bytes;}if(row.cacheFile){value.selected.cacheFile=row.cacheFile->bytes;}const auto& actual=list->children[row.sourceIndex];
  if(!row.kind||(*row.kind!=core::SavedShareKind::File&&*row.kind!=core::SavedShareKind::Directory)){value.refusal="Only whole file or directory shares are supported";}
  else if(row.duplicateIdentity||identities[value.selected.file]>1){value.refusal="The saved share identity is ambiguous";}
  else if(!row.file||!row.file->validEncoding||row.file->embeddedNul||!std::filesystem::path(row.file->bytes).is_absolute()){value.refusal="The saved source path is not a supported absolute UTF8 path";}
  else if(!row.persistent.value_or(false)){value.refusal="This share is not persistent";}
  else if(actual.find("per_key")){value.refusal="Personal share salt is not supported";}
  else if(!row.fingerprint||row.fingerprint->size()!=16){value.refusal="The Original share fingerprint is missing or malformed";}
  else if(!row.cacheFile||!row.cacheFile->validEncoding||row.cacheFile->embeddedNul||!supportedShareCacheName(row.cacheFile->bytes)){value.refusal="The saved cache name is outside shares/cacheN/decimal.torrent";}
  else{const auto* attrs=actual.find("attributes"),*props=actual.find("props");if(attrs&&(attrs->type!=N::Type::Dictionary||!attrs->children.empty())){value.refusal="Share category, tag or custom attributes are not supported";}else if(!props||props->type!=N::Type::Dictionary){value.refusal="Share properties are malformed";}else{for(std::size_t i=0;i<props->children.size();i+=2){if(props->children[i].bytes!="persistent"){value.refusal="Share property overrides are not supported";break;}}}}
  value.available=value.refusal.empty();result.rows.push_back(std::move(value));
 }return result;
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
std::expected<std::string,E> originalShareFingerprint(std::string_view input){try{
 require(input.size()<=8*1024*1024,E::Limit);std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),EVP_MD_CTX_free);require(bool(context)&&EVP_DigestInit_ex(context.get(),EVP_md5(),nullptr)==1,E::Crypto);std::size_t at=0;
 // BrokenMd5Hasher.calculateHash wraps its initial input BIG_ENDIAN, but its
 // buffered final block LITTLE_ENDIAN. It reads paired32bit words via getLong.
 for(;at+64<=input.size();at+=64){std::array<unsigned char,64> block{};for(std::size_t group=0;group<64;group+=8){for(std::size_t n=0;n<8;++n){block[group+n]=static_cast<unsigned char>(input[at+group+7-n]);}}require(EVP_DigestUpdate(context.get(),block.data(),block.size())==1,E::Crypto);}
 if(at<input.size()){require(EVP_DigestUpdate(context.get(),input.data()+at,input.size()-at)==1,E::Crypto);}std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};unsigned length=0;require(EVP_DigestFinal_ex(context.get(),digest.data(),&length)==1&&length==16,E::Crypto);std::string out(16,'\0');for(std::size_t word=0;word<16;word+=4){for(std::size_t n=0;n<4;++n){out[word+n]=static_cast<char>(digest[word+3-n]);}}return out;
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
std::expected<std::string,E> appendPersistentShare(const SharingValues& values,core::SavedShareKind kind,std::string_view file,std::string_view cache,std::string_view fingerprint){try{
 writableShape(values);require(kind==core::SavedShareKind::File||kind==core::SavedShareKind::Directory,E::Unsupported);require(supportedShareCacheName(cache)&&fingerprint.size()==16&&!file.empty()&&file.find('\0')==file.npos&&std::filesystem::path(file).is_absolute(),E::Malformed);if(const auto* entries=resources(values)){for(const auto& entry:entries->children){const auto* actualFile=entry.find("file"),*actualCache=entry.find("ifile");require(!actualFile||actualFile->type!=N::Type::Bytes||actualFile->bytes!=file,E::Ambiguous);require(!actualCache||actualCache->type!=N::Type::Bytes||actualCache->bytes!=cache,E::Ambiguous);}}
 const auto row="d10:attributesde4:file"+b(file)+"5:ifile"+b(cache)+"5:ihash"+b(fingerprint)+"5:propsd10:persistent4:truee4:typei"+std::to_string(kind==core::SavedShareKind::File?1:2)+"ee";std::string list="l";for(std::size_t i=0;i<values.rows.size();++i){list+=raw(values,i);}list+=row;list+='e';return rewrite(values,std::move(list));
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
std::expected<std::string,E> eraseShare(const SharingValues& values,const SavedShareSelector& selected){try{
 writableShape(values);require(selected.sourceIndex<values.rows.size()&&values.rows[selected.sourceIndex].selected==selected,E::Ambiguous);require(values.rows[selected.sourceIndex].available,E::Unsupported);std::string list="l";for(std::size_t i=0;i<values.rows.size();++i){if(i!=selected.sourceIndex){list+=raw(values,i);}}list+='e';return rewrite(values,std::move(list));
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
} // namespace biglybtc::sharing
