// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/bencode.hpp>
#include <cstdint>
namespace biglybtc::ui {
struct ClassicStatusSettings {
 bool shareRatio=true,nat=true,dht=true,ipFilter=true,networkInfo=false;
 bool dataOnly=false,separateProtocol=false;
};
inline ClassicStatusSettings readClassicStatusSettings(const core::Document* document){
 ClassicStatusSettings out;if(!document){return out;}
 const auto flag=[&](std::string_view key,bool fallback){const auto* v=document->find(key);return v&&v->type==core::Node::Type::Integer?static_cast<std::uint32_t>(v->integer)!=0:fallback;};
 out.shareRatio=flag("Status Area Show SR",true);out.nat=flag("Status Area Show NAT",true);out.dht=flag("Status Area Show DDB",true);out.ipFilter=flag("Status Area Show IPF",true);out.networkInfo=flag("Status Area Show NI",false);
 out.dataOnly=flag("config.style.dataStatsOnly",false);out.separateProtocol=flag("config.style.separateProtDataStats",false);return out;
}
}
