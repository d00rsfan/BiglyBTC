// SPDX-License-Identifier: GPL-2.0-or-later
#include "related_content.hpp"
#include "../services/related_content/cache_slices.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>
#include <stdexcept>
using namespace biglybtc;namespace rc=services::related_content;
namespace {void need(bool value,const char* text){if(!value){throw std::runtime_error(text);}}std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}std::string hash(int index){std::string out(20,'G');for(int i=0;i<4;++i){out[i]=char(std::uint32_t(index)>>(8*i));}return out;}}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 constexpr int Count=100005;std::string records="l",links="d";for(int i=0;i<Count;++i){const auto key=hash(i+1),name=rc::base32(key);std::string row="d2:_ii"+std::to_string(i)+"e2:_k"+b(name)+"1:d"+b("Generated discovery")+"1:h"+b(key);for(int f=0;f<15;++f){row+=b("generated-"+std::to_string(f))+b("Generated preserved field");}row+='e';records+=row;std::string index;for(int shift=24;shift>=0;shift-=8){index+=char(std::uint32_t(i)>>shift);}links+=b(name)+b(index);}records+='e';links+='e';const auto input="d2:rc"+records+"3:rcm"+links+b("unknown")+b("Generated preserved")+"e";
 QElapsedTimer timer;timer.start();auto catalog=rc::Catalog::decode(input,"Generated key");need(bool(catalog),catalog?"":catalog.error().message.c_str());need(catalog->size()==Count,"Large original cache survives the former item/node bounds");auto rows=catalog->items();need(rows.front().relatedTo.size()==1&&rows.back().relatedTo.size()==1,"Large relationship maps resolve");rows.clear();const auto encoded=catalog->encode("Generated key");need(bool(encoded),encoded?"":encoded.error().message.c_str());need(encoded->find("Generated preserved field")!=std::string::npos&&encoded->find("Generated preserved")!=std::string::npos,"Unknown original fields survive");catalog=rc::Catalog::decode("de","Generated key");auto reopened=rc::Catalog::decode(*encoded,"Generated key");need(reopened&&reopened->size()==Count,"Large encode/decode does not build a whole recursive tree");
 for(const auto invalid:{"deJUNK","d1:ali1eeeJUNK","d1:ai-0ee","d1:ai01ee","d1:a1:x1:a1:ye","d1:al1:xe"}){bool rejected=false;try{rc::detail::CacheSlices(invalid).dictionary();}catch(...){rejected=true;}need(rejected,"Malformed streaming structure rejected");}
 std::cout<<"PASS generated large discoveries: "<<Count<<" rows; preserved fields/relationships/roundtrip/malformed controls; "<<timer.elapsed()<<" ms\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
