#include "biglybtc/file_storage_selection.hpp"
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
using namespace biglybtc::compat;
std::size_t assertions=0;
void check(bool v,const char* m){++assertions;if(!v){std::cerr<<m<<'\n';std::exit(1);}}
std::vector<std::string_view> split(std::string_view text,char delimiter){std::vector<std::string_view> out;for(;;){const auto at=text.find(delimiter);out.push_back(text.substr(0,at));if(at==text.npos){return out;}text.remove_prefix(at+1);}}
std::int64_t integer(std::string_view text){std::int64_t n=0;const auto r=std::from_chars(text.data(),text.data()+text.size(),n);check(r.ec==std::errc{}&&r.ptr==text.data()+text.size(),"Generated integer");return n;}
std::string bits(std::span<const std::uint8_t> mask){std::string s;for(const auto v:mask){s+=v?'1':'0';}return s;}
void units(){
  const std::vector<std::int32_t> kinds{4,2,3,1,2};const std::vector<std::uint8_t> selected{1,1,1,1,0};
  auto result=originalSkipStoragePlan(kinds,selected,false);
  check(result&&*result==std::vector<OriginalFileStorageChange>{{1,{0,1,0,0,0}},{3,{1,0,0,0,0}}},"C-to-L precedes X-to-R independent of file ordering");
  result=originalSkipStoragePlan(kinds,selected,true);check(result&&result->empty(),"Skip does not compact files implicitly");
  check(!originalSkipStoragePlan(kinds,std::vector<std::uint8_t>{1},false),"Incomplete changed census refuses");
  check(!originalSkipStoragePlan(std::vector<std::int32_t>{0},std::vector<std::uint8_t>{1},false),"Unknown requested storage refuses");
  check(!originalSkipStoragePlan(std::vector<std::int32_t>{2},std::vector<std::uint8_t>{2},false),"Nonboolean mask refuses");
  result=originalSkipStoragePlan({}, {}, false);check(result&&result->empty(),"Known empty census");
  const std::vector<std::int32_t> large(100001,2);const std::vector<std::uint8_t> mask(100001,1);
  check(!originalSkipStoragePlan(large,mask,false),"Census bound precedes output allocation");
}
}
int main(int argc,char** argv){
  units();if(argc==1){std::cout<<"PASS_FILE_STORAGE_SELECTION_UNITS "<<assertions<<'\n';return 0;}
  check(argc==3,"Expected actual Original inputs/output");std::ifstream input(argv[1]),reference(argv[2]);check(input.good()&&reference.good(),"Reference readable");
  std::size_t rows=0,failures=0,full=0;
  for(std::string raw,observed;std::getline(input,raw);){
    check(raw.size()<8192&&bool(std::getline(reference,observed)),"Reference row counts/bounds");
    auto f=split(raw,'\t'),actual=split(observed,'\t');check(f.size()==6&&actual.size()==5&&f[0]==actual[0],"Reference identity/shape");
    std::vector<std::int32_t> kinds;for(auto value:split(f[1],',')){const auto n=integer(value);check(n>=1&&n<=4,"Bounded types");kinds.push_back(static_cast<std::int32_t>(n));}
    check(!kinds.empty()&&kinds.size()<=8,"Bounded complete census");
    const auto mask=integer(f[2]),initial=integer(f[5]);const bool skipped=integer(f[3])!=0;
    std::vector<std::uint8_t> changed;std::string before;
    for(std::size_t i=0;i<kinds.size();++i){changed.push_back((mask>>i)&1);before+=(initial<0?(kinds[i]==2||kinds[i]==4):bool((initial>>i)&1))?'1':'0';}
    auto plan=originalSkipStoragePlan(kinds,changed,skipped);check(plan.has_value(),"Known Original request plan");
    std::size_t issued=0;bool failed=false,awaiting=false,post=false;std::string expectedMask;
    for(const auto event:split(actual[4],';')){
      const auto fields=split(event,':');
      if(fields[0]=="request"){
        check(!failed&&!awaiting&&issued<plan->size()&&fields.size()==3,"No request after incomplete conversion");
        const auto& step=(*plan)[issued];check(integer(fields[1])==step.target&&fields[2]==bits(step.files),"Actual Original conversion group/order differs from Native plan");expectedMask=bits(step.files);awaiting=true;
      }else if(fields[0]=="result"){
        check(awaiting&&fields.size()==3&&fields[2].size()==expectedMask.size(),"Actual conversion receipt shape");
        for(std::size_t i=0;i<expectedMask.size();++i){check(fields[2][i]!='1'||expectedMask[i]=='1',"Receipt cannot widen requested mask");}
        failed=fields[2]!=expectedMask;awaiting=false;++issued;
      }else if(fields[0]=="skip"){
        check(!failed&&!awaiting&&issued==plan->size(),"Skipped flag changed before every conversion completed");
      }else if(event=="post-construction0"){post=true;check(!failed,"Post-change existence owner called after failed conversion");}
    }
    check(!awaiting,"No unterminated conversion receipt");
    if(failed){++failures;check(!post&&actual[1]==before,"Failure leaves skip flags unchanged even if earlier physical conversions committed");}
    else{
      ++full;check(issued==plan->size()&&post,"Every planned group precedes completed skip flow");
      for(std::size_t i=0;i<changed.size();++i){if(changed[i]){before[i]=skipped?'1':'0';}}
      check(actual[1]==before,"Selected and untouched skip fields follow actual setter flow");
    }
    ++rows;
  }
  std::string extra;check(rows>0&&!std::getline(reference,extra),"No unmatched Original rows");
  std::cout<<"PASS_NATIVE_UNSKIP_STORAGE "<<rows<<" rows; "<<full<<" complete; "<<failures<<" failed conversion prefixes; "<<assertions<<" assertions\n";
}
