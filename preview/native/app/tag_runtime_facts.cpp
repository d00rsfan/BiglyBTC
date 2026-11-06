// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_runtime_facts.hpp"
#include "manual_tag_policy.hpp"
#include <algorithm>
#include <QByteArray>
#include <bit>
#include <limits>
#include <set>

namespace biglybtc::app {
namespace c=core::tags::constraints;
namespace {
c::Problem unavailable(const char* detail){return {c::ProblemKind::Unavailable,detail};}
c::Problem invalid(const char* detail){return {c::ProblemKind::Invalid,detail};}
bool valid(const ConstraintFactStamp& stamp){
  const auto& b=stamp.binding;
  const auto parsed=constraintFactHash(b.target.hash);
  return b.session&&b.target.generation&&stamp.sequence&&parsed
      &&QString::fromStdString(parsed->hex())==b.target.hash
      &&bool(b.providerAdmission)==bool(b.providerGeneration);
}
c::Text text(const QString& value){return value.toStdU16String();}
void put(c::Facts& out,std::string key,c::Value value){
  const auto found=std::find_if(out.values.begin(),out.values.end(),[&](const auto& v){return v.key==key;});
  if(found==out.values.end()){out.values.push_back({std::move(key),std::move(value)});}
  else{found->value=std::move(value);}
}
void size(c::Facts& out,std::int64_t bytes){
  put(out,"size",bytes);put(out,"sizemb",bytes/(1024LL*1024));put(out,"sizegb",bytes/(1024LL*1024*1024));
}
void percent(c::Facts& out,std::int64_t bytes,std::int64_t remaining){
  // cb8 DownloadManagerStatsImpl1673: binary32 intermediate and truncation,
  // including the 999 correction. Only validated nonnegative geometry enters.
  std::int32_t done=1000;
  if(bytes){
    const float fraction=static_cast<float>(bytes-remaining)/static_cast<float>(bytes);
    done=static_cast<std::int32_t>(1000.0f*fraction);
    if(done==1000&&remaining>0){done=999;}
  }
  put(out,"percent",static_cast<float>(done)/10.0f);put(out,"remaining",remaining);
}
c::Result<bool> details(c::Facts& out,const ui::Details& value,const ui::Target& target){
  if(value.target!=target){return std::unexpected(unavailable("File/name snapshot is stale"));}
  if(value.bytes<0||value.name.size()>65536){return std::unexpected(invalid("Invalid original detail size/name"));}
  size(out,value.bytes);put(out,"name",text(value.name));
  if(!value.files){return false;}
  if(value.files->target!=target){return std::unexpected(unavailable("File snapshot is stale"));}
  if(value.files->items.size()>4096){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Original file fact limit"});}
  c::Strings names,selected;bool noDnd=true;std::size_t units=0;std::int64_t total=0;
  for(std::size_t index=0;index<value.files->items.size();++index){
    const auto& file=value.files->items[index];
    if(file.path.size()>65536){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Original file path fact limit"});}
    if(file.index!=static_cast<int>(index)||file.bytes<0||file.bytes>std::numeric_limits<std::int64_t>::max()-total
        ||file.path.isEmpty()||file.path.endsWith('/')){return std::unexpected(invalid("Original indexed file geometry is invalid"));}
    total+=file.bytes;
    const auto name=text(file.path.mid(file.path.lastIndexOf('/')+1));
    if(name.size()>65536||name.size()>262144-units){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Original file name fact limit"});}
    units+=name.size();names.push_back(name);
    if(file.skipped){noDnd=false;}else{selected.push_back(name);}
  }
  if(total!=value.bytes){return std::unexpected(invalid("Original file geometry differs from details"));}
  put(out,"filecount",static_cast<std::int64_t>(names.size()));
  put(out,"filecountselected",static_cast<std::int64_t>(selected.size()));
  put(out,"filenames",std::move(names));put(out,"filenamesselected",std::move(selected));
  return noDnd;
}
void phase(c::Facts& out,const engine::ManagedDownloadStatus& value){
  using P=engine::ManagedPhase;
  const auto p=value.phase;
  put(out,"predicate.isforcestart",value.forceStart);
  put(out,"predicate.ispaused",value.manuallyPaused);
  put(out,"predicate.isstopped",p==P::Stopped&&!value.manuallyPaused);
  put(out,"predicate.isqueued",p==P::Queued);
  put(out,"predicate.isdownloading",p==P::Downloading);
  put(out,"predicate.isseeding",p==P::Seeding);
  put(out,"predicate.iserror",p==P::Failed);
  put(out,"predicate.isrunning",p!=P::Stopped&&p!=P::Paused&&p!=P::Failed&&p!=P::Queued);
  // The original seeding branch queries completeRecheckStatus, absent here.
  if(p!=P::Seeding){put(out,"predicate.ischecking",p==P::Checking);}
  if(value.checked){put(out,"predicate.iscomplete",value.selectedComplete);}
}
void original(c::Facts& out,const core::Document& document){
  if(const auto* info=document.find("info");info&&info->type==core::Node::Type::Dictionary){
    const auto* flag=info->find("private");
    // TOTorrentImpl1224: only a Long, narrowed to Java int, can set private.
    const bool priv=flag&&flag->type==core::Node::Type::Integer&&static_cast<std::uint32_t>(flag->integer)!=0;
    put(out,"predicate.isprivate",priv);
  }
  const auto* attributes=document.find("attributes");
  if(!attributes||attributes->type!=core::Node::Type::Dictionary){return;}
  const auto* networks=attributes->find("networks");
  if(!networks||networks->type!=core::Node::Type::List||networks->children.size()>3){return;}
  c::Strings values;std::set<std::string_view> seen;
  for(const auto& network:networks->children){
    if(network.type!=core::Node::Type::Bytes||!seen.insert(network.bytes).second){return;}
    if(network.bytes=="Public"){values.emplace_back(u"Public");}
    else if(network.bytes=="I2P"){values.emplace_back(u"I2P");}
    else if(network.bytes=="Tor"){values.emplace_back(u"Tor");}
    else{return;} // No canonical/default network is fabricated.
  }
  put(out,"networks",std::move(values));
}
} // namespace
c::Result<core::TorrentHash> constraintFactHash(const QString& hash){
  if(hash.size()!=40&&hash.size()!=64){return std::unexpected(unavailable("Constraint UI identity length is invalid"));}
  const auto hex=hash.toLatin1();const auto bytes=QByteArray::fromHex(hex);
  if(bytes.toHex().toUpper()!=hex){return std::unexpected(unavailable("Constraint UI identity is not canonical hex"));}
  auto value=core::TorrentHash::parse(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())));
  if(!value){return std::unexpected(unavailable("Constraint raw identity length is invalid"));}
  return *value;
}
c::Result<ConstraintOriginalState> captureConstraintOriginalState(
    const ConstraintFactStamp& stamp,const storage::TorrentStateFile& state){
  if(!valid(stamp)||QString::fromStdString(state.hash().hex())!=stamp.binding.target.hash){
    return std::unexpected(unavailable("Original state identity/stamp is stale"));
  }
  return ConstraintOriginalState{stamp,state.document()};
}
c::Result<ConstraintFactSnapshot> captureConstraintFacts(
    const ConstraintFactStamp& current,const ConstraintFactInputs& input){
  if(!valid(current)||input.stamp!=current){return std::unexpected(unavailable("Constraint target/session/turn is stale"));}
  const auto& binding=current.binding;const auto& target=binding.target;
  if(!input.library||input.library->revision!=target.generation||input.library->items.size()>ui::LibraryModel::MaximumRows){
    return std::unexpected(unavailable("Constraint library revision is unavailable"));
  }
  const ui::LibraryItem* selected=nullptr;
  for(const auto& row:input.library->items){if(row.hash==target.hash){
    if(selected){return std::unexpected(invalid("Ambiguous constraint library identity"));}selected=&row;
  }}
  if(!selected||selected->providerAdmission!=binding.providerAdmission||selected->providerGeneration!=binding.providerGeneration){
    return std::unexpected(unavailable("Constraint provider admission/generation is stale"));
  }
  if(input.managed&&input.i2p){return std::unexpected(invalid("Two providers cannot own the selected fact row"));}
  if(binding.providerAdmission&&!input.managed&&!input.i2p){
    return std::unexpected(unavailable("Advertised constraint provider observation is missing"));
  }
  if(!binding.providerAdmission&&(input.managed||input.i2p)){
    return std::unexpected(unavailable("Constraint provider observation has no current library binding"));
  }
  // Public/mixed Host rows use columnLayoutSession as their provider generation.
  // The managed DTO has no independent generation field. Private I2P instead
  // supplies and verifies its actual group generation below.
  if(input.managed&&binding.providerGeneration!=binding.session){
    return std::unexpected(unavailable("Managed constraint provider belongs to another profile session"));
  }
  ConstraintFactSnapshot result;result.stamp_=current;auto& facts=result.facts_;
  if(selected->bytes&&*selected->bytes>=0){size(facts,*selected->bytes);}
  if(selected->savedPosition&&*selected->savedPosition>0){put(facts,"position",static_cast<std::int64_t>(*selected->savedPosition));}
  bool noDnd=false;
  if(input.details){
    if(selected->bytes&&*selected->bytes!=input.details->bytes){return std::unexpected(unavailable("Original detail/library sizes differ"));}
    auto checked=details(facts,*input.details,target);if(!checked){return std::unexpected(checked.error());}noDnd=*checked;
  }
  if(input.original){
    if(input.original->stamp!=current){return std::unexpected(unavailable("Original state turn is stale"));}
    original(facts,input.original->document);
  }
  if(!binding.providerAdmission&&selected->savedSelected){
    const auto& saved=*selected->savedSelected;
    // Already source-qualified original cached/no-DiskManager DND projection.
    if(saved.remaining&&*saved.remaining>=0){put(facts,"remaining",static_cast<std::int64_t>(*saved.remaining));}
    if(saved.percentThousandths&&*saved.percentThousandths>=0&&*saved.percentThousandths<=1000){
      put(facts,"percent",static_cast<float>(*saved.percentThousandths)/10.0f);
    }
  }
  if(!binding.providerAdmission&&selected->sent&&*selected->sent>=0){
    put(facts,"uploaded",static_cast<std::int64_t>(*selected->sent));
  }
  if(input.managed){
    const auto& m=*input.managed;
    if(!binding.providerAdmission||QString::fromStdString(m.hash.hex())!=target.hash
        ||m.admission!=binding.providerAdmission||!m.retained){return std::unexpected(unavailable("Managed fact admission is stale"));}
    if(m.bytes<0||m.completed<0||m.completed>m.bytes){return std::unexpected(invalid("Managed original byte range is invalid"));}
    if(selected->bytes&&*selected->bytes!=m.bytes){return std::unexpected(unavailable("Managed/library geometry differs"));}
    size(facts,m.bytes);phase(facts,m);
    if(m.cumulativeSent&&*m.cumulativeSent>=0){put(facts,"uploaded",*m.cumulativeSent);}
    if(m.queuePosition&&*m.queuePosition){put(facts,"position",static_cast<std::int64_t>(*m.queuePosition));}
    if(m.queueIdleUploadSeconds){put(facts,"upidle",*m.queueIdleUploadSeconds<0?std::numeric_limits<std::int64_t>::max():static_cast<std::int64_t>(*m.queueIdleUploadSeconds));}
    if(m.checked&&m.selectedComplete){put(facts,"percent",100.0f);put(facts,"remaining",std::int64_t{0});}
    else if(m.checked&&noDnd&&input.details&&input.details->bytes==m.bytes){percent(facts,m.bytes,m.bytes-m.completed);}
    // cumulativeReceived lacks SAVED discarded/hashfail prefixes; do not emit
    // downloaded or derive ratio from payload totals. Queue's ratio is separate.
  }
  if(input.i2p){
    const auto& row=*input.i2p;const auto& m=row.value;
    if(!binding.providerAdmission||QString::fromStdString(row.hash.hex())!=target.hash||m.admission!=binding.providerAdmission
        ||m.generation!=binding.providerGeneration||m.phase!=OwnedI2pService::Phase::Active||!m.current||m.joined){
      return std::unexpected(unavailable("Private I2P fact source is stale"));
    }
    if(m.cumulativeSent&&*m.cumulativeSent>=0){put(facts,"uploaded",*m.cumulativeSent);}
    if(m.checked){put(facts,"predicate.iscomplete",m.selectedComplete);}
    if(m.checked&&m.selectedComplete){put(facts,"percent",100.0f);put(facts,"remaining",std::int64_t{0});}
    // queued/seeding alone do not identify saved manual pause/error/force state.
    // Public+I2P mixed rows use their ONE ManagedDownloadStatus above.
  }
  if(input.queue){
    if(!binding.providerAdmission||!input.queue->sequence||input.queue->rows.size()>4096){return std::unexpected(unavailable("Queue fact source is unavailable"));}
    const engine::ExternalQueueRow* row=nullptr;
    for(const auto& candidate:input.queue->rows){if(QString::fromStdString(candidate.binding.hash.hex())==target.hash){
      if(row||candidate.binding.admission!=binding.providerAdmission||candidate.facts.token!=binding.providerAdmission){
        return std::unexpected(unavailable("Queue fact identity/admission differs"));}row=&candidate;
    }}
    if(row){
      const auto& q=row->facts;
      if(q.shareRatio){
        if(*q.shareRatio==-1){put(facts,"shareratio",static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()));}
        else{put(facts,"shareratio",static_cast<float>(*q.shareRatio)/1000.0f);}
      }
      if(q.secondsDownloading){put(facts,"downloadingfor",*q.secondsDownloading);}
      if(q.secondsSeeding){put(facts,"seedingfor",*q.secondsSeeding);}
    }
  }
  std::size_t factUnits=0;
  for(const auto& fact:facts.values){
    const auto count=[&](const c::Text& value){
      if(value.size()>65536||value.size()>262144-factUnits){return false;}
      factUnits+=value.size();return true;
    };
    if(const auto* value=std::get_if<c::Text>(&fact.value);value&&!count(*value)){
      return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Constraint fact text limit"});
    }
    if(const auto* values=std::get_if<c::Strings>(&fact.value)){
      if(values->size()>4096){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Constraint fact list limit"});}
      for(const auto& value:*values){if(!count(value)){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Constraint fact text limit"});}}
    }
  }
  return result;
}
c::Result<c::Value> evaluateCurrentConstraint(const c::Expression& expression,
    const ConstraintFactStamp& current,const ConstraintFactSnapshot& snapshot){
  if(!valid(current)||current!=snapshot.stamp()){return std::unexpected(unavailable("Constraint fact snapshot is stale"));}
  if(snapshot.hasTagScope()){return std::unexpected(unavailable("Current tag source generation is required for tag facts"));}
  return c::evaluate(expression,snapshot.facts());
}
#include "tag_membership_facts.inc"
} // namespace biglybtc::app
