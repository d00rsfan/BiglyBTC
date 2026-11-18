// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/compact_file_requirement.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace biglybtc::compat;
namespace {
std::size_t assertions=0;
void check(bool condition,const char* text){++assertions;if(!condition){throw std::runtime_error(text);}}
std::vector<std::string> split(const std::string& text,char separator){std::vector<std::string> result;std::istringstream stream(text);for(std::string part;std::getline(stream,part,separator);){result.push_back(part);}return result;}
void unit(){
  auto zero=originalFilePieceSpan(0,0,16384,true);check(zero&&*zero==OriginalFilePieceSpan{0,0,true},"Original zero origin uses truncation toward zero");
  auto aligned=originalFilePieceSpan(16384,0,16384,true);check(aligned&&*aligned==OriginalFilePieceSpan{1,0,true},"Aligned zero file has reversed boundary span");
  auto partial=originalFilePieceSpan(1,0,16384,true);check(partial&&*partial==OriginalFilePieceSpan{0,0,true},"Partial-piece zero file shares its boundary");
  for(const auto& values:std::array<std::array<std::int64_t,3>,6>{{{0,0,1},{-1,1,1},{0,-1,1},{0,1,0},{std::numeric_limits<std::int64_t>::max(),1,1},{std::int64_t(std::numeric_limits<std::int32_t>::max())+1,1,1}}}){
    check(!originalFilePieceSpan(values[0],values[1],values[2],false),"Invalid or unrepresentable geometry refuses");
  }
  const std::array<OriginalFilePieceSpan,3> files{{{0,0,false},{1,0,true},{1,1,true}}};
  auto unneeded=originalCompactFileMustExist(files,1);check(unneeded&&!*unneeded,"Wanted preceding piece cannot require aligned zero target");
  auto needed=originalCompactFileMustExist(files,0);check(needed&&*needed,"Wanted current compact file remains required");
  check(!originalCompactFileMustExist(files,files.size()),"Foreign target refuses");
  const std::array<OriginalFilePieceSpan,2> unordered{{{1,1,true},{0,0,false}}};check(!originalCompactFileMustExist(unordered,0),"Unordered incomplete geometry refuses");
  const std::array<OriginalFilePieceSpan,1> malformed{{{3,0,true}}};check(!originalCompactFileMustExist(malformed,0),"Malformed reversed range refuses");
  std::cout<<"PASS_NATIVE_COMPACT_UNIT "<<assertions<<" assertions\n";
}
void oracle(const char* source,const char* expected){
  std::ifstream inputs(source),outputs(expected);check(bool(inputs)&&bool(outputs),"Explicit generated oracle inputs required");
  std::size_t rows=0;std::string input,output;
  while(std::getline(inputs,input)){
    check(bool(std::getline(outputs,output)),"Original output missing a row");const auto in=split(input,'\t'),out=split(output,'\t');
    check(in.size()==6&&out.size()==4&&in[0]==out[0]&&in[2]==out[1],"Original row/target identity mismatch");
    const auto lengths=split(in[1],','),spans=split(out[2],',');check(!lengths.empty()&&lengths.size()<=16&&lengths.size()==spans.size(),"Original bounded file count mismatch");
    const auto mask=std::stoull(in[3]);std::int64_t offset=0;std::vector<OriginalFilePieceSpan> native;
    for(std::size_t i=0;i<lengths.size();++i){const auto bytes=std::stoll(lengths[i]);auto projected=originalFilePieceSpan(offset,bytes,16384,(mask&(std::uint64_t{1}<<i))!=0);check(bool(projected),"Actual original geometry must project");const auto fields=split(spans[i],':');check(fields.size()==3,"Original geometry row");
      check(projected->first==std::stoi(fields[0])&&projected->last==std::stoi(fields[1])&&projected->skipped==bool(std::stoi(fields[2])),"Actual TOTorrentFile geometry/effective skip differs");native.push_back(*projected);offset+=bytes;
    }
    auto required=originalCompactFileMustExist(native,std::stoull(in[2]));check(required&&*required==bool(std::stoi(out[3])),"Actual RDResumeHandler compact requirement differs");++rows;
  }
  check(!std::getline(outputs,output),"Original output contains extra rows");check(rows>0,"Empty original comparison is not a qualification");
  std::cout<<"PASS_NATIVE_COMPACT_ORACLE "<<rows<<" rows; "<<assertions<<" assertions\n";
}
}
int main(int argc,char** argv){try{unit();if(argc==3){oracle(argv[1],argv[2]);}else if(argc!=1){throw std::runtime_error("Expected no arguments or generated input/original output");}return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
