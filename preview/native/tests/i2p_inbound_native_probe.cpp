// Opt-in test executable: real original-manager -> source-bound inbound DHT.
// No payload/torrent owner, raw packet injection, manual LS2 store or grant DTO.
#include "i2p_inbound_source_driver.hpp"
#include "profile.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <cstdio>
#include <future>
#include <limits>
#include <sys/stat.h>
namespace i2p=biglybtc::services::i2p;namespace cp=i2p::i2cp;namespace d=i2p::dht;
namespace provider=i2p::provider;namespace storage=biglybtc::storage;
using Driver=biglybtc::p07_test::InboundSourceDriver;
namespace {
constexpr std::uint16_t QueryPort=18301;constexpr int MaxCommands=16;
void require(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
QByteArray read(const QString& path,qint64 bound){QFile file(path);require(file.open(QIODevice::ReadOnly)&&file.size()>0&&file.size()<=bound,"Bounded fixture input unavailable");auto bytes=file.readAll();require(file.error()==QFileDevice::NoError,"Fixture input read failed");return bytes;}
void write(const QString& path,const QByteArray& bytes){QSaveFile file(path);require(file.open(QIODevice::WriteOnly)&&file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner),"Private fixture output unavailable");require(file.write(bytes)==bytes.size()&&file.commit(),"Fixture atomic output failed");}
void json(const QString& path,QJsonObject value){write(path,QJsonDocument(value).toJson());}
QJsonObject object(const QString& path){QJsonParseError error;const auto doc=QJsonDocument::fromJson(read(path,65536),&error);require(error.error==QJsonParseError::NoError&&doc.isObject(),"Invalid fixture JSON object");return doc.object();}
QString digest(const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
QByteArray b(const QByteArray& value){return QByteArray::number(value.size())+':'+value;}
void directory(const QString& path){require(!QFileInfo::exists(path)&&QDir().mkdir(path)&&::chmod(path.toUtf8().constData(),0700)==0,"Fresh private profile directory required");}
QJsonObject observation(const d::Actor::InboundObservation& value){return {{"requested",value.requested},{"ready",value.ready},{"retained",value.retained},{"sourceCurrent",value.sourceCurrent},{"grantActive",value.grantActive},{"scope",QString::number(value.scope)},{"requests",QString::number(value.requests)},{"replyPrepared",QString::number(value.replyPrepared)},{"tokenRejected",QString::number(value.tokenRejected)},{"tokenBindings",static_cast<qint64>(value.tokenBindings)},{"peerKeys",static_cast<qint64>(value.peerKeys)},{"peerValues",static_cast<qint64>(value.peerValues)}};}
struct CounterSample {std::uint64_t requests,replies,rejected;};
struct ExpirySamples {
  qint64 previous=-1,lastEmptyBeforeQueue=-1,firstQueued=-1,firstEmpty=-1,maxGap=0;
  bool observe(qint64 at,bool queued){
    if(at<0||(previous>=0&&at<previous)){return false;}
    if(previous>=0){maxGap=std::max(maxGap,at-previous);}previous=at;
    if(firstQueued<0){if(queued){firstQueued=at;}else{lastEmptyBeforeQueue=at;}}
    else if(!queued&&firstEmpty<0){firstEmpty=at;}
    return true;
  }
  bool complete()const{return lastEmptyBeforeQueue>=0&&firstQueued>=0&&firstEmpty>=0;}
  // Arrival is later than the last empty sample and no later than firstQueued.
  // The fixed fastReceive deadline is arrival+30s, not firstQueued+30s.
  bool deadlineCompatible()const{return complete()&&firstEmpty-lastEmptyBeforeQueue>=30000;}
};
CounterSample sample(const d::Actor::InboundObservation& value){return {value.requests,value.replyPrepared,value.tokenRejected};}
bool validRejectedInterval(CounterSample before,CounterSample after){
  constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
  if(after.requests==maximum||after.replies==maximum||after.rejected==maximum||after.requests<before.requests||after.replies<before.replies||after.rejected<before.rejected){return false;}
  const auto requests=after.requests-before.requests,replies=after.replies-before.replies,rejected=after.rejected-before.rejected;
  // Original managers also issue background valid requests. Every counted
  // outcome must still be accounted for, with exactly one token rejection and
  // at least one successful reply for the separately verified exact readback.
  return rejected==1&&replies>=1&&requests>replies&&requests-replies==1;
}
int checkCounterInterval(){
  QJsonArray cases;const CounterSample original{11,11,0};bool ok=true;
  const auto check=[&](const char* name,CounterSample before,CounterSample after,bool expected){const auto actual=validRejectedInterval(before,after);ok=ok&&(actual==expected);cases.append(QJsonObject{{"case",name},{"expected",expected},{"actual",actual}});};
  check("one reply plus rejection",original,{13,12,1},true);
  check("actual original-manager regression",original,{18,17,1},true);
  check("additional valid replies accounted",original,{23,22,1},true);
  check("missing fresh reply",original,{12,11,1},false);
  check("unaccounted request outcome",original,{18,16,1},false);
  check("multiple token rejections",original,{14,12,2},false);
  check("counter regression",original,{10,11,1},false);
  check("missing token rejection",original,{12,12,0},false);
  constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
  check("saturated interval is ambiguous",{maximum-2,maximum-2,0},{maximum,maximum-1,1},false);
  check("unsaturated near maximum",{maximum-3,maximum-3,0},{maximum-1,maximum-2,1},true);
  QJsonArray expiryCases;const auto expiryCheck=[&](const char* name,bool actual){ok=ok&&actual;expiryCases.append(QJsonObject{{"case",name},{"passed",actual}});};
  ExpirySamples normal;bool normalClock=normal.observe(0,false);for(qint64 at=25;at<30000;at+=25){normalClock=normal.observe(at,true)&&normalClock;}expiryCheck("observed interval may be less than30s",normalClock&&normal.observe(30000,false)&&normal.deadlineCompatible()&&normal.firstEmpty-normal.firstQueued==29975&&normal.maxGap==25);
  ExpirySamples early;expiryCheck("early empty is never rewritten by later polling",early.observe(0,false)&&early.observe(25,true)&&early.observe(1000,false)&&early.observe(40000,false)&&early.firstEmpty==1000&&!early.deadlineCompatible());
  ExpirySamples absent;expiryCheck("missing actual queue cannot qualify",absent.observe(0,false)&&absent.observe(40000,false)&&!absent.complete()&&!absent.deadlineCompatible());
  ExpirySamples clock;expiryCheck("sample clock cannot regress",clock.observe(25,false)&&!clock.observe(24,true)&&clock.firstQueued<0);
  ExpirySamples delayed;expiryCheck("large sample gap is reported without an exact residence claim",delayed.observe(0,false)&&delayed.observe(25,true)&&delayed.observe(60000,false)&&delayed.deadlineCompatible()&&delayed.maxGap==59975&&delayed.firstEmpty-delayed.firstQueued==59975);
  QFile output;if(!output.open(stdout,QIODevice::WriteOnly)){return 1;}const auto bytes=QJsonDocument(QJsonObject{{"status",ok?"PASS_PURE_COUNTER_INTERVAL_REGRESSION":"FAIL"},{"cases",cases},{"expiry_observation_cases",expiryCases},{"network_or_source_effects",false}}).toJson();if(output.write(bytes)!=bytes.size()||!output.flush()){return 1;}return ok?0:1;
}
struct Prepared {std::shared_ptr<provider::ProfileSource> source;std::optional<i2p::ClientIdentity> identity;};
Prepared prepare(){
  const QString profile="/work/inbound-profile";directory(profile);directory(profile+"/plugins");directory(profile+"/plugins/azneti2phelper");directory(profile+"/active");
  auto key=read("/work/pure-known-native.keys",1024);struct Clean{QByteArray& value;~Clean(){OPENSSL_cleanse(value.data(),static_cast<std::size_t>(value.size()));}} clean{key};
  auto identity=i2p::decodeClientIdentity({reinterpret_cast<const i2p::Byte*>(key.constData()),static_cast<std::size_t>(key.size())});require(identity.has_value(),"Original generated Native key invalid");const auto destination=read("/work/pure-known-native.dest",1024);
  require(identity->destination()->wire==i2p::Bytes(reinterpret_cast<const i2p::Byte*>(destination.constData()),reinterpret_cast<const i2p::Byte*>(destination.constData()+destination.size())),"Original generated Native public identity differs");
  write(profile+"/plugins/azneti2phelper/dest_key1.dat",key);
  // Lexically sorted original settings, empty inventory and empty metadata dir;
  // no Public Host settings, active torrent state, payload directory or grant.
  QByteArray config="d";for(const auto* name:{"Plugin.azneti2phelper.azi2phelper.dht.enable","Plugin.azneti2phelper.azi2phelper.pure.enabled","Plugin.azneti2phelper.enable","PluginInfo.azneti2phelper.enabled"}){config+=b(name)+"i1e";}config+="e";
  write(profile+"/biglybt.config",config);write(profile+"/downloads.config","d9:downloadslee");
  auto lease=storage::ProfileLease::acquire(profile.toStdString());require(lease.has_value(),"Owned generated profile lease unavailable");auto source=provider::ProfileSource::createSession(*lease);require(source.has_value(),"Generated profile session source refused");
  auto consumed=(*source)->takeIdentity();require(consumed.has_value(),"Source one-shot original identity refused");return {*source,std::move(*consumed)};
}
d::Contact bootstrap(){const auto compact=read("/work/bootstrap.node",54);auto node=d::decodeContact({reinterpret_cast<const i2p::Byte*>(compact.constData()),static_cast<std::size_t>(compact.size())});require(node.has_value(),"Actual original secure bootstrap invalid");const auto wire=read("/work/bootstrap.dest",1024);auto destination=i2p::decodeDestination({reinterpret_cast<const i2p::Byte*>(wire.constData()),static_cast<std::size_t>(wire.size())});require(destination&&destination->hash==node->hash,"Actual bootstrap identity differs");node->destination=std::move(*destination);return std::move(*node);}
cp::SessionOptions sessionOptions(){return {{"inbound.length","0"},{"outbound.length","1"},{"inbound.lengthVariance","0"},{"outbound.lengthVariance","0"},{"inbound.quantity","1"},{"outbound.quantity","1"},{"inbound.backupQuantity","0"},{"outbound.backupQuantity","0"},{"inbound.allowZeroHop","true"},{"outbound.allowZeroHop","false"},{"i2cp.leaseSetType","3"},{"i2cp.leaseSetEncType","4"},{"i2cp.dontPublishLeaseSet","false"},{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}};}
struct Mutation {std::uint64_t before=0,after=0;};
Mutation replaceKey(const std::shared_ptr<provider::ProfileSource>& source){auto gate=source->lease()->sourceAccess(true);require(gate.has_value(),"Actual mutation source access unavailable");const QString path="/work/inbound-profile/plugins/azneti2phelper/dest_key1.dat";struct stat before{},after{};require(::stat(path.toUtf8().constData(),&before)==0,"Fixture key inode unavailable");auto bytes=read(path,1024);struct Clean{QByteArray& value;~Clean(){OPENSSL_cleanse(value.data(),static_cast<std::size_t>(value.size()));}} clean{bytes};write(path,bytes);require(::stat(path.toUtf8().constData(),&after)==0&&before.st_dev==after.st_dev&&before.st_ino!=after.st_ino,"Actual generated key replacement not observed");return {static_cast<std::uint64_t>(before.st_ino),static_cast<std::uint64_t>(after.st_ino)};}
}
int main(int argc,char** argv){
  QCoreApplication app(argc,argv);if(app.arguments().size()==2&&app.arguments()[1]=="--check-counter-interval"){return checkCounterInterval();}if(app.arguments().size()!=3||app.arguments()[2]!="/work"){return 2;}bool parsed=false;const auto port=app.arguments()[1].toUInt(&parsed);if(!parsed||port<1024||port>65535){return 2;}
  QElapsedTimer elapsed,heartbeat;elapsed.start();heartbeat.start();QTimer poll;cp::InitialChannel channel;std::unique_ptr<Driver> driver;std::unique_ptr<d::Actor> actor;std::optional<Prepared> prepared;
  auto preparing=std::async(std::launch::async,prepare);std::future<Mutation> mutation;std::optional<Mutation> mutated;
  bool done=false,success=false,createQueued=false,actorStarted=false,ready=false,baseline=false,badChecked=false,closedChecked=false,resumed=false,sourceLoss=false,sourceFailed=false;QString reason,command;int next=1;QByteArray commandBytes;std::optional<d::Actor::InboundObservation> beforeBad,beforeSource,resumeStart;
  bool expiryHeld=false,expiryDrained=false,expiryRecovered=false;
  std::optional<d::Actor::InboundObservation> beforeExpiry;
  ExpirySamples expirySamples;
  std::uint64_t expiryRevision=0,expiryDrops=0,expiryHeldTurns=0;
  std::optional<std::uint16_t> expirySession;
  const auto finish=[&](bool ok,const QString& why){if(done){return;}done=true;success=ok;reason=why.left(512);poll.stop();json("/work/inbound-native-terminal.json",{{"phase","before-source-worker-joins"},{"elapsed_ms",elapsed.elapsed()},{"reason",reason}});if(actor){actor->close();}if(driver){driver->withdraw();}else{channel.close();}app.exit(ok?0:1);};
  const auto step=[&](const QString& name,QJsonObject extra=QJsonObject{}){extra.insert("status","ACTUAL_SOURCE_BOUND_INBOUND_CONTROL");extra.insert("sequence",next);extra.insert("command",name);extra.insert("command_sha256",digest(commandBytes));extra.insert("elapsed_ms",elapsed.elapsed());if(actor){extra.insert("observation",observation(actor->inboundObservation()));}json(QStringLiteral("/work/inbound-native-step-%1.json").arg(next),extra);++next;command.clear();commandBytes.clear();};
  QObject::connect(&poll,&QTimer::timeout,&app,[&]{if(done){return;}try{
    require(elapsed.elapsed()<1000000,"Absolute inbound Native deadline");
    if(!prepared){if(preparing.wait_for(std::chrono::seconds(0))!=std::future_status::ready){return;}prepared.emplace(preparing.get());driver=std::make_unique<Driver>(prepared->source,channel);require(channel.begin({QHostAddress::LocalHost,static_cast<quint16>(port),"2.13.0",false,30000}),"Real source-bound I2CP begin refused");}
    if(driver->observation().failed&&!sourceLoss){throw std::runtime_error("Unexpected source-permit failure");}
    if(!sourceLoss&&(channel.state()==cp::InitialChannel::State::Failed||channel.state()==cp::InitialChannel::State::Closed)){throw std::runtime_error("Unexpected real I2CP session loss");}
    if(!createQueued&&channel.state()==cp::InitialChannel::State::DateReceived){require(prepared->identity.has_value()&&driver->queueCreate(std::move(*prepared->identity),sessionOptions()),"Held original Create request refused");prepared->identity.reset();createQueued=true;}
    if(!actorStarted&&channel.leaseSetSubmission()){
      actor=std::make_unique<d::Actor>(channel);driver->setActor(actor.get());std::array<i2p::Byte,14> random{};require(RAND_bytes(random.data(),static_cast<int>(random.size()))==1,"Native NID entropy unavailable");auto local=d::makeNodeId(prepared->source->destination().hash,QueryPort,random);require(local.has_value(),"Native secure NID refused");d::Actor::Options options;options.localId=*local;options.queryPort=QueryPort;options.seeds.push_back(bootstrap());options.inbound=true;require(actor->begin(options),"Actual source-bound inbound Actor refused");
      d::Contact contact{*local,prepared->source->destination().hash,QueryPort,prepared->source->destination()};auto compact=d::encodeContact(contact);require(compact&&compact->size()==54,"Native secure compact identity refused");write("/work/inbound-native.node",QByteArray(reinterpret_cast<const char*>(compact->data()),static_cast<qsizetype>(compact->size())));const auto& destination=prepared->source->destination().wire;write("/work/inbound-native.dest",QByteArray(reinterpret_cast<const char*>(destination.data()),static_cast<qsizetype>(destination.size())));write("/work/inbound-target.hash",QCryptographicHash::hash("owned-inbound-seed-target",QCryptographicHash::Sha1));write("/work/inbound-downloader-target.hash",QCryptographicHash::hash("owned-inbound-downloader-target",QCryptographicHash::Sha1));actorStarted=true;
    }
    if(!actorStarted){if(channel.state()==cp::InitialChannel::State::Failed||channel.state()==cp::InitialChannel::State::Closed){throw std::runtime_error("Actual I2CP session startup failed");}return;}
    const auto current=actor->inboundObservation();const auto held=driver->lastHeldInbound();
    if(expiryHeld){
      require(beforeExpiry&&current.scope==beforeExpiry->scope&&current.ready&&current.requested&&current.retained&&current.sourceCurrent&&channel.socketActive()&&channel.state()==cp::InitialChannel::State::SessionCreated&&channel.sessionId()==expirySession,"Receive-only expiry lost its live source/session/scope");
      require(prepared->source->lease()->sourceRevision()==expiryRevision&&channel.receiverObservation().dropped==expiryDrops,"Receive-only expiry source changed or receiver dropped input");
      require(driver->observation().inboundPaused&&!driver->observation().paused&&actor->observation().phase!=d::Phase::Running,"Receive-only expiry consumer barrier differs");
      require(current.requests==beforeExpiry->requests&&current.replyPrepared==beforeExpiry->replyPrepared&&current.tokenRejected==beforeExpiry->tokenRejected,"Receive-only expiry Actor processed queued input");
      const auto queued=channel.dhtScopeObservation(current.scope).queued;
      const bool firstQueued=queued&&expirySamples.firstQueued<0;require(expirySamples.observe(elapsed.elapsed(),queued!=0),"Receive expiry sample clock regressed");
      if(firstQueued){json("/work/inbound-expiry-queued.json",{{"status","ACTUAL_DHT_QUEUE_HELD_WITH_LIVE_SOURCE"},{"last_empty_before_first_queue_ms",expirySamples.lastEmptyBeforeQueue},{"first_observed_queued_ms",expirySamples.firstQueued},{"queued",static_cast<qint64>(queued)},{"observation",observation(current)},{"source_worker_continues",true},{"exact_commanded_transaction_identified",false}});}
      // Normal dhtDeadline runs from the channel timer or an existing effect
      // flush. Observation is passive; this probe never takes or purges input.
      if(expirySamples.complete()){require(expirySamples.deadlineCompatible(),"Queue emptied before the pinned receive-deadline lower bound");expiryDrained=true;}
    }
    if(!ready&&current.ready&&current.retained&&current.sourceCurrent&&current.scope&&held&&held->ready&&held->grantActive&&held->scope==current.scope){ready=true;json("/work/inbound-native-ready.json",{{"status","ACTUAL_LOCAL_LS2_SOURCE_HELD_INBOUND_NODE_READY"},{"query_port",QueryPort},{"reply_port",QueryPort+1},{"ready",observation(current)},{"actual_held_observation",observation(*held)},{"remote_LS2_publication_claimed",false},{"torrent_payload_grants",0}});}
    if(heartbeat.elapsed()>=1000){heartbeat.restart();const auto state=driver->observation();json("/work/inbound-native-progress.json",{{"elapsed_ms",elapsed.elapsed()},{"observation",observation(current)},{"held_turns",QString::number(state.heldTurns)},{"create_requests",QString::number(state.createRequests)},{"permit_phase",static_cast<int>(state.phase)},{"validation_failed",state.validationFailed},{"command",command}});}
    if(!ready){return;}
    if(command.isEmpty()){
      require(next<=MaxCommands,"Bounded inbound control command count exhausted");const auto path=QStringLiteral("/work/inbound-native-command-%1.json").arg(next);if(!QFileInfo::exists(path)){return;}commandBytes=read(path,4096);const auto value=object(path);require(value.size()==2&&value["sequence"].toInteger()==next&&value["command"].isString(),"Invalid exact inbound command");command=value["command"].toString();require(command=="snapshot"||command=="bad-token-baseline"||command=="bad-token-check"||command=="expiry-hold"||command=="expiry-check"||command=="expiry-resume"||command=="expiry-recovered"||command=="close"||command=="resume"||command=="source-loss"||command=="stop","Unsupported fixed inbound control");
    }
    if(command=="snapshot"){step(command);}
    else if(command=="bad-token-baseline"){
      require(!baseline&&current.ready&&current.sourceCurrent&&current.peerKeys==2&&current.peerValues==2&&current.tokenBindings>=2,"Original two-target baseline missing");beforeBad=current;baseline=true;step(command);
    }else if(command=="bad-token-check"){
      require(baseline&&!badChecked&&beforeBad&&current.scope==beforeBad->scope&&current.ready&&current.sourceCurrent,"Current original baseline lost");json("/work/inbound-counter-interval.json",{{"before",observation(*beforeBad)},{"after",observation(current)},{"interval_valid",validRejectedInterval(sample(*beforeBad),sample(current))},{"extra_valid_requests_allowed",true},{"exact_original_peer_readback_still_required",true}});require(validRejectedInterval(sample(*beforeBad),sample(current)),"Actual invalid-token/refetch outcomes differ");require(current.peerKeys==beforeBad->peerKeys&&current.peerValues==beforeBad->peerValues&&current.tokenBindings==beforeBad->tokenBindings,"Bad token changed stored peer/binding counts");badChecked=true;step(command,{{"before",observation(*beforeBad)},{"actual_binding_rejection_delta",1},{"unchanged_store_counts",true},{"original_exact_peer_readback_required",true}});
    }else if(command=="expiry-hold"){
      require(badChecked&&!beforeExpiry&&!closedChecked&&current.ready&&current.sourceCurrent&&channel.socketActive()&&channel.receiverObservation().fast&&channel.dhtScopeObservation(current.scope).queued==0&&channel.dhtScopeObservation(current.scope).writeSlots==8&&actor->observation().phase!=d::Phase::Running,"Receive-only expiry requires an idle real fastReceive server");
      beforeExpiry=current;expiryRevision=prepared->source->lease()->sourceRevision();expiryDrops=channel.receiverObservation().dropped;expiryHeldTurns=driver->observation().heldTurns;expirySession=channel.sessionId();require(expirySession.has_value(),"Receive-only expiry lacks actual I2CP session");require(expirySamples.observe(elapsed.elapsed(),false),"Initial receive expiry sample refused");driver->pauseInbound(true);driver->request();expiryHeld=true;
      step(command,{{"sole_actor_consumer_paused",true},{"channel_effects_paused",false},{"source_revision",QString::number(expiryRevision)},{"receiver_drops",QString::number(expiryDrops)}});
    }else if(command=="expiry-check"){
      require(expiryHeld&&!expiryRecovered,"Receive-only expiry check ordering differs");if(!expiryDrained||channel.dhtScopeObservation(current.scope).queued){return;}
      require(driver->observation().heldTurns>expiryHeldTurns,"No genuine worker progress during receive-only expiry");
      step(command,{{"real_queue_expired",true},{"last_empty_before_first_queue_ms",expirySamples.lastEmptyBeforeQueue},{"first_observed_queued_ms",expirySamples.firstQueued},{"first_observed_empty_ms",expirySamples.firstEmpty},{"observed_queued_to_empty_ms",expirySamples.firstEmpty-expirySamples.firstQueued},{"arrival_lower_bound_to_empty_ms",expirySamples.firstEmpty-expirySamples.lastEmptyBeforeQueue},{"maximum_observation_gap_ms",expirySamples.maxGap},{"pinned_fast_receive_deadline_ms",30000},{"channel_socket_active",channel.socketActive()},{"source_worker_progress",true},{"unchanged_actor_counters",true},{"receiver_drop_delta",0},{"purge_caused_by_normal_channel_deadline_path",true},{"exact_commanded_transaction_identified",false}});
    }else if(command=="expiry-resume"){
      require(expiryHeld&&expiryDrained&&!expiryRecovered&&channel.dhtScopeObservation(current.scope).queued==0,"Receive-only expiry resume requires an empty expired queue");expiryHeld=false;driver->pauseInbound(false);step(command,{{"expired_queue_empty_before_resume",true},{"fresh_real_worker_requested",true}});
    }else if(command=="expiry-recovered"){
      require(beforeExpiry&&expiryDrained&&!expiryHeld&&!expiryRecovered,"Receive-only recovery ordering differs");
      require(current.scope==beforeExpiry->scope&&current.ready&&current.sourceCurrent&&channel.socketActive(),"Receive-only expiry lost its original live server");
      if(current.requests<=beforeExpiry->requests||current.replyPrepared<=beforeExpiry->replyPrepared){return;}expiryRecovered=true;step(command,{{"fresh_original_ping_result_required",true},{"fresh_request_and_reply_prepared",true},{"same_source_and_scope",true}});
    }else if(command=="close"){
      require(badChecked&&expiryRecovered&&!closedChecked&&!sourceLoss,"Close control ordering differs");actor->stopInbound();const auto closed=actor->inboundObservation();require(!closed.requested&&!closed.ready&&closed.retained&&closed.sourceCurrent&&channel.socketActive(),"Explicit server stop did not preserve a live channel");closedChecked=true;step(command,{{"server_stop_only",true},{"channel_socket_active",channel.socketActive()}});
    }else if(command=="resume"){
      require(closedChecked&&!resumed&&!sourceLoss,"Resume control ordering differs");if(!resumeStart){require(actor->enableInbound(),"Explicit server resume refused");resumeStart=actor->inboundObservation();}const auto active=actor->inboundObservation();if(!active.ready||!active.sourceCurrent){return;}resumed=true;step(command,{{"fresh_server_observation",observation(active)}});
    }else if(command=="source-loss"){
      require(resumed,"Source control requires an actually resumed server");
      if(!sourceLoss){require(current.ready&&current.sourceCurrent&&current.replyPrepared>0,"Resumed server has no real reply before source loss");beforeSource=current;prepared->source->invalidate();driver->pause(true);sourceLoss=true;mutation=std::async(std::launch::async,replaceKey,prepared->source);}
      if(!mutated){if(mutation.wait_for(std::chrono::seconds(0))!=std::future_status::ready){return;}mutated=mutation.get();driver->pause(false);driver->request();}
      if(!driver->observation().validationFailed){return;}sourceFailed=true;require(!channel.socketActive()&&!actor->inboundObservation().ready,"Failed fresh validation retained live network effects");step(command,{{"before",observation(*beforeSource)},{"explicit_invalidation_before_worker",true},{"actual_key_inode_changed",mutated->before!=mutated->after},{"fresh_source_validation_failed",true},{"channel_socket_active",channel.socketActive()},{"kernel_watcher_latency_claimed",false}});
    }else if(command=="stop"){
      require(badChecked&&expiryRecovered&&closedChecked&&resumed&&sourceFailed,"Required inbound source/control phases incomplete");step(command);finish(true,"Original inbound counters, receive-only expiry and actual source withdrawal complete");
    }
  }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));}});
  poll.start(25);QTimer::singleShot(1000000,&app,[&]{finish(false,"Absolute inbound Native deadline");});app.exec();
  // No Qt wait on any worker/gate: all joins happen only after local network
  // withdrawal and exit from the event loop, before captured locals are freed.
  try{if(driver){driver->withdraw();driver->join();}if(mutation.valid()){(void)mutation.get();}if(preparing.valid()){prepared.emplace(preparing.get());}actor.reset();driver.reset();prepared.reset();auto released=storage::ProfileLease::acquire("/work/inbound-profile");require(released.has_value(),"Joined inbound source retained OFD lock");require(read("/work/inbound-profile/downloads.config",65536)=="d9:downloadslee"&&QDir("/work/inbound-profile/active").entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot).isEmpty()&&!QFileInfo::exists("/work/inbound-profile/payload"),"Inbound node created ordinary torrent or payload state");json("/work/inbound-native-result.json",{{"status",success?"PASS_ACTUAL_SOURCE_BOUND_INBOUND_NATIVE":"FAIL"},{"reason",reason},{"elapsed_ms",elapsed.elapsed()},{"commands",next-1},{"source_validation_failed",sourceFailed},{"receive_only_expiry_completed",expiryRecovered},{"exact_commanded_expiry_transaction_identified",false},{"independent_router_eof_fault_executed",false},{"worker_joins_complete",true},{"profile_lock_reacquired",true},{"payload_or_torrent_grants",0},{"source_grants_minted_by_fixture",false},{"requires_original_peer_result",true}});}catch(const std::exception& error){success=false;json("/work/inbound-native-result.json",{{"status","FAIL"},{"reason",QString::fromUtf8(error.what()).left(512)},{"worker_joins_complete",false}});}
  return success?0:1;
}
