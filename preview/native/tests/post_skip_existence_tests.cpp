// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/post_skip_existence.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {
using namespace biglybtc::compat;
std::size_t assertions=0;
void check(bool value,const char* message){++assertions;if(!value){std::cerr<<message<<'\n';std::exit(1);}}
std::vector<std::string> split(const std::string& value,char separator){
  std::vector<std::string> result;std::istringstream input(value);std::string field;
  while(std::getline(input,field,separator)){result.push_back(field);}return result;
}
std::string bits(const std::vector<std::uint8_t>& value){std::string result;for(auto bit:value){result.push_back(char('0'+bit));}return result;}
int oracle(){
  std::string line;
  while(std::getline(std::cin,line)){
    const auto fields=split(line,'\t');check(fields.size()==9,"Nine reference input fields required");
    const auto lengths=split(fields[1],',');const auto rawKinds=split(fields[2],',');
    check(lengths.size()==rawKinds.size()&&lengths.size()<64,"Complete bounded oracle census required");
    std::vector<OriginalFilePieceSpan> files;std::vector<std::int32_t> kinds;
    std::vector<std::uint8_t> changed,exists;std::int64_t offset=0;
    const auto skipMask=std::stoull(fields[3]),changeMask=std::stoull(fields[4]),existsMask=std::stoull(fields[5]);
    for(std::size_t i=0;i<lengths.size();++i){
      const auto length=std::stoll(lengths[i]);const auto span=originalFilePieceSpan(offset,length,16384,(skipMask>>i)&1);
      check(span.has_value(),"Actual file geometry accepted");files.push_back(*span);offset+=length;kinds.push_back(std::stoi(rawKinds[i]));
      changed.push_back((changeMask>>i)&1);exists.push_back((existsMask>>i)&1);
    }
    std::vector<std::uint8_t> flags;std::optional<std::span<const std::uint8_t>> resume;
    const auto mode=std::stoi(fields[8]);check(mode>=0&&mode<=4,"Known reference resume mode required");
    if(mode){flags.resize(static_cast<std::size_t>((offset+16383)/16384));for(std::size_t i=0;i<flags.size();++i){flags[i]=mode==1?0:mode==2?1:mode==3?2:i%2==0?0:3;}resume=flags;}
    const auto result=originalPostSkipExistence(files,kinds,changed,exists,fields[6]=="1",fields[7]=="1",resume);
    check(result.has_value(),"Closed actual Source matrix accepts");
    std::cout<<fields[0]<<'\t'<<bits(result->interesting)<<'\t'<<bits(result->existenceProbes)<<'\t'<<bits(result->allocationRequests)<<'\t'<<bits(result->compactRemovals)<<'\n';
  }
  return 0;
}
}
int main(int argc,char** argv){
  if(argc==2&&std::string(argv[1])=="--oracle"){return oracle();}
  const std::vector<std::uint8_t> yes{1},no{0},done{2},empty;
  const std::vector<OriginalFilePieceSpan> skipped{{0,1,true}};
  for(const int kind:{1,2,3,4}){
    const std::vector<std::int32_t> kinds{kind};
    auto result=originalPostSkipExistence(skipped,kinds,yes,yes,true,true);
    check(result&&result->compactRemovals[0]==(kind==2||kind==4),"Only unneeded C/X schedules removal");
    const std::vector<std::uint8_t> flags{0,2};
    result=originalPostSkipExistence(skipped,kinds,yes,no,false,true,flags);
    check(result&&result->allocationRequests[0]==(kind==1||kind==3),"Nonzero resume requires L/R even with all files skipped");
    result=originalPostSkipExistence(skipped,kinds,yes,no,false,false,flags);
    check(result&&result->interesting==no&&result->allocationRequests==no,"Construction performs no existence work");
  }
  const std::vector<OriginalFilePieceSpan> chain{{0,0,true},{0,1,true},{1,1,true}};
  const std::vector<std::int32_t> compact{2,2,2};
  auto result=originalPostSkipExistence(chain,compact,std::vector<std::uint8_t>{1,0,0},std::vector<std::uint8_t>{1,1,1},true,true);
  check(result&&bits(result->interesting)=="110"&&bits(result->compactRemovals)=="110","Neighbour inclusion does not expand transitively");
  const std::vector<OriginalFilePieceSpan> shared{{0,0,false},{0,0,true}};
  result=originalPostSkipExistence(shared,std::vector<std::int32_t>{1,4},std::vector<std::uint8_t>{0,1},std::vector<std::uint8_t>{0,0},false,true);
  check(result&&bits(result->allocationRequests)=="11","Required shared neighbour is included in allocation request");
  const std::vector<OriginalFilePieceSpan> zero{{1,0,false}};
  result=originalPostSkipExistence(zero,std::vector<std::int32_t>{1},yes,no,false,true,empty);
  check(result&&result->allocationRequests==no,"Aligned empty file does not inspect a reversed resume range");
  auto error=originalPostSkipExistence(skipped,std::vector<std::int32_t>{1},yes,no,false,true,done);
  check(!error&&error.error()==PostSkipExistenceError::Resume,"Short resume refuses before any IO authority");
  error=originalPostSkipExistence(skipped,std::vector<std::int32_t>{5},yes,no,false,true);
  check(!error&&error.error()==PostSkipExistenceError::Shape,"Unknown storage type refuses");
  error=originalPostSkipExistence(skipped,std::vector<std::int32_t>{1},empty,no,false,true);
  check(!error&&error.error()==PostSkipExistenceError::Shape,"Partial mask refuses");
  error=originalPostSkipExistence(std::vector<OriginalFilePieceSpan>{{1,1,true},{0,0,true}},std::vector<std::int32_t>{1,1},std::vector<std::uint8_t>{1,1},std::vector<std::uint8_t>{0,0},false,true);
  check(!error&&error.error()==PostSkipExistenceError::Order,"Out of order census refuses");
  const std::vector<OriginalFilePieceSpan> large(100000,{0,0,true});const std::vector<std::int32_t> manyKinds(large.size(),2);const std::vector<std::uint8_t> manyYes(large.size(),1);
  result=originalPostSkipExistence(large,manyKinds,manyYes,manyYes,true,true);
  check(result&&result->interesting==manyYes&&result->compactRemovals==manyYes,"One hundred thousand shared zero-file targets remain bounded");
  std::cout<<"PASS_POST_SKIP_EXISTENCE "<<assertions<<" assertions\n";
}
