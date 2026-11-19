#include "original_storage_defaults.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

namespace {
using namespace biglybtc;
std::size_t assertions=0;
void check(bool value,const char* message){++assertions;if(!value){std::cerr<<message<<'\n';std::exit(1);}}
core::Document parse(const std::string& text){auto d=core::Document::parse(text);check(d.has_value(),"Generated saved priorities parse");return std::move(*d);}
}
int main(){
  using Kind=storage::OriginalStorageKind;
  for(const auto kind:{Kind::Linear,Kind::Compact,Kind::Reordered,Kind::ReorderedCompact}){for(int mode=0;mode<5;++mode){
    const auto document=parse(mode==0?"de":"d15:file_prioritiesli"+std::to_string(mode==1?-1:mode==2?0:mode==3?1:-2)+"eee");
    const std::array requested{kind};const auto result=storage::readOriginalFilePolicies(document.root(),requested);
    check(result&&result->projected.size()==1&&result->restored.size()==1,"Complete constructor restore vectors");
    const bool compact=kind==Kind::Compact||kind==Kind::ReorderedCompact;
    check(result->restored[0].skipped==(mode==2||((mode==0||mode==1)&&compact)),"C/X defaults retained only for all-normal saved values");
    check(result->restored[0].priority==(mode==3?1:mode==4?-1:0),"Original signed priorities retained");
    check(result->projected[0].skipped==(mode==2),"Raw source projection remains independent");
  }}
  const std::array requested{Kind::Compact,Kind::ReorderedCompact,Kind::Linear};
  const auto ordinary=parse("d15:file_prioritiesli-1ei-1ei-1eee");const auto initial=storage::readOriginalFilePolicies(ordinary.root(),requested);
  check(initial&&initial->restored[0].skipped&&initial->restored[1].skipped&&!initial->restored[2].skipped,"Complete mixed constructor defaults");
  const auto override=parse("d15:file_prioritiesli-1ei-1ei1eee");const auto replaced=storage::readOriginalFilePolicies(override.root(),requested);
  check(replaced&&replaced->restored==replaced->projected&&!replaced->restored[0].skipped&&!replaced->restored[1].skipped,"Any nonnormal slot replaces all constructor fields");
  const std::array invalid{static_cast<Kind>(9)};check(!storage::readOriginalFilePolicies(ordinary.root(),invalid),"Unknown requested kind refuses");
  const std::vector<Kind> huge(100001,Kind::Linear);check(!storage::readOriginalFilePolicies(ordinary.root(),huge),"Complete census work bound refuses");
  const auto malformed=parse("d15:file_priorities1:xe");check(!storage::readOriginalFilePolicies(malformed.root(),requested),"Malformed saved projection refuses");
  std::cout<<"PASS_ORIGINAL_EFFECTIVE_POLICY "<<assertions<<" assertions\n";
}
