// SPDX-License-Identifier: GPL-2.0-or-later
#include "tracker.hpp"
#include <algorithm>
namespace biglybtc::services::i2p::i2cp {
namespace {
using Code=::biglybtc::services::i2p::ErrorCode;
auto bad(const char* why){return std::unexpected(::biglybtc::services::i2p::Error{Code::InvalidFormat,why});}
void put(Bytes& out,std::uint64_t n,unsigned count){for(unsigned i=count;i>0;--i){out.push_back(static_cast<Byte>(n>>(8*(i-1))));}}
std::uint64_t get(View raw,std::size_t at,unsigned count){std::uint64_t n=0;for(unsigned i=0;i<count;++i){n=(n<<8)|raw[at+i];}return n;}
Bytes framed(Byte type,Bytes body){Bytes out;out.reserve(5+body.size());put(out,body.size(),4);out.push_back(type);out.insert(out.end(),body.begin(),body.end());return out;}
}
ConfigResult<Bytes> encodeHostLookup(const HostLookup& input){if(input.sessionId==65535||!input.timeoutMilliseconds){return bad("Invalid bounded HostLookup fields");}Bytes b;put(b,input.sessionId,2);put(b,input.request,4);put(b,input.timeoutMilliseconds,4);b.push_back(0);b.insert(b.end(),input.hash.begin(),input.hash.end());return framed(HostLookupType,std::move(b));}
ConfigResult<HostLookup> decodeHostLookup(const Frame& frame){if(frame.type!=HostLookupType||frame.payload.size()!=43||frame.payload[10]!=0){return bad("Only exact original hash lookup38 supported");}const View b=frame.payload;HostLookup out;out.sessionId=static_cast<std::uint16_t>(get(b,0,2));out.request=static_cast<std::uint32_t>(get(b,2,4));out.timeoutMilliseconds=static_cast<std::uint32_t>(get(b,6,4));if(out.sessionId==65535||!out.timeoutMilliseconds){return bad("Invalid HostLookup fields");}std::copy(b.begin()+11,b.end(),out.hash.begin());return out;}
ConfigResult<Bytes> encodeHostReply(const HostReply& input){if(input.sessionId==65535||(input.result==0)!=input.destination.has_value()){return bad("Invalid HostReply result/destination");}Bytes b;put(b,input.sessionId,2);put(b,input.request,4);b.push_back(input.result);if(input.destination){auto parsed=decodeDestination(input.destination->wire);if(!parsed){return std::unexpected(parsed.error());}b.insert(b.end(),parsed->wire.begin(),parsed->wire.end());}return framed(HostReplyType,std::move(b));}
ConfigResult<HostReply> decodeHostReply(const Frame& frame){if(frame.type!=HostReplyType||frame.payload.size()<7||frame.payload.size()>7+MaximumWireBytes){return bad("Bounded HostReply39 required");}const View b=frame.payload;HostReply out;out.sessionId=static_cast<std::uint16_t>(get(b,0,2));out.request=static_cast<std::uint32_t>(get(b,2,4));out.result=b[6];if(out.sessionId==65535){return bad("Unknown lookup session");}if(out.result){if(b.size()!=7){return bad("Trailing failure reply bytes");}}else{auto dest=decodeDestination(b.subspan(7));if(!dest){return std::unexpected(dest.error());}out.destination=std::move(*dest);}return out;}
} // namespace biglybtc::services::i2p::i2cp
