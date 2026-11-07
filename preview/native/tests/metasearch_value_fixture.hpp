// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>
namespace metasearch_value_fixture {
inline std::string b(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
inline std::string mapping(std::string_view name,int field,std::string extra={}){return "d"+b("name")+b(name)+b("field")+"i"+std::to_string(field)+"e"+extra+"e";}
inline std::string engine(int id=7,std::string uid="owned-json",std::string extra={},std::string maps={}){
 if(maps.empty()){maps="l"+mapping("title",1)+"e";}
 return "d"+b("id")+"i"+std::to_string(id)+"e"+b("uid")+b(uid)+b("type")+"i2e"+b("selected")+"i2e"+b("name")+b("Owned")+b("web.search_url_format")+b("https://feeds.example.org/search/%s?q=%s")+b("web.maps")+maps+extra+"e";
}
inline std::string catalog(const std::string& rows){return "d"+b("engines")+"l"+rows+"e"+b("future")+b("keep")+ "e";}
}
