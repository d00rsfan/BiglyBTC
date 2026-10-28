// SPDX-License-Identifier: MPL-2.0
#include "mainline_scrape.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace biglybtc::services::mainline_scrape;
namespace {
void require(bool ok,const char* message){if(!ok){throw std::runtime_error(message);}}
std::vector<std::string_view> split(std::string_view s,char delimiter){std::vector<std::string_view> out;do{const auto at=s.find(delimiter);out.push_back(s.substr(0,at));if(at==s.npos){break;}s.remove_prefix(at+1);}while(true);return out;}
unsigned number(std::string_view text){unsigned n=0;const auto [p,ec]=std::from_chars(text.data(),text.data()+text.size(),n);require(ec==std::errc{}&&p==text.data()+text.size(),"integer");return n;}
std::string unhex(std::string_view input){require(input.size()%2==0,"hex size");std::string out;for(std::size_t i=0;i<input.size();i+=2){unsigned n=0;const auto [p,ec]=std::from_chars(input.data()+i,input.data()+i+2,n,16);require(ec==std::errc{}&&p==input.data()+i+2,"hex");out.push_back(static_cast<char>(n));}return out;}
std::string hex(std::span<const std::uint8_t> raw){static constexpr char digits[]="0123456789abcdef";std::string result;for(auto b:raw){result+=digits[b>>4];result+=digits[b&15];}return result;}
std::string bytes(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
Address addr(std::string_view h){const auto raw=unhex(h);const auto a=address({reinterpret_cast<const std::uint8_t*>(raw.data()),raw.size()});require(a.has_value(),"address");return *a;}
Bloom populated(unsigned count){require(count<=Bloom::Bits,"bits bound");std::string raw(Bloom::Bytes,'\0');for(unsigned i=0;i<count;++i){raw[i/8]=static_cast<char>(static_cast<unsigned char>(raw[i/8])|(1U<<(i%8)));}return *Bloom::parse(raw);}
std::string body(std::string extra=""){return "d2:id20:"+std::string(20,'I')+extra+"e";}
Response response(std::string_view spec){const auto fields=split(spec,'/');require(fields.size()==3,"response fields");std::string extra="5:nodes0:";
 if(fields[0]!="-"){extra+="4:BFse"+bytes(unhex(fields[0]));}if(fields[1]!="-"){extra+="4:BFpe"+bytes(unhex(fields[1]));}
 if(fields[2]!="-"){extra+="6:valuesl";for(auto peer:split(fields[2],'+')){extra+=bytes(unhex(peer));}extra+="e";}
 auto parsed=decode(body(extra));require(parsed.has_value(),"response decode");return std::move(*parsed);
}
void replay(const char* path){std::ifstream input(path);require(input.good(),"input");std::string line;std::size_t rows=0;
 while(std::getline(input,line)){require(++rows<=4096&&line.size()<=131072,"tape bound");const auto fields=split(line,'\t');require(fields.size()==3,"tape columns");const auto op=fields[0],id=fields[1],data=fields[2];require(!id.empty()&&id.size()<=64,"id");
  if(op=="B"||op=="H"||op=="I"){Bloom b;
   if(op=="B"){b=populated(number(data));}else if(op=="H"){auto parsed=Bloom::parse(unhex(data));require(parsed.has_value(),"serialized bloom");b=*parsed;}
   else if(data!="-"){for(auto h:split(data,',')){require(b.insert(addr(h)).has_value(),"insert");}}
   std::cout<<"B\t"<<id<<'\t'<<hex(b.bytes())<<'\t'<<b.estimate()<<'\n';
  }else if(op=="A"){Aggregate all;if(data!="-"){for(auto s:split(data,';')){require(all.add(response(s)).has_value(),"aggregate");}}
   const auto summary=all.finish();require(summary.has_value(),"finish");std::cout<<"A\t"<<id<<'\t'<<summary->seeds<<'\t'<<summary->peers<<'\t'<<summary->direct<<'\n';
  }else{throw std::runtime_error("tape opcode");}
 }require(input.eof(),"tape read");
}
void codecAndNumerical(){
 require(Bloom{}.estimate()==0,"empty estimate");require(populated(2048).estimate()==std::numeric_limits<std::int32_t>::max(),"saturated Java conversion");
 require(!Bloom::parse(std::string(255,'\0'))&&!Bloom::parse(std::string(257,'\0')),"filter lengths");
 Bloom one;require(one.insert(addr("08080808")).has_value(),"numeric insert");const auto same=one;require(one.insert(addr("08080808")).has_value()&&one.bytes()==same.bytes(),"duplicate idempotence");
 Bloom mapped;require(mapped.insert(addr("00000000000000000000ffff08080808")).has_value()&&mapped.bytes()==one.bytes(),"mapped InetAddress normalization");
 Bloom unioned=one;unioned.merge(one);require(unioned.bytes()==one.bytes(),"union idempotence");
 require(!permittedDirect(addr("7f000001"),6881)&&!permittedDirect(addr("0a000001"),6881)&&!permittedDirect(addr("fe800000000000000000000000000001"),6881),"source bogons");
 require(permittedDirect(addr("fc000000000000000000000000000001"),6881)&&permittedDirect(addr("ffffffff"),6881),"source predicate is not an invented reserved-range list");
 require(!permittedDirect(addr("08080808"),0),"zero port");
}
void dialectAndDecoder(){
 const std::string raw(256,'\xff');const auto se=body("5:nodes0:4:BFpe"+bytes(raw)+"4:BFse"+bytes(raw));
 require(decode(se).has_value(),"original dialect");require(!decode(se,Dialect::LibtorrentBFsd),"wrong explicit dialect");
 const auto sd=body("5:nodes0:4:BFsd"+bytes(raw));require(decode(sd,Dialect::LibtorrentBFsd).has_value()&&!decode(sd),"libtorrent alias explicit only");
 require(!decode(body("5:nodes0:4:BFse"+bytes(raw)+"4:BFsd"+bytes(raw)),Dialect::LibtorrentBFsd),"mixed dialect ambiguous even equal");
 require(!decode(body("5:nodes0:4:BFpe255:"+std::string(255,'x'))),"malformed filter");
 require(!decode(body())&&!decode(body("6:valuesle")),"filters/empty values alone not a source carrier");
 require(decode(body("6:valuesl1:xe")).has_value(),"nonempty invalid-length values still supplies source carrier");
 require(!decode(body("5:nodes1:x"))&&!decode(body("5:nodes0:6:valuesi1e")),"node geometry and values type");
 require(!decode(body("5:nodes0:5:nodes0:")),"duplicate keys refused");
 require(!decode(std::string(16385,'x')),"wire bound");
 auto direct=decode(body("5:nodes0:6:valuesl6:"+unhex("080808081ae1")+"6:"+unhex("080808081ae2")+"6:"+unhex("7f0000011ae1")+"e"));
 require(direct.has_value()&&direct->direct.size()==2,"ports filter before address dedup");
 Aggregate a;require(a.add(*direct).has_value(),"direct aggregate");auto first=a.finish();require(first&&first->direct==1&&first->peers==1,"addresses not ports");
}
void boundedTerminalAggregation(){
 const auto r=response("-/-/080808081ae1+010101011ae1");Aggregate a,b;
 require(a.add(r).has_value()&&a.add(r).has_value()&&b.add(r).has_value(),"responses");a.cancel();require(!a.finish()&&!a.add(r),"cancel terminal");
 auto result=b.finish();require(result&&result->direct==2&&result->responses==1,"independent owner unaffected");require(!b.add(r)&&!b.finish(),"finish terminal");
 Aggregate limited;for(std::size_t i=0;i<MaximumResponses;++i){require(limited.add(r).has_value(),"round slots");}require(!limited.add(r),"response bound");
 result=limited.finish();require(result&&result->responses==MaximumResponses&&result->direct==2,"overflow cannot mutate accepted results");
 Response oversized;oversized.direct.resize(MaximumPeers+1);Aggregate fresh;require(!fresh.add(oversized),"direct bound");auto empty=fresh.finish();require(empty&&empty->responses==0&&empty->direct==0,"precommit failure");
}
}
int main(int argc,char** argv){try{if(argc==3&&std::string_view(argv[1])=="--replay"){replay(argv[2]);return 0;}require(argc==1,"arguments");codecAndNumerical();dialectAndDecoder();boundedTerminalAggregation();std::cout<<"PASS codec/numerical, dialect/decoder, bounded terminal aggregation\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
