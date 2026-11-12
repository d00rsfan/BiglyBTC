// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/swarm_file_merge.hpp"
#include <iostream>
#include <stdexcept>
using namespace biglybtc::core;
namespace {
void require(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
std::string b(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
MergeFile file(std::string owner,int index,std::int64_t bytes){MergeFile f;f.owner=std::move(owner);f.admission=1;f.index=index;f.name="generated.bin";f.bytes=bytes;f.pieceLength=16384;return f;}
FileMergePlan plan(const std::vector<MergeFile>& files,FileMergeSettings settings={},FileMergeLimits limits={}){auto p=planFileMerges(files,settings,limits);require(p.has_value(),"planner accepted generated valid metadata");return std::move(*p);}
bool pair(const FileMergePlan& p,std::size_t a,std::size_t b,bool root=false){for(const auto& v:p.pairs){if(v.first==a&&v.second==b&&v.sameRoot==root){return true;}}return false;}
}
int main(){try{
 auto defaults=readFileMergeSettings(nullptr);require(defaults&&defaults->enabled&&!defaults->extended&&!defaults->tolerance&&defaults->minPieces==5,"original merge defaults");
 auto doc=Document::parse("d"+b("Merge Same Size Files")+"i0e"+b("Merge Same Size Files Extended")+"i1e"+b("Merge Same Size Files Tolerance")+"i99e"+b("Merge Same Size Files Min Pieces")+"i0ee");require(doc.has_value(),"settings fixture");auto configured=readFileMergeSettings(&*doc);require(configured&&!configured->enabled&&configured->extended&&configured->tolerance==99&&!configured->minPieces,"exact original settings keys");
 auto bad=Document::parse("d"+b("Merge Same Size Files")+b("yes")+"e");require(bad&&!readFileMergeSettings(&*bad),"invalid optional policy cannot silently enable writes");
 auto a=file("A",0,131072),c=file("B",0,131072);c.name="different-name.bin";require(pair(plan({a,c}),0,1),"exact-size candidates do not require the same filename");require(plan({a,a}).pairs.empty(),"same torrent is not a size-only peer");
 c.ownerComplete=true;require(plan({a,c}).pairs.empty(),"complete download manager requires Extended");FileMergeSettings extended;extended.extended=true;require(pair(plan({a,c},extended),0,1),"Extended includes a complete source");a.ownerComplete=true;require(plan({a,c},extended).pairs.empty(),"no pointless complete-to-complete work");a.ownerComplete=false;c.ownerComplete=false;
 c.skipped=true;require(plan({a,c}).pairs.empty(),"skip respected");c.skipped=false;c.padding=true;require(plan({a,c}).pairs.empty(),"padding not a local file candidate");c.padding=false;c.ownerEligible=false;require(plan({a,c}).pairs.empty(),"low-noise and metadata owners excluded");c.ownerEligible=true;
 std::array<std::byte,32> root{};root[0]=std::byte{7};a.root=c.root=root;auto other=file("C",0,131072);auto rooted=plan({a,c,other});require(rooted.pairs.size()==1&&pair(rooted,0,1,true),"authenticated-root groups precede size-only groups");c.owner=a.owner;c.index=1;require(pair(plan({a,c}),0,1,true),"same-torrent distinct root-identical files can be reused");c.admission=2;require(plan({a,c}).pairs.empty(),"stale incarnation never a same-torrent peer");
 a=file("A",0,131072);c=file("B",0,131074);require(plan({a,c}).pairs.empty(),"different lengths are not exact-size peers");FileMergeSettings tolerant;tolerant.tolerance=2;require(pair(plan({a,c},tolerant),0,1),"same-name original tolerance clustering");c.name="unrelated.bin";require(plan({a,c},tolerant).pairs.empty(),"tolerance cannot group unrelated names");
 a=file("A",0,131072);c=file("B",0,131072);auto smallA=file("A",1,3000),smallB=file("B",1,3000),smallC=file("C",1,3000);auto small=plan({a,c,smallA,smallB,smallC});require(small.pairs.size()==2&&pair(small,2,3),"second pass includes small files only for already-linked owners");
 FileMergeLimits limits;limits.groupMembers=2;limits.pairs=1;auto bounded=plan({a,c,other},FileMergeSettings{},limits);require(bounded.truncated&&bounded.pairs.size()==1,"candidate limits are explicit");a.offset=9223372036854775807LL;require(!planFileMerges(std::vector<MergeFile>{a},FileMergeSettings{}),"offset overflow rejected");
 std::cout<<"PASS_FILE_MERGE_POLICY defaults/settings/eligibility/root/size/tolerance/two-pass/bounds\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL_FILE_MERGE_POLICY "<<e.what()<<'\n';return 1;}}
