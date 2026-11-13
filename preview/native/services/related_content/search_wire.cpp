// SPDX-License-Identifier: GPL-2.0-or-later
#include "search_wire.hpp"
#include <QCryptographicHash>
#include <QHostAddress>
#include <QStringDecoder>
#include <limits>
namespace biglybtc::services::related_content {
namespace {
using core::Node;using core::Document;
auto fail(const char* text){return std::unexpected(Error{text});}
std::string b(std::string_view text){return std::to_string(text.size())+":"+std::string(text);}
std::string n(std::int64_t value){return "i"+std::to_string(value)+"e";}
bool unique(const Node& node){if(node.type==Node::Type::Dictionary){for(std::size_t i=0;i<node.children.size();i+=2){for(std::size_t j=0;j<i;j+=2){if(node.children[i].bytes==node.children[j].bytes){return false;}}}}for(const auto& child:node.children){if(!unique(child)){return false;}}return true;}
std::optional<std::int64_t> integer(const Node& node,std::string_view key){const auto* p=node.find(key);if(!p||p->type!=Node::Type::Integer){return {};}return p->integer;}
std::optional<std::string> bytes(const Node& node,std::string_view key){const auto* p=node.find(key);if(!p||p->type!=Node::Type::Bytes){return {};}return std::string(p->bytes);}
bool utf(std::string_view text){QStringDecoder decoder(QStringDecoder::Utf8);const QString value=decoder(QByteArrayView(text.data(),qsizetype(text.size())));(void)value;return !decoder.hasError();}
Result<Document> document(std::string raw,std::size_t limit){if(raw.size()>limit){return fail("Related search message exceeds its bound");}auto doc=Document::parse(std::move(raw));if(!doc||doc->root().type!=Node::Type::Dictionary||!unique(doc->root())){return fail("Invalid related search dictionary");}return std::move(*doc);}
bool address(std::string_view text){return text.size()<=64&&!text.contains('%')&&!QHostAddress(QString::fromUtf8(text.data(),qsizetype(text.size()))).isNull();}
}
std::string searchHandler(bool bigly){const QByteArray name=bigly?"com.biglybt.core.content.RelatedContentManager$RCMSearchXFerBiglyBT":"com.aelitis.azureus.core.content.RelatedContentManager$RCMSearchXFer";return QCryptographicHash::hash(name,QCryptographicHash::Sha1).toStdString();}
Result<std::string> encodeSearchRequest(const SearchMessage& message){
 std::string out="d";if(message.operation==SearchOperation::Query){auto query=SearchQuery::compile(message.query);if(!query){return std::unexpected(query.error());}if(message.query.maximumAgeSeconds>0){out+=b("a")+n(message.query.maximumAgeSeconds);}if(message.query.minimumLeechers>0){out+=b("l")+n(message.query.minimumLeechers);}if(message.query.minimumSeeds>0){out+=b("s")+n(message.query.minimumSeeds);}out+=b("t")+b(query->normalisedTerm());}
 else{if(message.operation==SearchOperation::UpdateBloom){if(message.bloomCount<0||message.bloomCount>6251){return fail("Invalid search Bloom update count");}out+=b("s")+n(message.bloomCount);}out+=b("x")+b(message.operation==SearchOperation::FetchBloom?"f":"u");}out+='e';if(out.size()>255){return fail("Related search exceeds original DATA key length");}return out;
}
Result<SearchMessage> decodeSearchRequest(std::string raw){
 auto doc=document(std::move(raw),255);if(!doc){return std::unexpected(doc.error());}SearchMessage out;
 if(doc->find("x")){const auto operation=bytes(doc->root(),"x");if(!operation||(*operation!="f"&&*operation!="u")||doc->find("t")){return fail("Invalid search operation");}out.operation=*operation=="f"?SearchOperation::FetchBloom:SearchOperation::UpdateBloom;if(out.operation==SearchOperation::UpdateBloom){auto count=integer(doc->root(),"s");if(!count||*count<0||*count>6251){return fail("Invalid search Bloom count");}out.bloomCount=std::int32_t(*count);}}
 else{auto term=bytes(doc->root(),"t");if(!term){return fail("Related search term missing");}out.query.term=*term;for(auto pair:{std::pair{"s",&out.query.minimumSeeds},{"l",&out.query.minimumLeechers},{"a",&out.query.maximumAgeSeconds}}){if(doc->find(pair.first)){auto value=integer(doc->root(),pair.first);if(!value||*value< -1||*value>std::numeric_limits<std::int32_t>::max()){return fail("Invalid related search filter");}*pair.second=std::int32_t(*value);}}if(!SearchQuery::compile(out.query)){return fail("Invalid related search term");}}
 return out;
}
Result<std::string> encodeSearchResponse(const SearchResponse& reply){
 if(reply.items.size()>30||reply.contacts.size()>50||(reply.bloom&&reply.unchangedCount)){return fail("Related search response count exceeds original limits");}std::string out="d";
 if(!reply.contacts.empty()){out+=b("c")+"l";for(const auto& c:reply.contacts){if(!address(c.address)||!c.port){return fail("Invalid numeric search contact");}out+="d"+b("a")+b(c.address)+b("p")+n(c.port)+"e";}out+='e';}
 if(reply.bloom){auto encoded=reply.bloom->encode();if(!encoded||reply.bloom->bits()>50001||reply.bloom->count()>6251){return fail("Invalid search Bloom response");}out+=b("f")+*encoded;}
 if(reply.resultsPresent||!reply.items.empty()){out+=b("l")+"l";for(const auto& i:reply.items){if(i.hash.size()!=20||i.title.size()>4096||!utf(i.title)||i.size<0||i.randoms.size()>100||i.trackerKeys.size()>400||i.webSeedKeys.size()>400||i.trackerKeys.size()%4||i.webSeedKeys.size()%4){return fail("Invalid search result");}out+="d"+b("d")+n(i.lastSeen);if(!i.tags.empty()){out+=b("g")+b(encodeTags(i.tags));}out+=b("h")+b(i.hash);if(!i.trackerKeys.empty()){out+=b("k")+b(i.trackerKeys);}out+=b("l")+n(i.leechers())+b("n")+b(i.title);if(i.networks&&i.networks!=1){out+=b("o")+n(i.networks);}out+=b("p")+n(i.publishedHours)+b("r")+n(i.randoms.size())+b("s")+n(i.size)+b("v")+n(i.version);if(!i.webSeedKeys.empty()){out+=b("w")+b(i.webSeedKeys);}out+=b("z")+n(i.seeds())+"e";}out+='e';}
 if(reply.unchangedCount){if(*reply.unchangedCount<0||*reply.unchangedCount>6251){return fail("Invalid unchanged Bloom count");}out+=b("s")+n(*reply.unchangedCount);}out+='e';if(out.size()>16384){return fail("Related search response exceeds DATA budget");}return out;
}
Result<SearchResponse> decodeSearchResponse(std::string raw){
 auto doc=document(std::move(raw),16384);if(!doc){return std::unexpected(doc.error());}SearchResponse out;
 if(const auto* filter=doc->find("f")){auto bloom=SearchBloom::decodeForRelatedSearch(*filter);if(!bloom){return fail("Invalid remote search Bloom");}out.bloom=std::move(*bloom);}
 if(doc->find("s")){auto count=integer(doc->root(),"s");if(!count||*count<0||*count>6251||out.bloom){return fail("Invalid remote Bloom acknowledgement");}out.unchangedCount=std::int32_t(*count);}
 if(const auto* list=doc->find("c")){if(list->type!=Node::Type::List||list->children.size()>50){return fail("Search contact count exceeded");}for(const auto& row:list->children){if(row.type!=Node::Type::Dictionary){return fail("Invalid search contact");}auto ip=bytes(row,"a");auto port=integer(row,"p");if(!ip){if(row.find("m")){continue;}return fail("Missing search contact");}if(!port||!address(*ip)||*port<1||*port>65535){return fail("Invalid search contact address");}out.contacts.push_back({*ip,std::uint16_t(*port)});}}
 if(const auto* list=doc->find("l")){out.resultsPresent=true;if(list->type!=Node::Type::List||list->children.size()>30){return fail("Search result count exceeded");}for(const auto& row:list->children){if(row.type!=Node::Type::Dictionary){return fail("Invalid search result map");}auto hash=bytes(row,"h");if(!hash){continue;}auto title=bytes(row,"n");if(hash->size()!=20||!title||title->size()>4096||!utf(*title)){return fail("Invalid search result identity");}Item i;i.hash=*hash;i.key=base32(i.hash);i.title=*title;
 auto number=[&](std::string_view key,std::int64_t low,std::int64_t high,std::int64_t fallback)->Result<std::int64_t>{if(!row.find(key)){return fallback;}auto value=integer(row,key);if(!value||*value<low||*value>high){return fail("Invalid search result number");}return *value;};
 auto version=number("v",0,2147483647,0),size=number("s",0,std::numeric_limits<std::int64_t>::max(),0),rank=number("r",0,100,0),seen=number("d",0,2147483647,0),published=number("p",0,2147483647,0),seeds=number("z",-1,65535,-1),peers=number("l",-1,65535,-1),nets=number("o",0,255,1);if(!version||!size||!rank||!seen||!published||!seeds||!peers||!nets){return fail("Invalid related result counters");}i.version=std::int32_t(*version);i.size=*size;i.randoms.resize(std::size_t(*rank));i.lastSeen=std::int32_t(*seen);i.publishedHours=std::int32_t(*published);i.networks=std::uint8_t(*nets);i.packedPeers=*seeds<0||*peers<0?-1:std::int32_t((std::uint32_t(*seeds)<<16)|std::uint32_t(*peers));
 for(auto pair:{std::pair{"k",&i.trackerKeys},{"w",&i.webSeedKeys}}){if(row.find(pair.first)){auto value=bytes(row,pair.first);if(!value||value->size()>400||value->size()%4){return fail("Invalid result source fingerprints");}*pair.second=*value;}}if(row.find("g")){auto value=bytes(row,"g");if(!value||value->size()>512){return fail("Invalid result tags");}i.tags=decodeTags(*value);}out.items.push_back(std::move(i));}}
 return out;
}
}
