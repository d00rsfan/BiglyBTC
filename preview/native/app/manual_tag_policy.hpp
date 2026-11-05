// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
namespace biglybtc::app {
struct ManualTagAutomation {
  bool autoAdd=false,autoRemove=false,other=false,malformed=false;
  bool addAllowed()const noexcept{return !malformed&&!autoAdd;}
  bool removeAllowed()const noexcept{return !malformed&&!autoRemove;}
};
// Value-only cb8 TagDownloadWithState::isTagAuto projection. This classifies
// saved policy; it never evaluates a constraint, resolves a taggable or writes.
// Malformed selected types refuse; unknown fields and tracker_templates remain
// inert. Original order matters: trackers, untagged, then constraint.
inline ManualTagAutomation classifyManualTagMembership(const core::Node& record)noexcept {
  const auto malformed=[] {return ManualTagAutomation{false,false,false,true};};
  if(record.type!=core::Node::Type::Dictionary){return malformed();}
  const auto* config=record.find("c");if(!config){return {};}
  if(config->type!=core::Node::Type::Dictionary){return malformed();}
  const auto validStrings=[](const core::Node* value){
    if(!value){return true;}if(value->type!=core::Node::Type::List){return false;}
    for(const auto& item:value->children){if(item.type!=core::Node::Type::Bytes){return false;}}return true;
  };
  const auto* trackers=config->find("pp.trackers");if(!validStrings(trackers)){return malformed();}
  if(trackers&&!trackers->children.empty()){return {true,true,false,false};}
  if(const auto* untagged=config->find("pp.untagged")){
    if(untagged->type!=core::Node::Type::Integer){return malformed();}
    if(untagged->integer==1){return {true,true,false,false};}
  }
  const auto* constraint=config->find("pp.constraint");if(!validStrings(constraint)){return malformed();}
  if(!constraint||constraint->children.empty()){return {};}
  if(const auto* enabled=config->find("pp.enabled.constraint")){
    if(enabled->type!=core::Node::Type::Integer){return malformed();}
    if(enabled->integer!=1){return {};}
  }
  // Java String.trim removes only U+0000..U+0020. Their UTF-8 bytes are exact;
  // every non-ASCII character (including decoded replacement) is nonempty.
  bool nonempty=false;for(const unsigned char value:constraint->children[0].bytes){if(value>0x20){nonempty=true;break;}}
  if(!nonempty){return {};}
  if(constraint->children.size()>1){
    const auto& options=constraint->children[1].bytes;
    if(options.find("am=1;")!=options.npos){return {true,false,false,false};}
    if(options.find("am=2;")!=options.npos){return {false,true,false,false};}
    if(options.find("am=3;")!=options.npos){return {false,false,true,false};}
  }
  return {true,true,false,false};
}
} // namespace biglybtc::app
