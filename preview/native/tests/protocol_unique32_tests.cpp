// SPDX-License-Identifier: GPL-2.0-or-later
#include "unique32.hpp"
#include "detail/aes_rounds.hpp"
#include "detail/swap_or_not.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Test-executable-only allocation failure. No production factory or channel
// gets a supplied key, cursor, RNG, allocator, grant or error injection hook.
namespace {std::atomic<bool> failNextNew=false;}
[[gnu::noinline]] void* operator new(std::size_t size){
  if(failNextNew.exchange(false)){throw std::bad_alloc();}
  if(void* value=std::malloc(size?size:1)){return value;}throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* value) noexcept{std::free(value);}
[[gnu::noinline]] void operator delete(void* value,std::size_t) noexcept{std::free(value);}
namespace p=biglybtc::services::protocol_ids;namespace d=p::detail;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,p::Error> result){check(bool(result),"Unexpected allocator error");return std::move(*result);}
template<class T>void error(const std::expected<T,p::Error>& result,p::Error expected,const char* why){check(!result&&result.error()==expected,why);}
unsigned hex(char c){if(c>='0'&&c<='9'){return static_cast<unsigned>(c-'0');}if(c>='a'&&c<='f'){return static_cast<unsigned>(c-'a'+10);}throw std::runtime_error("Malformed reference hex");}
std::uint32_t word(const std::string& text){check(text.size()==8,"Malformed reference word");std::uint32_t result=0;for(const auto c:text){result=(result<<4)|hex(c);}return result;}

void smallDomainsAreBijectionsWithExactReverseRounds(){
  d::AesRounds::Key key{};for(unsigned i=0;i<key.size();++i){key[i]=static_cast<unsigned char>(i);}
  for(unsigned width=1;width<=8;++width){
    auto rounds=take(d::AesRounds::create(key,width));
    std::vector<bool> seen(std::size_t{1}<<width,false);
    for(std::uint32_t input=0;input<seen.size();++input){
      const auto output=take(rounds.apply(input));check(output<seen.size()&&!seen[output],"Round loop is not a bijection");seen[output]=true;
      check(take(rounds.apply(output,true))==input,"Reverse round order is not the inverse");
    }
    check(std::all_of(seen.begin(),seen.end(),[](bool x){return x;}),"Permutation omits a domain element");
  }
  const std::array<std::uint32_t,1> pivot{5};
  auto bit=[](std::size_t,std::uint32_t pair)->std::expected<bool,p::Error>{return (pair&1u)!=0;};
  for(std::uint32_t input=0;input<8;++input){const auto once=take(d::transform(input,3,pivot,bit));check(take(d::transform(once,3,pivot,bit))==input,"An individual swap round is not an involution");}
  error(d::AesRounds::create(key,0),p::Error::Failed,"Zero domain width accepted");
  error(d::AesRounds::create(key,33),p::Error::Failed,"Oversized domain width accepted");
}

void fixed32BitOutputsMatchIndependentPythonOpenSsl(const char* path){
  std::ifstream input(path);check(bool(input),"Generate the independently implemented reference first");
  std::string line;unsigned count=0;std::set<std::pair<std::string,std::uint32_t>> unique;
  while(std::getline(input,line)){
    check(line.size()<=200,"Unbounded reference line");if(line.empty()||line[0]=='#'){continue;}
    std::istringstream row(line);std::string keyHex,inHex,outHex,extra;
    check(bool(row>>keyHex>>inHex>>outHex)&&!(row>>extra)&&keyHex.size()==64,"Malformed reference row");
    d::AesRounds::Key key{};for(std::size_t i=0;i<key.size();++i){key[i]=static_cast<unsigned char>((hex(keyHex[2*i])<<4)|hex(keyHex[2*i+1]));}
    const auto before=word(inHex),after=word(outHex);check(unique.emplace(keyHex,before).second,"Duplicate reference input");
    auto rounds=take(d::AesRounds::create(key));
    check(take(rounds.apply(before))==after,"C++ disagrees with independent 32-bit golden output");
    check(take(rounds.apply(after,true))==before,"Actual 32-bit inverse disagrees");
    check(++count<=26,"Too many reference cases");
  }
  check(input.eof()&&count==26,"Incomplete two-key/boundary reference vectors");
}

