#include "biglybtc/file_policy_restoration.hpp"
#include "biglybtc/compact_file_requirement.hpp"
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
using namespace biglybtc;
std::size_t assertions=0;
void check(bool value,const char* message){++assertions;if(!value){std::cerr<<message<<'\n';std::exit(1);}}
std::vector<std::string_view> split(std::string_view value,char delimiter){
  std::vector<std::string_view> out;
  for(;;){const auto at=value.find(delimiter);out.push_back(value.substr(0,at));if(at==value.npos){return out;}value.remove_prefix(at+1);}
}
std::int64_t integer(std::string_view value){
  std::int64_t out=0;const auto r=std::from_chars(value.data(),value.data()+value.size(),out);
  check(r.ec==std::errc{}&&r.ptr==value.data()+value.size(),"Generated integer");return out;
}
void units(){
  const std::vector<compat::FilePolicy> initial{{true,0},{false,0},{true,0}};
  auto restored=compat::originalRestoredFilePolicy(std::vector<compat::FilePolicy>(3),initial);
  check(restored&&*restored==initial,"Ordinary saved fields must keep compact constructor skipped defaults");
  const std::vector<compat::FilePolicy> special{{false,0},{false,1},{true,0}};
  restored=compat::originalRestoredFilePolicy(special,initial);
  check(restored&&*restored==special,"One special field replaces the whole file-set values");
  check(!compat::originalRestoredFilePolicy(special,std::span<const compat::FilePolicy>{}),"Incomplete initial census refuses");
  auto empty=compat::originalRestoredFilePolicy({},{});check(empty&&empty->empty(),"Known empty census");
  const std::vector<compat::OriginalFilePieceSpan> reversed{{0,0,false},{1,0,true},{1,1,false}};
  auto batch=compat::originalCompactFileRequirements(reversed);check(batch&&(*batch)[1]==0,"Aligned zero range must not acquire adjacent needed pieces");
  check(!compat::originalCompactFileRequirements(std::vector<compat::OriginalFilePieceSpan>{{2,2,false},{0,0,false}}),"Unordered complete census refuses");
  std::vector<compat::OriginalFilePieceSpan> scale;for(std::int32_t i=0;i<100000;++i){scale.push_back({i,i,true});}
  batch=compat::originalCompactFileRequirements(scale);check(batch&&batch->size()==scale.size(),"Large missing optional census is evaluated in one bounded pass");
  for(const auto value:*batch){check(value==0,"Fully skipped census has no required compact edges");}
  const std::vector<compat::FilePolicy> large(100001);
  check(!compat::originalRestoredFilePolicy(large,large),"Restoration is bounded before output allocation");
}
}
int main(int argc,char** argv){
  units();
  if(argc==1){std::cout<<"PASS_COMPACT_OWNER_UNITS "<<assertions<<'\n';return 0;}
  check(argc==3,"Expected inputs and actual Original output");
  std::ifstream cases(argv[1]),original(argv[2]);check(cases.good()&&original.good(),"Reference inputs readable");std::size_t count=0;
  for(std::string input,expected;std::getline(cases,input);){
    check(input.size()<8192&&bool(std::getline(original,expected)),"Bounded reference row counts");
    const auto f=split(input,'\t');check(f.size()==6,"Reference fields");
    const auto lengths=split(f[1],','),kinds=split(f[2],',');check(lengths.size()==kinds.size()&&!lengths.empty()&&lengths.size()<=8,"Complete file census");
    std::vector<compat::FilePolicy> initial;for(const auto kind:kinds){const auto k=integer(kind);check(k>=1&&k<=4,"Original storage kind");initial.push_back({k==2||k==4,0});}
    std::string raw="de";
    if(f[3]!="absent"){
      raw="d15:file_prioritiesl";if(f[3]!="empty"){for(const auto value:split(f[3],',')){raw+='i'+std::to_string(integer(value))+'e';}}raw+="ee";
    }
    auto document=core::Document::parse(raw);check(document.has_value(),"Strict saved source fixture");
    auto projected=compat::originalFilePolicy(document->root(),lengths.size());check(projected.has_value(),"Strict raw policy projection");
    auto restored=compat::originalRestoredFilePolicy(*projected,initial);check(restored.has_value(),"Full restored policy");
    std::vector<compat::OriginalFilePieceSpan> geometry;std::int64_t offset=0;
    std::string values,required;
    for(std::size_t i=0;i<lengths.size();++i){
      const auto size=integer(lengths[i]);auto span=compat::originalFilePieceSpan(offset,size,16384,(*restored)[i].skipped);check(span.has_value(),"Exact supported original geometry");geometry.push_back(*span);offset+=size;
      if(i){values+=',';}values+=(*restored)[i].skipped?"1:":"0:";values+=std::to_string((*restored)[i].priority);
    }
    const auto batch=compat::originalCompactFileRequirements(geometry);check(batch&&batch->size()==geometry.size(),"Complete batch requirement census");
    for(std::size_t i=0;i<lengths.size();++i){
      if(i){required+=',';}const auto kind=integer(kinds[i]);
      if(kind==2||kind==4){auto needed=compat::originalCompactFileMustExist(geometry,i);check(needed.has_value(),"Compact requirement known");check((*batch)[i]==*needed,"Batch matches single qualified compact predicate");required+=*needed?'1':'0';}else{required+='-';}
    }
    check(std::string(f[0])+'\t'+values+'\t'+required==expected,"Native restored flags/compact requirements differ from actual constructors/receiver/predicate");++count;
  }
  std::string extra;check(!std::getline(original,extra)&&count>0,"No unmatched Original output");
  std::cout<<"PASS_NATIVE_COMPACT_OWNER "<<count<<" rows; "<<assertions<<" assertions\n";
}
