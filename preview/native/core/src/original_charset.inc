// LocaleUtilDecoderReal returns a decoded value only when Java re-encoding
// reproduces EVERY source byte. Otherwise the whole value uses UTF8 replacement.
// Recognize the exact reversible encoder language; never guess through iconv.
#include "original_charset_tables.inc"
void charsetAppend(std::string& out,std::uint32_t c){
  if(c<0x80){out+=static_cast<char>(c);}
  else if(c<0x800){out+=static_cast<char>(0xc0|(c>>6));out+=static_cast<char>(0x80|(c&63));}
  else if(c<0x10000){out+=static_cast<char>(0xe0|(c>>12));out+=static_cast<char>(0x80|((c>>6)&63));out+=static_cast<char>(0x80|(c&63));}
  else{out+=static_cast<char>(0xf0|(c>>18));out+=static_cast<char>(0x80|((c>>12)&63));out+=static_cast<char>(0x80|((c>>6)&63));out+=static_cast<char>(0x80|(c&63));}
}
std::string charsetUtf8(std::string_view input){
  std::string out;out.reserve(input.size());
  const auto byte=[&](std::size_t n){return static_cast<unsigned char>(input[n]);};
  const auto continuation=[&](std::size_t n){return (byte(n)&0xc0)==0x80;};
  for(std::size_t p=0;p<input.size();){
    const auto first=byte(p);std::size_t width=1;
    if(first<0x80){out+=input[p++];continue;}
    if(first>=0xc2&&first<=0xdf){width=2;}
    else if(first>=0xe0&&first<=0xef){width=3;}
    else if(first>=0xf0&&first<=0xf4){width=4;}
    else{charsetAppend(out,0xfffd);++p;continue;}
    std::size_t consumed=1;
    while(consumed<width&&p+consumed<input.size()&&continuation(p+consumed)){
      if(consumed==1&&((first==0xe0&&byte(p+1)<0xa0)||(first==0xf0&&byte(p+1)<0x90)||(first==0xf4&&byte(p+1)>0x8f))){break;}
      ++consumed;
    }
    if(consumed!=width){charsetAppend(out,0xfffd);p+=consumed;continue;}
    if(width==3&&first==0xed&&byte(p+1)>=0xa0){charsetAppend(out,0xfffd);p+=3;continue;}
    out.append(input.substr(p,width));p+=width;
  }
  return out;
}
std::optional<std::uint32_t> charsetPair(std::span<const CharsetPair> rows,std::uint32_t key){
  const auto found=std::ranges::lower_bound(rows,key,{},&CharsetPair::key);
  return found!=rows.end()&&found->key==key?std::optional(found->scalar):std::nullopt;
}
std::optional<std::size_t> charsetIndex(std::string_view name,bool showAll=false){
  std::string lower(name);std::ranges::transform(lower,lower.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);});
  const auto found=std::ranges::lower_bound(CharsetAliases,lower,{},&CharsetAlias::name);
  if(found==CharsetAliases.end()||found->name!=lower||(!showAll&&found->index==8)){return {};}
  return found->index;
}
std::optional<std::string> charsetStateless(std::string_view input,std::size_t index){
  const auto& map=CharsetMaps[index];std::string result;
  const auto byte=[&](std::size_t n){return static_cast<unsigned char>(input[n]);};
  for(std::size_t p=0;p<input.size();){
    if(const auto scalar=charsetPair(map.one,byte(p))){charsetAppend(result,*scalar);++p;continue;}
    const std::size_t width=index==1&&byte(p)==0x8f?3:index==3&&p+1<input.size()&&byte(p+1)>=0x30&&byte(p+1)<=0x39?4:2;
    if(input.size()-p<width){return {};}
    std::uint32_t key=0;for(std::size_t n=0;n<width;++n){key=(key<<8)|byte(p+n);}
    std::optional<std::uint32_t> scalar;
    if(width==4){
      const auto a=byte(p),b=byte(p+1),c=byte(p+2),d=byte(p+3);
      if(a<0x81||a>0xfe||b<0x30||b>0x39||c<0x81||c>0xfe||d<0x30||d>0x39){return {};}
      key=(((a-0x81)*10+b-0x30)*126+c-0x81)*10+d-0x30;
      auto found=std::ranges::upper_bound(map.four,key,{},&CharsetRange::first);
      if(found!=map.four.begin()){--found;if(key<=found->last){scalar=found->scalar+key-found->first;}}
    }else{scalar=charsetPair(width==2?map.two:map.three,key);}
    if(!scalar){return {};}charsetAppend(result,*scalar);p+=width;
  }
  return result;
}
std::optional<std::string> charsetJapanese(std::string_view input){
  std::string result,reverse;unsigned state=0,encoded=0;
  const auto byte=[&](std::size_t n){return static_cast<unsigned char>(input[n]);};
  for(std::size_t p=0;p<input.size();){
    if(byte(p)==0x1b){
      if(input.size()-p<3){return {};}
      if(input.substr(p,3)=="\x1b(B"){state=0;}
      else if(input.substr(p,3)=="\x1b(J"){state=0x4a;}
      else if(input.substr(p,3)=="\x1b(I"){state=0x49;}
      else if(input.substr(p,3)=="\x1b$B"){state=0x24;}
      else{return {};}p+=3;continue;
    }
    const std::size_t width=state==0x24?2:1;
    if(input.size()-p<width||byte(p)==0x0e||byte(p)==0x0f){return {};}
    std::uint32_t key=state<<16;
    if(width==2){key|=(byte(p)<<8)|byte(p+1);}else{key|=byte(p);}
    const auto scalar=charsetPair(CharsetMaps[9].one,key);if(!scalar){return {};}
    if(encoded!=state){reverse+=state==0?"\x1b(B":state==0x24?"\x1b$B":state==0x4a?"\x1b(J":"\x1b(I";encoded=state;}
    reverse.append(input.substr(p,width));charsetAppend(result,*scalar);p+=width;
  }
  if(encoded){reverse+="\x1b(B";}
  return reverse==input?std::optional(std::move(result)):std::nullopt;
}
std::optional<std::string> charsetKorean(std::string_view input){
  std::string result,reverse;bool shift=false,encodedShift=false,defined=false;
  const auto byte=[&](std::size_t n){return static_cast<unsigned char>(input[n]);};
  for(std::size_t p=0;p<input.size();){
    if(byte(p)==0x1b){if(input.substr(p,4)!="\x1b$)C"){return {};}p+=4;continue;}
    if(byte(p)==0x0e||byte(p)==0x0f){shift=byte(p)==0x0e;++p;continue;}
    const std::size_t width=shift?2:1;if(input.size()-p<width){return {};}
    const std::uint32_t key=shift?(byte(p)<<8)|byte(p+1):byte(p);
    const auto scalar=charsetPair(CharsetMaps[10].one,key);if(!scalar){return {};}
    if(shift){if(!defined){reverse+="\x1b$)C";defined=true;}if(!encodedShift){reverse+='\x0e';encodedShift=true;}}
    else{if(encodedShift){reverse+='\x0f';encodedShift=false;}if(key=='\n'){defined=false;}}
    reverse.append(input.substr(p,width));charsetAppend(result,*scalar);p+=width;
  }
  return reverse==input?std::optional(std::move(result)):std::nullopt;
}
std::string charsetDecode(std::string_view input,std::size_t index){
  auto decoded=index==9?charsetJapanese(input):index==10?charsetKorean(input):charsetStateless(input,index);
  return decoded?std::move(*decoded):charsetUtf8(input);
}
