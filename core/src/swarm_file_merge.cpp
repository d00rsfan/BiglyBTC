// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/swarm_file_merge.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
namespace biglybtc::core {
std::expected<FileMergeSettings,FileMergeError> readFileMergeSettings(const Document* document){
 FileMergeSettings result;if(!document){return result;}
 if(document->root().type!=Node::Type::Dictionary){return std::unexpected(FileMergeError{FileMergeError::Options,"root"});}
 auto value=[&](std::string_view key,std::int64_t fallback)->std::expected<std::int64_t,FileMergeError>{
  const Node* found=nullptr;const auto& nodes=document->root().children;
  for(std::size_t n=0;n+1<nodes.size();n+=2){if(nodes[n].type==Node::Type::Bytes&&nodes[n].bytes==key){if(found){return std::unexpected(FileMergeError{FileMergeError::Options,std::string(key)});}found=&nodes[n+1];}}
  if(!found){return fallback;}if(found->type!=Node::Type::Integer){return std::unexpected(FileMergeError{FileMergeError::Options,std::string(key)});}return found->integer;
 };
 auto enabled=value("Merge Same Size Files",1),extended=value("Merge Same Size Files Extended",0),tolerance=value("Merge Same Size Files Tolerance",0),minimum=value("Merge Same Size Files Min Pieces",5);
 for(const auto* v:{&enabled,&extended,&tolerance,&minimum}){if(!*v){return std::unexpected(v->error());}}
 if(*enabled<0||*enabled>1||*extended<0||*extended>1||*tolerance<0||*tolerance>2147483647||*minimum<0||*minimum>2147483647){return std::unexpected(FileMergeError{FileMergeError::Options,"merge limits"});}
 result.enabled=*enabled!=0;result.extended=*extended!=0;result.tolerance=*tolerance;result.minPieces=*minimum;return result;
}
std::expected<FileMergePlan,FileMergeError> planFileMerges(std::span<const MergeFile> files,FileMergeSettings settings,FileMergeLimits limits){
 try{
  FileMergePlan result;if(!settings.enabled){return result;}
  if(settings.tolerance<0||settings.minPieces<0||!limits.files||limits.files>200000||!limits.pairs||limits.pairs>16384||limits.groupMembers<2||limits.groupMembers>256){return std::unexpected(FileMergeError{FileMergeError::Options,"merge limits"});}
  std::vector<std::size_t> eligible;std::size_t nameBytes=0;
  for(std::size_t n=0;n<files.size();++n){const auto& f=files[n];if(!f.ownerEligible||f.padding||f.skipped||!f.bytes||(!settings.extended&&f.ownerComplete)){continue;}
   if(f.owner.empty()||!f.admission||f.index<0||f.offset<0||f.bytes<0||f.pieceLength<=0||f.offset>std::numeric_limits<std::int64_t>::max()-f.bytes){return std::unexpected(FileMergeError{FileMergeError::Geometry,"file geometry"});}
   if(eligible.size()>=limits.files||f.name.size()>limits.nameBytes-nameBytes){result.truncated=true;continue;}nameBytes+=f.name.size();eligible.push_back(n);
  }result.eligibleFiles=eligible.size();
  std::set<std::pair<std::size_t,std::size_t>> emitted;std::set<std::string> largeOwners;
  auto emit=[&](std::size_t a,std::size_t b,bool root,bool firstPass){
   if(a==b){return;}const auto& x=files[a];const auto& y=files[b];
   if((x.ownerComplete&&y.ownerComplete)||(x.owner==y.owner&&(!root||x.admission!=y.admission||x.index==y.index))){return;}
   auto key=std::minmax(a,b);if(emitted.contains(key)){return;}if(result.pairs.size()>=limits.pairs){result.truncated=true;return;}
   emitted.emplace(key);result.pairs.push_back({key.first,key.second,root});if(firstPass){largeOwners.insert(x.owner);largeOwners.insert(y.owner);}
  };
  for(int pass=0;pass<2;++pass){
   if(pass&&largeOwners.empty()){break;}std::vector<std::size_t> selected;
   for(const auto n:eligible){const auto& f=files[n];const auto pieces=(f.offset+f.bytes-1)/f.pieceLength-f.offset/f.pieceLength+1;if(pieces>=settings.minPieces||(pass&&largeOwners.contains(f.owner))){selected.push_back(n);}}
   std::ranges::stable_sort(selected,[&](auto a,auto b){return files[a].ownerComplete<files[b].ownerComplete;});
   std::map<std::array<std::byte,32>,std::vector<std::size_t>> roots;
   for(const auto n:selected){if(files[n].root){auto& group=roots[*files[n].root];if(group.size()<limits.groupMembers){group.push_back(n);}else{result.truncated=true;}}}
   std::set<std::size_t> rooted;
   for(const auto& [hash,group]:roots){(void)hash;if(group.size()<2){continue;}for(std::size_t i=0;i<group.size();++i){for(std::size_t j=i+1;j<group.size();++j){if(files[group[i]].bytes==files[group[j]].bytes){emit(group[i],group[j],true,pass==0);}}}for(const auto n:group){rooted.insert(n);}}
   std::map<std::string,std::vector<std::size_t>> names;std::map<std::size_t,std::int64_t> canonical;
   if(settings.tolerance){for(const auto n:selected){if(!rooted.contains(n)){names[files[n].name].push_back(n);}}
    for(auto& [name,group]:names){(void)name;std::ranges::stable_sort(group,[&](auto a,auto b){return files[a].bytes<files[b].bytes;});std::int64_t start=-1;for(const auto n:group){if(start<0||files[n].bytes-start>settings.tolerance){start=files[n].bytes;}canonical[n]=start;}}
   }
   std::map<std::int64_t,std::vector<std::size_t>> sizes;
   for(const auto n:selected){if(rooted.contains(n)){continue;}auto& group=sizes[canonical.contains(n)?canonical[n]:files[n].bytes];
    if(std::ranges::any_of(group,[&](auto other){return files[other].owner==files[n].owner;})){continue;}if(group.size()<limits.groupMembers){group.push_back(n);}else{result.truncated=true;}}
   for(const auto& [bytes,group]:sizes){(void)bytes;for(std::size_t i=0;i<group.size();++i){for(std::size_t j=i+1;j<group.size();++j){emit(group[i],group[j],false,pass==0);}}}
  }return result;
 }catch(...){return std::unexpected(FileMergeError{FileMergeError::Memory,"file merge index"});}
}
}
