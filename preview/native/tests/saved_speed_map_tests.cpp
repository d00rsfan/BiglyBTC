#include "biglybtc/saved_speed_map.hpp"
#include "biglybtc/saved_speed_manager.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace biglybtc::core;
std::size_t cases=0,assertions=0;
void check(bool condition,const char* reason){++assertions;if(!condition){throw std::runtime_error(reason);}}
std::string read(const std::filesystem::path& path){
  std::ifstream stream(path,std::ios::binary);check(bool(stream),"fixture open failed");
  return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
SavedSpeedMap parse(std::string_view bytes,SavedSpeedMapMode mode=SavedSpeedMapMode::Variance){
  auto result=readSavedSpeedMap(bytes,mode);check(bool(result),"saved map parse failed");return std::move(*result);
}
std::string one(std::int64_t metric){return "d5:pingsld1:mi"+std::to_string(metric)+"e1:xi1e1:yi2eeee";}
template<class Body>void run(const char* name,Body body){body();++cases;std::cout<<"case "<<name<<'\n';}
}

int main(int argc,char** argv){try{
  check(argc==2,"pass exact saved-speed-map fixture directory");const std::filesystem::path fixtures=argv[1];
  run("literal-writer-records-match-independent-narrowing-vectors",[&]{
    std::ifstream input(fixtures/"expected.tsv");check(bool(input),"expected TSV unavailable");std::string line;std::size_t rows=0;
    while(std::getline(input,line)){
      if(line.empty()||line.front()=='#'){continue;}
      std::istringstream fields(line);std::string leaf,extra;std::size_t index=0;unsigned x=0,y=0,metric=0,up=0,down=0,clamped=0;double rating=0;
      check(bool(fields>>leaf>>index>>x>>y>>metric>>up>>down>>clamped>>rating)&&!(fields>>extra),"invalid independent TSV row");
      check(leaf.find('/')==std::string::npos&&leaf.ends_with(".dat"),"unsafe fixture name");
      const auto value=parse(read(fixtures/leaf));check(value.presence==SavedSpeedMapPresence::Present,"pings list not present");
      check(index<value.records.size()&&value.records[index].point.has_value(),"literal point missing");
      const auto& row=value.records[index];const auto& point=*row.point;
      check(row.sourceIndex==index&&!row.malformed,"literal source index/validity lost");
      check(point.x==x&&point.y==y&&point.metric==metric,"Java int/short/clamp mismatch");
      check(point.uploadBytesPerSecond==up&&point.downloadBytesPerSecond==down,"256-byte grid scaling mismatch");
      check(point.metricClamped==bool(clamped),"upper clamp provenance mismatch");
      check(point.varianceRating&&std::abs(*point.varianceRating-rating)<1e-12,"literal metric rating mismatch");++rows;
    }
    check(rows==11,"literal normalization corpus incomplete");
  });
  run("genuine-zero-empty-missing-and-wrong-list-are-distinct",[&]{
    const auto missing=parse("de"),empty=parse("d5:pingslee"),wrong=parse("d5:pingsi0ee");
    const auto zero=parse(read(fixtures/"known-zero.dat"));
    check(missing.presence==SavedSpeedMapPresence::Missing&&!missing.maxMetric&&!missing.currentMetricRating,"missing map fabricated a sample");
    check(empty.presence==SavedSpeedMapPresence::Present&&empty.records.empty()&&!empty.maxUploadBytesPerSecond,"empty map fabricated bounds");
    check(wrong.presence==SavedSpeedMapPresence::Malformed&&wrong.records.empty(),"wrong list became empty/missing");
    check(zero.records.size()==1&&zero.records.front().point&&zero.maxMetric==0&&zero.maxUploadBytesPerSecond==0&&zero.maxDownloadBytesPerSecond==0,"observed zero disappeared");
    check(zero.currentMetricRating==1.0&&zero.zones.empty(),"zero rating or isolated geometry wrong");
  });
  run("ordinary-adjacent-zone-extents-and-average-are-literal",[&]{
    const auto value=parse(read(fixtures/"ordinary.dat"));check(value.zones.size()==2,"ordinary adjacency lost");
    const auto& a=value.zones[0];check(a.firstRecord==0&&a.secondRecord==1,"zone source identity wrong");
    check(a.uploadStartBytesPerSecond==256&&a.uploadEndBytesPerSecond==1279&&a.downloadStartBytesPerSecond==512&&a.downloadEndBytesPerSecond==1791&&a.metric==75,"first source zone geometry mismatch");
    const auto& b=value.zones[1];check(b.uploadStartBytesPerSecond==768&&b.uploadEndBytesPerSecond==1279&&b.downloadStartBytesPerSecond==1280&&b.downloadEndBytesPerSecond==1791&&b.metric==125,"second source zone geometry mismatch");
    check(!a.reversedUpload&&!a.reversedDownload&&!b.reversedUpload&&!b.reversedDownload,"ordinary zone marked reversed");
    check(value.maxUploadBytesPerSecond==1024&&value.maxDownloadBytesPerSecond==1536&&value.maxMetric==150,"point bounds wrong");
  });
  run("signed-short-region-ordering-is-preserved-with-reversal-flags",[&]{
    const auto value=parse(read(fixtures/"wrap-and-cap.dat"));check(value.zones.size()==6,"valid wrapped points lost");
    const auto& crossing=value.zones[1];check(crossing.firstRecord==1&&crossing.secondRecord==2,"crossing source indices wrong");
    check(crossing.uploadStartBytesPerSecond==8388608&&crossing.uploadEndBytesPerSecond==8388607&&crossing.reversedUpload,"signed-short upload ordering silently normalized");
    check(crossing.downloadStartBytesPerSecond==8388608&&crossing.downloadEndBytesPerSecond==8388607&&crossing.reversedDownload,"signed-short download ordering silently normalized");
    check(crossing.metric==33517,"unsigned metric average/int division mismatch");
    check(value.maxUploadBytesPerSecond==16776960&&value.maxDownloadBytesPerSecond==16776960&&value.maxMetric==65535,"wrapped maximum lost");
  });
  run("signed-long-raw-facts-survive-java-int-then-short-projection",[&]{
    const auto value=parse(read(fixtures/"wrap-and-cap.dat"));
    check(value.records[0].rawX==4294967297LL&&value.records[0].rawMetric==4294967396LL,"raw saved longs narrowed in place");
    check(value.records[4].rawX==std::numeric_limits<std::int64_t>::max()&&value.records[4].rawY==std::numeric_limits<std::int64_t>::min(),"int64 extremes not preserved");
    check(value.records[5].rawMetric==2147483648LL&&value.records[5].point->metric==0&&!value.records[5].point->metricClamped,"negative Java int was upper-clamped before int narrowing");
  });
  run("malformed-rows-keep-independent-fields-and-break-zone-links",[&]{
    const auto value=parse(read(fixtures/"malformed-gaps.dat"));
    check(value.records.size()==7&&value.malformedRecords==3,"malformed source slots were dropped");
    check(value.records[1].rawX==2&&value.records[1].rawY==3&&!value.records[1].rawMetric&&!value.records[1].point&&value.records[1].malformed,"wrong metric fabricated point or lost independent raw fields");
    check(value.records[2].malformed&&!value.records[2].rawX&&!value.records[2].point,"nondictionary row admitted");
    check(value.records[3].point&&value.records[3].point->metric==0&&value.records[4].malformed,"valid zero after gap lost");
    check(value.zones.size()==1&&value.zones.front().firstRecord==5&&value.zones.front().secondRecord==6,"zone bridged malformed/unknown slots");
    check(value.currentMetricRating==0.0,"last actual rating lost");
  });
  run("malformed-last-record-does-not-reuse-an-older-current-rating",[]{
    const auto value=parse("d5:pingsld1:mi50e1:xi1e1:yi2eedeee");
    check(value.records.size()==2&&value.records[0].point&&value.records[1].malformed,"malformed tail shape lost");
    check(value.maxMetric==50&&!value.currentMetricRating&&value.zones.empty(),"malformed tail reused an old rating/zone");
  });
  run("source-variance-rating-thresholds-match-literal-boundaries",[&]{
    std::ifstream input(fixtures/"rating.tsv");check(bool(input),"rating TSV unavailable");std::string line;std::size_t rows=0;
    while(std::getline(input,line)){
      if(line.empty()||line.front()=='#'){continue;}std::istringstream fields(line);std::int64_t raw=0;unsigned effective=0;double expected=0;std::string extra;
      check(bool(fields>>raw>>effective>>expected)&&!(fields>>extra),"invalid rating vector");const auto value=parse(one(raw));
      check(value.records[0].point->metric==effective&&value.currentMetricRating&&std::abs(*value.currentMetricRating-expected)<1e-12,"variance piecewise rating mismatch");++rows;
    }
    check(rows==11,"rating boundaries incomplete");
  });
  run("explicit-rtt-mode-has-its-own-cap-without-variance-unit-claim",[&]{
    const auto value=parse(read(fixtures/"wrap-and-cap.dat"),SavedSpeedMapMode::Rtt);
    check(value.mode==SavedSpeedMapMode::Rtt&&!value.records[1].point->metricClamped&&value.records[1].point->metric==1501,"RTT mode incorrectly applied variance cap");
    check(value.records[6].point->metric==30000&&value.records[6].point->metricClamped,"RTT cap not30000");
    check(value.records[2].point->metric==65535&&!value.records[2].point->varianceRating&&value.currentMetricRating==0.0,"RTT source getter/non-variance distinction wrong");
  });
  run("unknown-subtrees-source-order-and-owned-bytes-remain-exact",[&]{
    auto raw=read(fixtures/"ordinary.dat");const auto original=raw;auto value=parse(raw);raw.assign("de");
    check(value.source&&value.source->originalBytes()==original&&value.source->serialize()==original,"source edited or alias escaped");
    const auto* opaque=value.source->find("opaque");check(opaque&&opaque->find("key")&&opaque->find("key")->bytes=="value","unknown subtree lost");
    auto existing=readSavedSpeedHistory(original);check(bool(existing)&&existing->savedPingRecords==3,"existing parser contract changed");
    auto fromSource=parse(existing->source->originalBytes());check(fromSource.records.size()==3&&fromSource.source->originalBytes()==existing->source->originalBytes(),"SavedSpeedHistory source not reusable");
    check(fromSource.source!=existing->source,"fresh projection borrowed mutable ownership");
    auto reused=readSavedSpeedMap(existing->source);check(bool(reused)&&reused->source==existing->source&&reused->records.size()==3,"owned overload cloned or lost source");
  });
  run("owned-document-official-edits-are-rejected-until-fresh-parse",[]{
    auto parsed=Document::parse(one(50));check(bool(parsed),"edit fixture parse failed");
    check(bool(parsed->replaceValue("future","i7e")),"official edit failed");
    auto source=std::make_shared<const Document>(std::move(*parsed));const auto result=readSavedSpeedMap(source);
    check(!result&&result.error()==SavedSpeedMapError::EditedDocument,"edited source projected as original bytes");
    const auto fresh=parse(source->serialize());check(fresh.records.size()==1,"fresh serialized document not reusable");
    const auto absent=readSavedSpeedMap(std::shared_ptr<const Document>{});check(!absent&&absent.error()==SavedSpeedMapError::Root,"null owner fabricated empty map");
  });
  run("owned-document-inconsistent-views-are-rejected-before-view-read",[]{
    auto parsed=Document::parse(one(50));check(bool(parsed),"view fixture parse failed");
    auto& key=const_cast<Node&>(parsed->root().children.front());
    key.bytes=std::string_view(reinterpret_cast<const char*>(std::uintptr_t{1}),5);
    auto result=readSavedSpeedMap(std::make_shared<const Document>(std::move(*parsed)));
    check(!result&&result.error()==SavedSpeedMapError::EditedDocument,"unowned key view was used");
    parsed=Document::parse(one(50));check(bool(parsed),"integer fixture parse failed");
    auto& metric=const_cast<Node&>(*parsed->find("pings")->children[0].find("m"));metric.integer=0;
    result=readSavedSpeedMap(std::make_shared<const Document>(std::move(*parsed)));
    check(!result&&result.error()==SavedSpeedMapError::EditedDocument,"raw/tree integer disagreement admitted");
  });
  run("owned-document-oversized-tree-cannot-inherit-wider-parser-quotas",[]{
    Limits wider;wider.depth=80;std::string deep="d1:z";deep.append(65,'l');deep.append(66,'e');
    auto parsed=Document::parse(deep,wider);check(bool(parsed),"wide-depth fixture parse failed");
    auto result=readSavedSpeedMap(std::make_shared<const Document>(std::move(*parsed)));
    check(!result&&result.error()==SavedSpeedMapError::Limit,"wide inherited depth escaped projection bound");
    wider=Limits{};wider.nodes=100005;std::string nodes="d1:zl";
    for(std::size_t i=0;i<100000;++i){nodes+="i0e";}nodes+="ee";
    parsed=Document::parse(nodes,wider);check(bool(parsed),"wide-node fixture parse failed");
    result=readSavedSpeedMap(std::make_shared<const Document>(std::move(*parsed)));
    check(!result&&result.error()==SavedSpeedMapError::Limit,"wide inherited node cap escaped projection bound");
  });
  run("selected-record-bound-is-atomic-and-never-prefix-success",[]{
    std::string raw="d5:pingsl";const std::string row="d1:mi1e1:xi1e1:yi1ee";
    for(std::size_t i=0;i<SavedSpeedMapMaximumRecords;++i){raw+=row;}raw+="ee";
    const auto full=parse(raw);check(full.records.size()==1200&&full.zones.size()==1199,"default writer maximum rejected");
    raw.insert(raw.size()-2,row);auto over=readSavedSpeedMap(raw);
    check(!over&&over.error()==SavedSpeedMapError::Limit,"oversize map returned a misleading prefix");
  });
  run("input-format-root-depth-node-and-mode-errors-are-distinct",[]{
    auto result=readSavedSpeedMap("bad");check(!result&&result.error()==SavedSpeedMapError::Format,"format error lost");
    result=readSavedSpeedMap("le");check(!result&&result.error()==SavedSpeedMapError::Root,"root error lost");
    result=readSavedSpeedMap("de",static_cast<SavedSpeedMapMode>(99));check(!result&&result.error()==SavedSpeedMapError::Mode,"invalid mode accepted");
    result=readSavedSpeedMap(std::string(Limits{}.inputBytes+1,'x'));check(!result&&result.error()==SavedSpeedMapError::Limit,"input limit missing");
    std::string deep="d1:z";deep.append(65,'l');deep.append(66,'e');result=readSavedSpeedMap(deep);check(!result&&result.error()==SavedSpeedMapError::Limit,"depth limit missing");
    std::string nodes="d1:zl";for(std::size_t i=0;i<100000;++i){nodes+="i0e";}nodes+="ee";
    result=readSavedSpeedMap(nodes);check(!result&&result.error()==SavedSpeedMapError::Limit,"node limit missing");
  });
  std::cout<<"PASS "<<cases<<" saved-speed-map cases; "<<assertions<<" assertions; source-derived fixtures only, no Java runtime equality or live mapper claim\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