void cursorCannotWrapAndZeroConsumesItsInput(){
  auto cursor=p::MaximumInputs-1;
  check(take(d::reserve(cursor))==0x7fffffffu&&cursor==p::MaximumInputs,"Final permitted input is wrong");
  error(d::reserve(cursor),p::Error::Exhausted,"Half-domain budget wrapped");check(cursor==p::MaximumInputs,"Exhaustion changed cursor");
  cursor=std::numeric_limits<std::uint64_t>::max();error(d::reserve(cursor),p::Error::Exhausted,"UINT64_MAX wrapped");check(cursor==std::numeric_limits<std::uint64_t>::max(),"Overflow changed cursor");
  d::Sequence tiny(3);auto identity=[](std::uint32_t x)->std::expected<std::uint32_t,p::Error>{return x;};
  check(take(tiny.next(p::Zero::Skip,identity))==1&&tiny.consumed()==2,"Skipped zero did not consume its input");
  check(take(tiny.next(p::Zero::Skip,identity))==2,"Sequential input was lost");error(tiny.next(p::Zero::Skip,identity),p::Error::Exhausted,"Small budget wrapped");
  d::Sequence onlyZero(1);error(onlyZero.next(p::Zero::Skip,identity),p::Error::Exhausted,"Final zero fabricated an output");check(onlyZero.consumed()==1,"Final zero was not consumed");
  d::Sequence allow(1);check(take(allow.next(p::Zero::Allow,identity))==0,"Allow-zero policy silently changed");
}

void resourceAndCryptoFailureNeverReplayAnInput(){
  for(const auto cause:{p::Error::Resource,p::Error::Crypto}){
    d::Sequence sequence(4);unsigned calls=0;
    auto fail=[&](std::uint32_t)->std::expected<std::uint32_t,p::Error>{++calls;return std::unexpected(cause);};
    error(sequence.next(p::Zero::Skip,fail),cause,"Initial error not preserved");
    check(sequence.consumed()==1&&sequence.failed(),"Error did not consume and poison reserved input");
    error(sequence.next(p::Zero::Skip,fail),p::Error::Failed,"Poisoned lifetime retried");check(calls==1&&sequence.consumed()==1,"Poisoned lifetime repeated crypto");
  }
  failNextNew=true;auto allocation=p::Unique32::create();failNextNew=false;
  error(allocation,p::Error::Resource,"Actual public factory allocation failure escaped");
  auto lazy=take(p::Unique32::create());
  failNextNew=true;auto failed=lazy.next();failNextNew=false;
  error(failed,p::Error::Resource,"Actual lazy round-cache allocation failure escaped");
  const auto state=take(lazy.statistics());check(state.failed&&state.reservedInputs==1&&!state.initialized,"Lazy failure was not permanently recorded");
  error(lazy.next(),p::Error::Failed,"Actual failed allocator rekeyed/retried");
}

void publicFactoryIsLazyMoveOnlyAndThreadAffine(){
  static_assert(!std::is_copy_constructible_v<p::Unique32>&&!std::is_copy_assignable_v<p::Unique32>);
  auto allocator=take(p::Unique32::create());auto before=take(allocator.statistics());
  check(!before.initialized&&before.reservedInputs==0&&!before.failed,"Factory eagerly used secret/ID inputs");
  std::optional<p::Error> foreignNext,foreignStats;
  std::thread foreign([&]{auto next=allocator.next();if(!next){foreignNext=next.error();}auto stats=allocator.statistics();if(!stats){foreignStats=stats.error();}});foreign.join();
  check(foreignNext==p::Error::Thread&&foreignStats==p::Error::Thread,"Foreign thread accepted allocator use");
  check(take(allocator.statistics()).reservedInputs==0,"Foreign thread consumed an input");
  std::set<std::uint32_t> outputs;
  for(unsigned i=0;i<2048;++i){const auto value=take(allocator.next());check(value!=0&&outputs.insert(value).second,"Public allocator repeated or emitted forbidden zero");}
  const auto count=take(allocator.statistics()).reservedInputs;
  auto moved=std::move(allocator);error(allocator.next(),p::Error::Closed,"Moved-from allocator remained usable");
  check(take(moved.statistics()).reservedInputs==count,"Move changed the exact saved cursor");
  const auto last=take(moved.next());check(last!=0&&outputs.insert(last).second,"Move reset allocation history");
  const auto after=take(moved.statistics()).reservedInputs;
  check(after>=count+1&&after<=count+2,"Move lost the cursor or repeated a skipped zero");
}
}
int main(int argc,char** argv){
  try{
    check(argc==2,"Expected the independently generated reference-vector path");
    smallDomainsAreBijectionsWithExactReverseRounds();fixed32BitOutputsMatchIndependentPythonOpenSsl(argv[1]);
    cursorCannotWrapAndZeroConsumesItsInput();resourceAndCryptoFailureNeverReplayAnInput();publicFactoryIsLazyMoveOnlyAndThreadAffine();
    std::cout<<"PASS protocol UNIQUE32 prototype: five families\n";return EXIT_SUCCESS;
  }catch(const std::exception& failure){failNextNew=false;std::cerr<<failure.what()<<'\n';return EXIT_FAILURE;}
}
