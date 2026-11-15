// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/bencode.hpp>
#include <array>
#include <bit>
namespace biglybtc::ui {
struct TagStripSettings {
 bool categories=false,tags=true,categoriesCompletedOnly=false,tagsCompletedOnly=false;
 bool filtersOnly=false,imageOverride=false,inclusive=true;std::int32_t alignment=0;
 bool operator==(const TagStripSettings&)const=default;
};
inline constexpr std::array TagStripBooleanKeys{
 std::pair{"Library.ShowCatButtons",&TagStripSettings::categories},
 std::pair{"Library.ShowTagButtons",&TagStripSettings::tags},
 std::pair{"Library.ShowCatButtons.CompOnly",&TagStripSettings::categoriesCompletedOnly},
 std::pair{"Library.ShowTagButtons.CompOnly",&TagStripSettings::tagsCompletedOnly},
 std::pair{"Library.ShowTagButtons.FiltersOnly",&TagStripSettings::filtersOnly},
 std::pair{"Library.ShowTagButtons.ImageOverride",&TagStripSettings::imageOverride},
 std::pair{"Library.ShowTagButtons.Inclusive",&TagStripSettings::inclusive}};
inline TagStripSettings readTagStripSettings(const core::Document* document){
 TagStripSettings out;if(!document){return out;}for(const auto& [key,member]:TagStripBooleanKeys){if(const auto* value=document->find(key);value&&value->type==core::Node::Type::Integer){out.*member=static_cast<std::uint32_t>(value->integer)!=0;}}
 if(const auto* value=document->find("Library.ShowTagButtons.Align");value&&value->type==core::Node::Type::Integer){out.alignment=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value->integer));}return out;
}
enum class TagStripSettingsError {Conflict,Format};
// Pure original-field edit; the profile owner supplies the primary source/CAS.
inline std::expected<bool,TagStripSettingsError> editTagStripSettings(core::Document& document,const TagStripSettings& expected,const TagStripSettings& value){
 if(document.root().type!=core::Node::Type::Dictionary){return std::unexpected(TagStripSettingsError::Format);}if(readTagStripSettings(&document)!=expected){return std::unexpected(TagStripSettingsError::Conflict);}if(expected==value){return false;}
 auto updated=document;
 for(const auto& [key,member]:TagStripBooleanKeys){if(expected.*member!=value.*member&&!updated.replaceValue(key,value.*member?"i1e":"i0e")){return std::unexpected(TagStripSettingsError::Format);}}
 if(expected.alignment!=value.alignment&&!updated.replaceValue("Library.ShowTagButtons.Align","i"+std::to_string(value.alignment)+"e")){return std::unexpected(TagStripSettingsError::Format);}document=std::move(updated);return true;
}
}
