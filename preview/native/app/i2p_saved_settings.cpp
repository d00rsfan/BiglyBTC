// SPDX-License-Identifier: GPL-2.0-or-later
#include "i2p_saved_settings.hpp"
#include "plugin_settings.hpp"
#include <algorithm>
#include <array>
namespace biglybtc::app {
std::expected<I2pServiceOptions,storage::Error> readSavedI2pOptions(const core::Document& config,I2pServiceOptions options){
  auto binding=storage::originalPluginBinding("azneti2phelper","azneti2phelper");
  if(!binding){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,binding.error().message});}
  storage::OriginalPluginSettings saved(config,std::move(*binding));
  struct Setting {const char* name;const char* property;int fallback,minimum,maximum;};
  constexpr std::array<Setting,6> fields{{
    {"azi2phelper.i2p.inbound.hops","inbound.length",3,0,7},
    {"azi2phelper.i2p.outbound.hops","outbound.length",3,0,7},
    {"azi2phelper.i2p.inbound.quantity","inbound.quantity",2,1,16},
    {"azi2phelper.i2p.outbound.quantity","outbound.quantity",2,1,16},
    {"azi2phelper.i2p.inbound.var","inbound.lengthVariance",0,-3,3},
    {"azi2phelper.i2p.outbound.var","outbound.lengthVariance",0,-3,3}}};
  for(const auto& field:fields){
    const auto value=saved.integer(field.name,field.fallback);
    if(value<field.minimum||value>field.maximum){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,std::string("Unsupported saved I2P setting: ")+field.name});}
    if(std::ranges::none_of(options.sessionOptions,[&](const auto& entry){return entry.first==field.property;})){
      options.sessionOptions.emplace_back(field.property,std::to_string(value));
    }
  }
  if(saved.boolean("azi2phelper.use.ext",false)){
    const auto* host=config.find("Plugin.azneti2phelper.azi2phelper.use.ext.host");
    if(host&&(host->type!=core::Node::Type::Bytes||host->bytes!="127.0.0.1")){
      return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"This private I2P preview requires the saved external router host to be 127.0.0.1"});
    }
  }
  if(!saved.wrongTypes().empty()){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Malformed original I2P setting: "+saved.wrongTypes().front()});}
  return options;
}
}
