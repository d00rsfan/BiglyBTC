#include "biglybtc/file_selection_intent.hpp"
#include <algorithm>

namespace biglybtc::compat {
namespace {
bool valid(std::span<const std::uint8_t> changed){
  return !changed.empty()&&changed.size()<=100000
      &&std::ranges::none_of(changed,[](auto value){return value>1;})
      &&std::ranges::any_of(changed,[](auto value){return value==1;});
}
}
std::expected<std::string,PolicyError> encodeOriginalFileSelectionIntent(
    std::span<const std::uint8_t> changed,bool skipped){
  if(!valid(changed)){return std::unexpected(PolicyError{"Invalid original file-selection intent mask"});}
  std::string out="d4:mask"+std::to_string(changed.size())+':';
  out.append(reinterpret_cast<const char*>(changed.data()),changed.size());
  out+=skipped?"7:skippedi1ee":"7:skippedi0ee";
  return out;
}
std::expected<OriginalFileSelectionIntent,PolicyError> readOriginalFileSelectionIntent(
    const core::Node& node,std::size_t files){
  if(node.type!=core::Node::Type::Dictionary||node.children.size()!=4||files>100000){return std::unexpected(PolicyError{"Invalid original file-selection intent shape"});}
  const auto* mask=node.find("mask");const auto* skipped=node.find("skipped");
  if(!mask||mask->type!=core::Node::Type::Bytes||mask->bytes.size()!=files
      ||!skipped||skipped->type!=core::Node::Type::Integer||(skipped->integer!=0&&skipped->integer!=1)){
    return std::unexpected(PolicyError{"Original file-selection intent differs from its complete census"});
  }
  OriginalFileSelectionIntent result;result.skipped=skipped->integer==1;
  result.changed.reserve(files);for(const auto value:mask->bytes){result.changed.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));}
  if(!valid(result.changed)){return std::unexpected(PolicyError{"Invalid original file-selection intent mask values"});}
  return result;
}
}
