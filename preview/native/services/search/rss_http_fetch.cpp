// SPDX-License-Identifier: GPL-2.0-or-later
#include "rss_http_fetch.hpp"
#include "subscription_metainfo_fetch.hpp"
#include "metasearch_http_fetch.hpp"
#include "metasearch_metainfo_fetch.hpp"
#include "saved_subscription_result_source.hpp"
#include "../http/http_response.hpp"
#include "../../engine/network_binding.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QHostAddress>
#include <QHostInfo>
#include <QList>
#include <QNetworkProxy>
#include <QPointer>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QStringDecoder>
#include <QTcpSocket>
#include <QTimer>
#include <QXmlStreamReader>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <variant>

namespace biglybtc::search {
namespace {
std::atomic<bool> activeSlot{false};
std::atomic<std::uint64_t> nextNonce{1};
using Phase=RssFetchPhase;
using Error=RssFetchError;
enum class BodyKind {RssDocument,MetasearchDocument,TorrentMetainfo};
bool pending(Phase phase){return phase==Phase::BeforeDNS||phase==Phase::BeforeConnect||phase==Phase::BeforeGET;}
bool publicAddress(const QHostAddress& value){
  if(value.isNull()||value.isLoopback()||value.isMulticast()||!value.scopeId().isEmpty()){return false;}
  if(value.protocol()==QAbstractSocket::IPv4Protocol){
    const auto a=value.toIPv4Address();const auto first=a>>24;
    return !(first==0||first==10||first==127||first>=224||(a&0xffc00000u)==0x64400000u
        ||(a&0xffff0000u)==0xa9fe0000u||(a&0xfff00000u)==0xac100000u
        ||(a&0xffff0000u)==0xc0a80000u||(a&0xffffff00u)==0xc0000000u
        ||(a&0xffffff00u)==0xc0000200u||(a&0xffffff00u)==0xc0586300u
        ||(a&0xfffe0000u)==0xc6120000u||(a&0xffffff00u)==0xc6336400u
        ||(a&0xffffff00u)==0xcb007100u);
  }
  if(value.protocol()!=QAbstractSocket::IPv6Protocol){return false;}
  const auto a=value.toIPv6Address();
  // Public global unicast only: no mapped IPv4, ULA, scoped/local or multicast.
  return (a[0]&0xe0)==0x20&&!(a[0]==0x20&&a[1]==0x02)
      &&!(a[0]==0x20&&a[1]==0x01&&((a[2]==0&&a[3]==0)||(a[2]==0x0d&&a[3]==0xb8)));
}
bool canonicalUrl(const QUrl& url){
  const auto encoded=url.toEncoded(QUrl::FullyEncoded);
  if(!url.isValid()||(url.scheme()!="http"&&url.scheme()!="https")||url.host().isEmpty()
      ||!url.userInfo().isEmpty()||url.authority(QUrl::FullyEncoded).contains('@')||url.hasFragment()
      ||encoded.isEmpty()||encoded.size()>2048||url.port(80)<=0||url.port(80)>65535
      ||!std::ranges::all_of(encoded,[](unsigned char c){return c>=33&&c<=126;})){return false;}
  auto host=url.host().toLower();while(host.endsWith('.')){host.chop(1);}
  if(host.endsWith(".onion")||host.endsWith(".i2p")||host.contains('%')){return false;}
  QHostAddress numeric;
  if(numeric.setAddress(host)){return publicAddress(numeric);}
  if(host.size()>253||!host.contains('.')||host.endsWith('.')){return false;}
  for(const auto suffix:{".onion",".i2p",".localhost",".local",".lan",".home",".internal",".invalid",".test",".example"}){
    if(host.endsWith(QLatin1StringView(suffix))){return false;}
  }
  for(const auto& label:host.split('.')){
    if(label.isEmpty()||label.size()>63||label.startsWith('-')||label.endsWith('-')){return false;}
    if(!std::ranges::all_of(label,[](QChar c){return (c>=u'a'&&c<=u'z')||(c>=u'0'&&c<=u'9')||c==u'-';})){return false;}
  }
  return std::ranges::any_of(host.section('.',-1),[](QChar c){return c>=u'a'&&c<=u'z';});
}
// Only a genuine saved metasearch query can retain the initial literal
// origin form. QUrl still owns authority/DNS/TLS and every redirected route.
struct MetasearchInitialTarget {QByteArray bytes;};
std::expected<MetasearchInitialTarget,Error> metasearchInitialTarget(const SavedMetasearchSourceSnapshot& source){
  const auto raw=QByteArray::fromStdString(source.description().literalGetUrl);
  const auto url=QUrl::fromEncoded(raw,QUrl::StrictMode);
  if(!canonicalUrl(url)||raw.contains('#')||raw.contains('\\')||!std::ranges::all_of(raw,[](unsigned char c){return c>=33&&c<=126;})){return std::unexpected(Error::UnsupportedRoute);}
  const auto hex=[](unsigned char c){return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');};
  for(qsizetype at=0;at<raw.size();++at){if(raw[at]=='%'){if(at+2>=raw.size()||!hex(static_cast<unsigned char>(raw[at+1]))||!hex(static_cast<unsigned char>(raw[at+2]))){return std::unexpected(Error::UnsupportedRoute);}at+=2;}}
  const auto scheme=url.scheme().toLatin1()+"://";if(!raw.startsWith(scheme)){return std::unexpected(Error::UnsupportedRoute);}auto end=scheme.size();while(end<raw.size()&&raw[end]!='/'&&raw[end]!='?'){++end;}
  if(raw.left(end)!=scheme+url.authority(QUrl::FullyEncoded).toLatin1()){return std::unexpected(Error::UnsupportedRoute);}
  auto target=raw.mid(end);if(target.isEmpty()||target.front()=='?'){target.prepend('/');}
  if(target.isEmpty()||target.front()!='/'||target.size()>4096){return std::unexpected(Error::Limit);}return MetasearchInitialTarget{std::move(target)};
}
QEvent::Type cancelEvent(){static const auto type=static_cast<QEvent::Type>(QEvent::registerEventType());return type;}
struct Mailbox {
  mutable std::mutex mutex;
  std::mutex receiverMutex;
  QObject* receiver=nullptr;
  std::atomic<bool> cancelled{false};
  std::atomic<Error> cancelReason{Error::Cancelled};
  RssFetchObservation view;
  std::chrono::steady_clock::time_point phaseStarted=std::chrono::steady_clock::now();
  QUrl finalUrl;
  QByteArray body;
  bool approvalInFlight=false;
  void cancel(std::optional<Error> reason=Error::Cancelled)noexcept{
    {std::lock_guard lock(mutex);
      if(reason&&!view.failure&&view.phase!=Phase::Complete&&view.phase!=Phase::Closed){view.failure=*reason;}
      cancelReason=view.failure.value_or(reason.value_or(Error::Closed));cancelled=true;
      if(view.failure&&view.phase!=Phase::Closed){view.phase=*view.failure==Error::Cancelled?Phase::Cancelled:Phase::Failed;}}
    if(!reason){return;} // Cleanup joins/destroys the context; do not invent a cancellation failure.
    try{std::lock_guard lock(receiverMutex);if(receiver){QCoreApplication::postEvent(receiver,new QEvent(cancelEvent()));}}catch(...){}
  }
};
struct Configuration {
  QUrl initial;
  RssGetDescription description;
  BodyKind bodyKind=BodyKind::RssDocument;
  QByteArray referer; // Only a real saved query/result supplies this owned header.
  std::optional<MetasearchInitialTarget> initialMetasearchTarget;
  QSslConfiguration tls;
  std::vector<QHostAddress> locals;
  bool ipv6=false;
  std::shared_ptr<const storage::ProfileLease> epoch; // sourceRevision() only on socket thread.
  std::uint64_t nonce=0;
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
  std::optional<RssHttpFetch::PrivateEndpointFixture> fixture;
#endif
};
// Direct-only specialization of the existing tracker socket/event-loop logic.
// No torrent request, auth retry, SOCKS, proxy factory or source IO is copied.
class Operation final:public QObject {
public:
  Operation(std::shared_ptr<Mailbox> box,Configuration config,QObject* parent)
      :QObject(parent),box_(std::move(box)),config_(std::move(config)),url_(config_.initial){
    visited_.push_back(url_.toEncoded());age_.start();deadline_.setSingleShot(true);
    QObject::connect(&deadline_,&QTimer::timeout,this,[this]{fail(box_->cancelled?Error::Cancelled:Error::Deadline);});
    phase(Phase::BeforeDNS);arm();
  }
  ~Operation()override{cancelLookup();clearSocket();}
  void cancel(){fail(box_->cancelReason.load());}
  void approve(Phase expected,std::uint64_t nonce,std::uint64_t hop,std::uint64_t revision){
    try{
    {std::lock_guard lock(box_->mutex);box_->approvalInFlight=false;}
    if(!current(hop)||nonce!=config_.nonce||phase_!=expected||!pending(expected)){return;}
    // A write after issuance invalidates this private approval. Re-poll the
    // same phase for a new exact source observation; no effect or stale grant.
    if(!revision||config_.epoch->sourceRevision()!=revision){return;}
    if(!arm()){return;}
    switch(expected){
      case Phase::BeforeDNS:resolve(revision);break;
      case Phase::BeforeConnect:connect(revision);break;
      case Phase::BeforeGET:send(revision);break;
      default:break;
    }
    }catch(...){fail(Error::Memory);}
  }
private:
  std::shared_ptr<Mailbox> box_;
  Configuration config_;
  QUrl url_;
  QList<QByteArray> visited_;
  QElapsedTimer age_;
  QTimer deadline_;
  QTcpSocket* socket_=nullptr;
  int lookup_=-1;
  std::uint64_t hop_=1;
  Phase phase_=Phase::BeforeDNS;
  bool finished_=false;
  QHostAddress remote_,local_;
  QByteArray received_;
  std::size_t transferred_=0,gets_=0;
  bool current(std::uint64_t hop)const noexcept{return !finished_&&phase_!=Phase::BeforeDelivery&&!box_->cancelled&&hop_==hop;}
  template<class F>void guarded(F&& fn)noexcept{try{fn();}catch(...){fail(Error::Memory);}}
  void phase(Phase value){
    phase_=value;std::lock_guard lock(box_->mutex);box_->view.phase=value;box_->view.hop=hop_;box_->phaseStarted=std::chrono::steady_clock::now();
    box_->view.gets=gets_;box_->view.responseOctets=transferred_;box_->finalUrl=url_;box_->approvalInFlight=false;
  }
  bool arm(){
    const auto left=config_.description.totalMillis-age_.elapsed();
    if(left<=0){fail(Error::Deadline);return false;}
    deadline_.start(static_cast<int>(std::min<qint64>(config_.description.phaseMillis,left)));return true;
  }
  void cancelLookup()noexcept{
    if(lookup_<0){return;}const int id=std::exchange(lookup_,-1);
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
    if(config_.fixture&&config_.fixture->cancelResolve){try{config_.fixture->cancelResolve(id);}catch(...){}return;}
#endif
    QHostInfo::abortHostLookup(id);
  }
  void clearSocket(){if(socket_){auto* old=std::exchange(socket_,nullptr);QObject::disconnect(old,nullptr,this,nullptr);old->abort();old->deleteLater();}}
  bool allowed(const QHostAddress& value)const{
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
    if(config_.fixture){return value.isLoopback()&&value==config_.fixture->endpoint;}
#endif
    return publicAddress(value);
  }
  void resolved(const QHostInfo& info,std::uint64_t hop){
    if(!current(hop)){return;}guarded([&]{lookup_=-1;
      if(info.error()!=QHostInfo::NoError||info.addresses().isEmpty()||info.addresses().size()>32){fail(Error::Dns);return;}
      for(const auto& candidate:info.addresses()){
        if(!allowed(candidate)||(!config_.ipv6&&candidate.protocol()==QAbstractSocket::IPv6Protocol)){continue;}
        const auto local=std::ranges::find_if(config_.locals,[&](const auto& a){return a.protocol()==candidate.protocol();});
        if(local!=config_.locals.end()){remote_=candidate;local_=*local;phase(Phase::BeforeConnect);arm();return;}
      }
      fail(Error::UnsupportedRoute);
    });
  }
  void resolve(std::uint64_t revision){
    if(!current(hop_)||config_.epoch->sourceRevision()!=revision){return;}
    QHostAddress numeric;
    if(numeric.setAddress(url_.host())){
      QHostInfo info;info.setAddresses({numeric});resolved(info,hop_);return;
    }
    phase(Phase::Resolving);
    {std::lock_guard lock(box_->mutex);if(box_->view.resolverSubmissions>=6){failUnlocked_=true;}else{++box_->view.resolverSubmissions;}}
    if(failUnlocked_){fail(Error::Limit);return;}
    const auto hop=hop_;
    // Public-only route/source was validated before this private phase. No
    // system/application proxy lookup, target fallback or credential callback.
    if(!current(hop_)||config_.epoch->sourceRevision()!=revision){
      {std::lock_guard lock(box_->mutex);--box_->view.resolverSubmissions;}
      if(!box_->cancelled){phase(Phase::BeforeDNS);}return;
    }
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
    if(config_.fixture&&config_.fixture->resolve){
      const QPointer<Operation> context(this);
      lookup_=config_.fixture->resolve(url_.host(),this,[context,hop](const QHostInfo& info){
        if(!context){return;}QMetaObject::invokeMethod(context,[context,hop,info]{if(context){context->resolved(info,hop);}},Qt::QueuedConnection);
      });
    }
    else
#endif
    {lookup_=QHostInfo::lookupHost(url_.host(),this,[this,hop](const QHostInfo& info){resolved(info,hop);});}
    if(lookup_<0){fail(Error::Dns);}
  }
  bool failUnlocked_=false;
  void connect(std::uint64_t revision){
    if(!current(hop_)||config_.epoch->sourceRevision()!=revision){return;}
    if(socket_||!allowed(remote_)||remote_.isNull()||local_.isNull()){fail(Error::UnsupportedRoute);return;}
    const bool encrypted=url_.scheme()=="https";const auto hop=hop_;
    if(encrypted){
      auto* ssl=new QSslSocket(this);ssl->setSslConfiguration(config_.tls);ssl->setPeerVerifyMode(QSslSocket::VerifyPeer);ssl->setPeerVerifyName(url_.host());socket_=ssl;
      QObject::connect(ssl,&QSslSocket::encrypted,this,[this,hop]{if(current(hop)){guarded([this]{phase(Phase::BeforeGET);arm();});}});
      QObject::connect(ssl,&QSslSocket::sslErrors,this,[this,hop](const QList<QSslError>&){if(current(hop)){fail(Error::Tls);}});
    }else{socket_=new QTcpSocket(this);}
    socket_->setProxy(QNetworkProxy::NoProxy);socket_->setReadBufferSize(static_cast<qint64>(config_.description.responseOctets));
    QObject::connect(socket_,&QTcpSocket::connected,this,[this,hop,encrypted]{if(current(hop)&&!encrypted){guarded([this]{phase(Phase::BeforeGET);arm();});}});
    QObject::connect(socket_,&QTcpSocket::readyRead,this,[this,hop]{if(current(hop)){guarded([this]{read(false);});}});
    QObject::connect(socket_,&QTcpSocket::disconnected,this,[this,hop]{if(current(hop)){guarded([this]{read(true);});}});
    QObject::connect(socket_,&QTcpSocket::errorOccurred,this,[this,hop](QAbstractSocket::SocketError error){if(current(hop)){guarded([&]{if(error==QAbstractSocket::RemoteHostClosedError){read(true);}else{fail(error==QAbstractSocket::SslHandshakeFailedError?Error::Tls:Error::Http);}});}});
    if(!socket_->bind(local_,0)){fail(Error::UnsupportedRoute);return;}
    if(!current(hop_)||config_.epoch->sourceRevision()!=revision){clearSocket();return;}
    phase(Phase::Connecting);
    const auto port=static_cast<quint16>(url_.port(encrypted?443:80));
    if(encrypted){static_cast<QSslSocket*>(socket_)->connectToHostEncrypted(remote_.toString(),port,url_.host());}
    else{socket_->connectToHost(remote_,port);}
  }
  void send(std::uint64_t revision){
    if(!current(hop_)||config_.epoch->sourceRevision()!=revision){return;}
    if(!socket_){fail(Error::Http);return;}
    auto path=url_.path(QUrl::FullyEncoded).toUtf8();if(path.isEmpty()){path="/";}
    if(url_.hasQuery()){path+='?';path+=url_.query(QUrl::FullyEncoded).toUtf8();}
    if(config_.initialMetasearchTarget){path=config_.initialMetasearchTarget->bytes;}
    auto host=url_.host().contains(':')?url_.host().toUtf8():QUrl::toAce(url_.host());if(host.contains(':')){host='['+host+']';}
    if(url_.port()!=-1){host+=':'+QByteArray::number(url_.port());}
    auto packet="GET "+path+" HTTP/1.1\r\nHost: "+host+"\r\nUser-Agent: BiglyBTC\r\nAccept-Encoding: gzip\r\nConnection: close\r\n\r\n";
    if(config_.bodyKind!=BodyKind::RssDocument&&!config_.referer.isEmpty()){
      packet.chop(2);packet+="Referer: "+config_.referer+"\r\n\r\n";
    }
    if(packet.size()>8192||gets_>=config_.description.maximumGets){fail(Error::Limit);return;}
    if(!current(hop_)||config_.epoch->sourceRevision()!=revision){return;}
    if(socket_->write(packet)!=packet.size()){fail(Error::Http);return;}
    ++gets_;phase(Phase::Reading);arm();
  }
  bool utf8ContentType()const{
    const auto end=received_.indexOf("\r\n\r\n");if(end<0){return true;}
    bool found=false;
    for(auto line:received_.left(end).split('\n')){
      if(!line.toLower().startsWith("content-type:")){continue;}if(found){return false;}found=true;
      const auto value=QString::fromLatin1(line.mid(13)).trimmed();
      const QRegularExpression charset(QStringLiteral("(?:^|;)\\s*charset\\s*=\\s*(?:\"([^\"]+)\"|([^;\\s]+))"),QRegularExpression::CaseInsensitiveOption);
      auto matches=charset.globalMatch(value);const auto match=matches.hasNext()?matches.next():QRegularExpressionMatch{};
      if(matches.hasNext()){return false;}
      if(value.contains(QStringLiteral("charset"),Qt::CaseInsensitive)&&!match.hasMatch()){return false;}
      if(match.hasMatch()){
        const auto encoding=match.captured(1).isEmpty()?match.captured(2):match.captured(1);
        if(encoding.compare(QStringLiteral("UTF-8"),Qt::CaseInsensitive)!=0&&encoding.compare(QStringLiteral("UTF8"),Qt::CaseInsensitive)!=0){return false;}
      }
    }return true;
  }
  void read(bool eof){
    if(phase_!=Phase::Reading||!socket_){if(eof){fail(Error::Http);}return;}
    if(age_.elapsed()>=config_.description.totalMillis){fail(Error::Deadline);return;}
    const auto available=socket_->bytesAvailable();const auto remaining=config_.description.responseOctets-transferred_;
    if(available<0||static_cast<std::uint64_t>(available)>remaining){fail(Error::Limit);return;}
    const auto fresh=socket_->read(static_cast<qint64>(remaining)+1);
    if(static_cast<std::size_t>(fresh.size())>remaining){fail(Error::Limit);return;}
    transferred_+=static_cast<std::size_t>(fresh.size());received_+=fresh;
    const auto header=received_.indexOf("\r\n\r\n");
    if((header<0&&static_cast<std::size_t>(received_.size())>config_.description.headerBytes)||(header>=0&&static_cast<std::size_t>(header)>config_.description.headerBytes)){fail(Error::Limit);return;}
    if(config_.bodyKind!=BodyKind::TorrentMetainfo&&!utf8ContentType()){fail(Error::Encoding);return;}
    const auto profile=config_.bodyKind==BodyKind::TorrentMetainfo?services::http::ResponseProfile::TorrentMetainfo:services::http::ResponseProfile::RssDocument;
    const auto value=services::http::decodeResponse(received_,eof,profile);
    if(value.unsupported){fail(Error::Http);return;}if(!value.complete){phase(Phase::Reading);return;}
    if(value.failed||value.basicAuthentication){fail(Error::Http);return;}
    if(!value.location.isEmpty()){
      if(value.location.size()>2048||!std::ranges::all_of(value.location,[](unsigned char c){return c>=33&&c<=126;})){fail(Error::UnsupportedRoute);return;}
      const auto next=url_.resolved(QUrl::fromEncoded(value.location,QUrl::StrictMode));
      if(!canonicalUrl(next)||next.scheme()!=url_.scheme()){fail(Error::UnsupportedRoute);return;}
      if(static_cast<std::size_t>(visited_.size())>=config_.description.maximumGets||visited_.contains(next.toEncoded())){fail(Error::Limit);return;}
      config_.initialMetasearchTarget.reset();
      cancelLookup();clearSocket();received_.clear();url_=next;visited_.push_back(next.toEncoded());++hop_;
      phase(Phase::BeforeDNS);arm();return;
    }
    // Withdraw callbacks/transport BEFORE exposing the body to the Host worker.
    cancelLookup();clearSocket();
    {std::lock_guard lock(box_->mutex);box_->body=value.bytes;}
    phase(Phase::BeforeDelivery);arm();
  }
  void fail(Error error)noexcept{
    if(finished_){return;}finished_=true;deadline_.stop();cancelLookup();clearSocket();
    try{std::lock_guard lock(box_->mutex);
      if(!box_->view.failure){box_->view.failure=error;}
      const auto first=*box_->view.failure;box_->cancelReason=first;box_->cancelled=true;
      if(box_->view.phase!=Phase::Closed){box_->view.phase=first==Error::Cancelled?Phase::Cancelled:Phase::Failed;}
      box_->view.gets=gets_;box_->view.responseOctets=transferred_;box_->approvalInFlight=false;box_->body.clear();}catch(...){}
  }
};
class NetworkThread final {
  struct Context final:QObject {
    QEventLoop& loop;
    std::unique_ptr<Operation> operation;
    explicit Context(QEventLoop& value):loop(value){}
    bool event(QEvent* value)override{if(value->type()==cancelEvent()){if(operation){operation->cancel();}return true;}return QObject::event(value);}
  };
  std::shared_ptr<Mailbox> box_;
  std::mutex mutex_;
  std::condition_variable ready_;
  Context* context_=nullptr;
  std::atomic<unsigned> queued_{0};
  bool started_=false;
  std::thread thread_;
public:
  NetworkThread(std::shared_ptr<Mailbox> box,Configuration config):box_(std::move(box)),thread_([this,config=std::move(config)]()mutable{
    QEventLoop loop;Context context(loop);
    {std::lock_guard lock(box_->receiverMutex);box_->receiver=&context;}
    try{context.operation=std::make_unique<Operation>(box_,std::move(config),&context);}
    catch(...){std::lock_guard lock(box_->mutex);box_->view.phase=Phase::Failed;box_->view.failure=Error::Memory;}
    {std::lock_guard lock(mutex_);context_=&context;started_=true;}ready_.notify_one();
    if(box_->cancelled&&context.operation){context.operation->cancel();}loop.exec();context.operation.reset();
    {std::lock_guard lock(box_->receiverMutex);box_->receiver=nullptr;}
    {std::lock_guard lock(mutex_);context_=nullptr;}
  }){std::unique_lock lock(mutex_);ready_.wait(lock,[this]{return started_;});}
  ~NetworkThread(){
    {std::lock_guard lock(mutex_);if(context_){auto* context=context_;QMetaObject::invokeMethod(context,[context]{context->operation.reset();context->loop.quit();},Qt::QueuedConnection);}}
    if(thread_.joinable()){thread_.join();}
  }
  bool approve(Phase phase,std::uint64_t nonce,std::uint64_t hop,std::uint64_t revision){
    std::lock_guard lock(mutex_);if(!context_||queued_.load()>=4){return false;}
    auto* context=context_;++queued_;
    const bool posted=QMetaObject::invokeMethod(context,[this,context,phase,nonce,hop,revision]{--queued_;if(context->operation){context->operation->approve(phase,nonce,hop,revision);}},Qt::QueuedConnection);
    if(!posted){--queued_;}return posted;
  }
};
} // namespace

struct RssHttpFetch::Impl {
  std::thread::id worker=std::this_thread::get_id();
  std::shared_ptr<const storage::ProfileLease> lease;
  // Closed owning source set. No virtual/callback source can mint a phase.
  using Source=std::variant<SavedRssSourceSnapshot,SavedSubscriptionSourceSnapshot,SavedSubscriptionResultSnapshot,SavedMetasearchSourceSnapshot,SavedMetasearchResultHandle>;
  struct AutomaticSubscription {SavedSubscriptionSelector selected;};
  struct AutomaticResult {SavedSubscriptionResultSelector selected;};
  struct MetasearchQuery {SavedMetasearchSelector selected;std::string query;};
  struct MetasearchResult {const SavedMetasearchResultHandle* selected=nullptr;};
  using Selected=std::variant<SavedRssSelector,SavedSubscriptionSelector,SavedSubscriptionResultSelector,AutomaticSubscription,AutomaticResult,MetasearchQuery,MetasearchResult>;
  std::optional<Source> source;
  BodyKind bodyKind=BodyKind::RssDocument;
  std::filesystem::path profile;
  std::stop_token receiverToken;
  engine::NetworkBinding binding;
  std::uint64_t session=0,nonce=0;
  std::shared_ptr<Mailbox> box=std::make_shared<Mailbox>();
  std::unique_ptr<NetworkThread> network;
  using StopCallback=std::stop_callback<std::function<void()>>;
  std::unique_ptr<StopCallback> receiverStop;
  bool slot=false,closed=false,delivered=false;
  std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
  using Fixture=PrivateEndpointFixture;
#else
  struct Fixture {};
#endif
  const RssGetDescription& description()const{
    return std::visit([](const auto& saved)->const RssGetDescription&{return saved.description();},*source);
  }
  bool automatic()const{
    return std::visit([](const auto& saved){using T=std::remove_cvref_t<decltype(saved)>;if constexpr(std::is_same_v<T,SavedSubscriptionSourceSnapshot>||std::is_same_v<T,SavedSubscriptionResultSnapshot>){return saved.automaticPolicy()!=nullptr;}else{return false;}},*source);
  }
  std::expected<std::uint64_t,SavedRssSourceError> sourceCurrent(
      const storage::ProfileLease& caller,std::uint64_t currentSession)const{
    return std::visit([&](const auto& saved){return saved.current(caller,currentSession);},*source);
  }
  static std::expected<Source,Error> readSource(const storage::ProfileLease& caller,
      const std::filesystem::path& path,std::uint64_t session,Selected selected){
    return std::visit([&](auto&& identity)->std::expected<Source,Error>{
      using Identity=std::remove_cvref_t<decltype(identity)>;
      if constexpr(std::is_same_v<Identity,SavedRssSelector>){
        auto saved=SavedRssSourceSnapshot::read(caller,path,session,std::move(identity),SavedRssReadReason::ExplicitOneShotDescription);
        if(!saved){return std::unexpected(Error::Source);}
        return Source{std::in_place_type<SavedRssSourceSnapshot>,std::move(*saved)};
      }else if constexpr(std::is_same_v<Identity,SavedSubscriptionSelector>){
        auto saved=SavedSubscriptionSourceSnapshot::read(caller,path,session,std::move(identity));
        if(!saved){return std::unexpected(Error::Source);}
        return Source{std::in_place_type<SavedSubscriptionSourceSnapshot>,std::move(*saved)};
      }else if constexpr(std::is_same_v<Identity,AutomaticSubscription>){
        auto saved=SavedSubscriptionSourceSnapshot::readAutomatic(caller,path,session,std::move(identity.selected));
        if(!saved){return std::unexpected(Error::Source);}
        return Source{std::in_place_type<SavedSubscriptionSourceSnapshot>,std::move(*saved)};
      }else if constexpr(std::is_same_v<Identity,SavedSubscriptionResultSelector>||std::is_same_v<Identity,AutomaticResult>){
        auto saved=[&]{if constexpr(std::is_same_v<Identity,AutomaticResult>){return SavedSubscriptionResultSnapshot::readAutomatic(caller,path,session,std::move(identity.selected));}else{return SavedSubscriptionResultSnapshot::read(caller,path,session,std::move(identity));}}();
        if(!saved){return std::unexpected(Error::Source);}
        return Source{std::in_place_type<SavedSubscriptionResultSnapshot>,std::move(*saved)};
      }else if constexpr(std::is_same_v<Identity,MetasearchQuery>){
        auto saved=SavedMetasearchSourceSnapshot::read(caller,path,session,std::move(identity.selected),std::move(identity.query));
        if(!saved){return std::unexpected(Error::Source);}
        return Source{std::in_place_type<SavedMetasearchSourceSnapshot>,std::move(*saved)};
      }else{
        static_assert(std::is_same_v<Identity,MetasearchResult>);
        if(!identity.selected){return std::unexpected(Error::Source);}
        auto saved=identity.selected->retainedCopy(caller,session);
        if(!saved){return std::unexpected(Error::Source);}
        return Source{std::in_place_type<SavedMetasearchResultHandle>,std::move(*saved)};
      }
    },std::move(selected));
  }
  ~Impl(){close();}
  Error stop(Error error){box->cancel(error);std::lock_guard lock(box->mutex);return box->view.failure.value_or(error);}
  void join(){network.reset();std::lock_guard lock(box->mutex);box->view.joined=true;}
  void close()noexcept{
    if(closed){return;}closed=true;box->cancel(std::nullopt);receiverStop.reset();receiverToken={};network.reset();source.reset();lease.reset();profile.clear();
    {std::lock_guard lock(box->mutex);box->view.joined=true;box->view.phase=Phase::Closed;box->body.clear();}
    if(slot){activeSlot=false;slot=false;}
  }
  static std::expected<std::unique_ptr<Impl>,Error> make(const storage::ProfileLease& caller,
      const std::filesystem::path& path,std::uint64_t session,Selected selector,
      std::stop_token stop,std::optional<Fixture> fixture={}){
    if(stop.stop_requested()){return std::unexpected(Error::Cancelled);}
    bool no=false;if(!activeSlot.compare_exchange_strong(no,true)){return std::unexpected(Error::Busy);}
    std::unique_ptr<Impl> result;
    try{
      result=std::make_unique<Impl>();result->slot=true;result->session=session;
      if(cancelEvent()<QEvent::User){return std::unexpected(Error::Memory);}
      auto retained=caller.retain();if(!retained){return std::unexpected(Error::Source);}result->lease=*retained;
      auto source=readSource(caller,path,session,std::move(selector));
      if(!source){return std::unexpected(Error::Source);}result->source.emplace(std::move(*source));
      if(std::holds_alternative<SavedSubscriptionResultSnapshot>(*result->source)||std::holds_alternative<SavedMetasearchResultHandle>(*result->source)){result->bodyKind=BodyKind::TorrentMetainfo;}
      else if(std::holds_alternative<SavedMetasearchSourceSnapshot>(*result->source)){result->bodyKind=BodyKind::MetasearchDocument;}
      result->profile=path;result->receiverToken=stop;
      const auto& description=result->description();
      // The real result reader supplies URL/trust values, never configurable
      // metainfo resource limits. This private binary mode has one fixed cap.
      if(result->bodyKind==BodyKind::TorrentMetainfo&&(description.responseOctets!=8*1024*1024
          ||description.headerBytes!=16384||description.maximumGets!=6
          ||description.phaseMillis!=30000||description.totalMillis!=60000)){return std::unexpected(Error::Limit);}
      if(description.responseOctets>8*1024*1024||description.headerBytes>16384||description.maximumGets>6||!description.maximumGets
          ||description.phaseMillis<=0||description.phaseMillis>30000||description.totalMillis<=0||description.totalMillis>60000){return std::unexpected(Error::Limit);}
      Configuration config;config.initial=QUrl::fromEncoded(QByteArray::fromStdString(description.literalGetUrl),QUrl::StrictMode);
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
      if(fixture){if(!fixture->endpoint.isLoopback()||!fixture->resolve||!fixture->cancelResolve){return std::unexpected(Error::UnsupportedRoute);}config.fixture=std::move(fixture);}
#else
      (void)fixture;
#endif
      if(!canonicalUrl(config.initial)){return std::unexpected(Error::UnsupportedRoute);}
      if(const auto* query=std::get_if<SavedMetasearchSourceSnapshot>(&*result->source)){
        auto target=metasearchInitialTarget(*query);if(!target){return std::unexpected(target.error());}config.initialMetasearchTarget=std::move(*target);
      }
      std::optional<std::string> referer;const bool canonicalReferer=std::holds_alternative<SavedSubscriptionResultSnapshot>(*result->source);
      if(const auto* saved=std::get_if<SavedSubscriptionResultSnapshot>(&*result->source)){referer=saved->description().referer;}
      else if(const auto* saved=std::get_if<SavedMetasearchResultHandle>(&*result->source)){referer=saved->description().referer;}
      else if(std::holds_alternative<SavedMetasearchSourceSnapshot>(*result->source)&&!description.context.rootPage.isEmpty()){referer=description.context.rootPage.toStdString();}
      if(referer&&!referer->empty()){
        config.referer=QByteArray::fromStdString(*referer);
        if(config.referer.size()>2048||!std::ranges::all_of(config.referer,[](unsigned char c){return c>=33&&c<=126;})
            ||(canonicalReferer&&!canonicalUrl(QUrl::fromEncoded(config.referer,QUrl::StrictMode)))){return std::unexpected(Error::UnsupportedRoute);}
      }
      auto settings=storage::ConfigFile::load(caller,"biglybt.config",{1024*1024,1024*1024,65536,64});
      const core::Document* document=settings?&settings->document():nullptr;
      if(!settings&&settings.error().code!=storage::ErrorCode::Missing){return std::unexpected(Error::Source);}
      const auto policy=engine::readNetworkPolicy(document,{.mode=engine::NetworkMode::Normal});
      if(!policy||policy->proxy.kind!=engine::NetworkProxyKind::None){return std::unexpected(Error::UnsupportedRoute);}
      auto binding=engine::prepareNetworkBinding(*policy);if(!binding){return std::unexpected(Error::UnsupportedRoute);}result->binding=std::move(*binding);
      for(const auto& text:result->binding.effective.bindAddresses){
        QHostAddress local(QString::fromStdString(text));if(local.isNull()||!local.scopeId().isEmpty()){return std::unexpected(Error::UnsupportedRoute);}
        const auto prior=std::ranges::find_if(config.locals,[&](const auto& old){return old.protocol()==local.protocol();});
        if(prior!=config.locals.end()){if(*prior!=local){return std::unexpected(Error::UnsupportedRoute);}}
        else{config.locals.push_back(local);}
      }
      if(config.locals.empty()||config.locals.size()>64){return std::unexpected(Error::UnsupportedRoute);}
      const auto checked=result->sourceCurrent(caller,session);if(!checked){return std::unexpected(Error::Source);}
      auto nonce=nextNonce.load();while(nonce!=std::numeric_limits<std::uint64_t>::max()&&!nextNonce.compare_exchange_weak(nonce,nonce+1)){}
      if(!nonce||nonce==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error::Limit);}result->nonce=nonce;
      config.nonce=nonce;config.epoch=result->lease;config.ipv6=result->binding.effective.ipv6;config.description=description;config.bodyKind=result->bodyKind;
      config.tls=QSslConfiguration::defaultConfiguration();
      if(description.trustAnchors){config.tls.setCaCertificates(*description.trustAnchors);}
      config.tls.setProtocol(QSsl::TlsV1_2OrLater);
      {std::lock_guard lock(result->box->mutex);result->box->view.nonce=nonce;result->box->finalUrl=config.initial;}
      result->receiverStop=std::make_unique<StopCallback>(stop,[weak=std::weak_ptr<Mailbox>(result->box)]{if(const auto box=weak.lock()){box->cancel();}});
      result->network=std::make_unique<NetworkThread>(result->box,std::move(config));return result;
    }catch(...){if(!result){activeSlot=false;}return std::unexpected(Error::Memory);}
  }
  struct RawCompletion {std::uint64_t session=0,nonce=0;QUrl initialUrl,finalUrl;RssContext context;QByteArray bytes;std::size_t gets=0,responseOctets=0;};
  std::expected<std::optional<RawCompletion>,Error> pollMetasearch(
      const storage::ProfileLease&,std::uint64_t,BodyKind);
  std::expected<std::uint64_t,Error> validate(const storage::ProfileLease& caller,std::uint64_t currentSession){
    if(std::this_thread::get_id()!=worker){return std::unexpected(Error::WrongWorker);}
    if(!source||!lease||currentSession!=session||!caller.sharesOwnership(*lease)){return std::unexpected(Error::Source);}
    auto current=sourceCurrent(caller,currentSession);
    {std::lock_guard lock(box->mutex);++box->view.sourceChecks;}
    if(!current||!engine::networkBindingCurrent(binding)){return std::unexpected(Error::Source);}
    return *current;
  }
};
std::expected<std::optional<RssHttpFetch::Impl::RawCompletion>,Error> RssHttpFetch::Impl::pollMetasearch(
    const storage::ProfileLease& caller,std::uint64_t currentSession,BodyKind expectedBody){
  try{
    const auto reject=[&](Error error){const auto first=stop(error);join();return std::unexpected(first);};
    if(std::this_thread::get_id()!=worker){return std::unexpected(Error::WrongWorker);}
    if(closed){std::lock_guard lock(box->mutex);return std::unexpected(box->view.failure.value_or(Error::Closed));}
    const bool query=std::holds_alternative<SavedMetasearchSourceSnapshot>(*source),result=std::holds_alternative<SavedMetasearchResultHandle>(*source);
    if(bodyKind!=expectedBody||(!query&&!result)||(query!=(expectedBody==BodyKind::MetasearchDocument))){return reject(Error::Source);}
    if(delivered){return std::optional<RawCompletion>{};}
    if(box->cancelled){return reject(box->cancelReason.load());}
    if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()>=description().totalMillis){return reject(Error::Deadline);}
    RssFetchObservation view;{std::lock_guard lock(box->mutex);view=box->view;}
    if(view.failure){join();return std::unexpected(*view.failure);}
    const auto observed=validate(caller,currentSession);if(!observed){return reject(observed.error());}
    if(box->cancelled){return reject(box->cancelReason.load());}
    if(pending(view.phase)){
      {std::lock_guard lock(box->mutex);if(box->approvalInFlight){return std::optional<RawCompletion>{};}box->approvalInFlight=true;}
      if(!network||!network->approve(view.phase,nonce,view.hop,*observed)){return reject(Error::Closed);}
      return std::optional<RawCompletion>{};
    }
    if(view.phase!=Phase::BeforeDelivery){return std::optional<RawCompletion>{};}
    join();const auto before=validate(caller,currentSession);if(!before){return reject(before.error());}
    QByteArray body;QUrl finalUrl;std::chrono::steady_clock::time_point deliveryStarted;
    {std::lock_guard lock(box->mutex);body=std::move(box->body);finalUrl=box->finalUrl;deliveryStarted=box->phaseStarted;}
    const auto expired=[&]{const auto now=std::chrono::steady_clock::now();return std::chrono::duration_cast<std::chrono::milliseconds>(now-deliveryStarted).count()>=description().phaseMillis||std::chrono::duration_cast<std::chrono::milliseconds>(now-started).count()>=description().totalMillis;};
    if(expired()){return reject(Error::Deadline);}if(body.isEmpty()){return reject(Error::Parse);}if(body.size()>4*1024*1024){return reject(Error::Limit);}
    if(query){
      QStringDecoder decoder(QStringDecoder::Utf8);const QString text=decoder.decode(body);
      if(decoder.hasError()||text.contains(QChar(0))){return reject(Error::Encoding);}
    }
    RawCompletion completed;completed.session=session;completed.nonce=nonce;completed.initialUrl=QUrl::fromEncoded(QByteArray::fromStdString(description().literalGetUrl),QUrl::StrictMode);completed.finalUrl=std::move(finalUrl);completed.context=description().context;completed.bytes=std::move(body);completed.gets=view.gets;completed.responseOctets=view.responseOctets;
    const auto after=validate(caller,currentSession);if(box->cancelled){return reject(box->cancelReason.load());}if(!after){return reject(after.error());}if(expired()){return reject(Error::Deadline);}
    std::optional<Error> lateFailure;{std::lock_guard lock(box->mutex);if(box->cancelled||box->view.failure){lateFailure=box->view.failure.value_or(box->cancelReason.load());}else if(!lease||lease->sourceRevision()!=*after){lateFailure=Error::Source;}else{delivered=true;box->view.phase=Phase::Complete;}}
    if(lateFailure){return reject(*lateFailure);}return std::optional<RawCompletion>{std::move(completed)};
  }catch(const std::bad_alloc&){const auto first=stop(Error::Memory);join();return std::unexpected(first);}
}

RssHttpFetch::RssHttpFetch(std::unique_ptr<Impl> value):impl_(std::move(value)){}
RssHttpFetch::~RssHttpFetch(){close();}
std::expected<std::unique_ptr<RssHttpFetch>,RssFetchError> RssHttpFetch::openOneShot(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,SavedRssSelector selected,std::stop_token stop){
  auto value=Impl::make(lease,path,session,std::move(selected),stop);if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<RssHttpFetch>(new RssHttpFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<std::unique_ptr<RssHttpFetch>,RssFetchError> RssHttpFetch::openSubscriptionOneShot(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,SavedSubscriptionSelector selected,std::stop_token stop){
  auto value=Impl::make(lease,path,session,std::move(selected),stop);if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<RssHttpFetch>(new RssHttpFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<std::unique_ptr<RssHttpFetch>,RssFetchError> RssHttpFetch::openAutomaticSubscriptionOneShot(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,SavedSubscriptionSelector selected,std::stop_token stop){
  auto value=Impl::make(lease,path,session,Impl::AutomaticSubscription{std::move(selected)},stop);if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<RssHttpFetch>(new RssHttpFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
std::expected<std::unique_ptr<RssHttpFetch>,RssFetchError> RssHttpFetch::openFixture(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,SavedRssSelector selected,PrivateEndpointFixture fixture,std::stop_token stop){
  auto value=Impl::make(lease,path,session,std::move(selected),stop,std::move(fixture));if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<RssHttpFetch>(new RssHttpFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<std::unique_ptr<RssHttpFetch>,RssFetchError> RssHttpFetch::openSubscriptionFixture(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,SavedSubscriptionSelector selected,PrivateEndpointFixture fixture,std::stop_token stop){
  auto value=Impl::make(lease,path,session,std::move(selected),stop,std::move(fixture));if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<RssHttpFetch>(new RssHttpFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<std::unique_ptr<RssHttpFetch>,RssFetchError> RssHttpFetch::openAutomaticSubscriptionFixture(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,SavedSubscriptionSelector selected,PrivateEndpointFixture fixture,std::stop_token stop){
  auto value=Impl::make(lease,path,session,Impl::AutomaticSubscription{std::move(selected)},stop,std::move(fixture));if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<RssHttpFetch>(new RssHttpFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
#endif
bool RssHttpFetch::matchesSavedRssSource(const storage::ProfileLease& caller,
    const std::filesystem::path& profile,std::uint64_t session,const SavedRssSelector& selected,std::stop_token receiver)const noexcept{
  try{
    if(!impl_||impl_->closed||std::this_thread::get_id()!=impl_->worker||!impl_->lease||!impl_->source
        ||!caller.sharesOwnership(*impl_->lease)||impl_->profile!=profile||impl_->session!=session||impl_->receiverToken!=receiver){return false;}
    const auto* saved=std::get_if<SavedRssSourceSnapshot>(&*impl_->source);if(!saved){return false;}
    const auto& actual=saved->description().selected;
    return actual.sourceIndex==selected.sourceIndex&&actual.id==selected.id&&actual.uid==selected.uid;
  }catch(...){return false;}
}
bool RssHttpFetch::matchesSubscriptionSource(const storage::ProfileLease& caller,
    const std::filesystem::path& profile,std::uint64_t session,const SavedSubscriptionSelector& selected,std::stop_token receiver)const noexcept{
  try{
    if(!impl_||impl_->closed||std::this_thread::get_id()!=impl_->worker||!impl_->lease||!impl_->source
        ||impl_->automatic()||!caller.sharesOwnership(*impl_->lease)||impl_->profile!=profile||impl_->session!=session||impl_->receiverToken!=receiver){return false;}
    const auto* saved=std::get_if<SavedSubscriptionSourceSnapshot>(&*impl_->source);
    return saved&&saved->description().selected==selected;
  }catch(...){return false;}
}
bool RssHttpFetch::matchesAutomaticSubscriptionSource(const storage::ProfileLease& caller,
    const std::filesystem::path& profile,std::uint64_t session,const SavedSubscriptionSelector& selected,std::stop_token receiver)const noexcept{
  try{
    if(!impl_||impl_->closed||std::this_thread::get_id()!=impl_->worker||!impl_->lease||!impl_->source
        ||!impl_->automatic()||!caller.sharesOwnership(*impl_->lease)||impl_->profile!=profile||impl_->session!=session||impl_->receiverToken!=receiver){return false;}
    const auto* saved=std::get_if<SavedSubscriptionSourceSnapshot>(&*impl_->source);
    return saved&&saved->description().selected==selected;
  }catch(...){return false;}
}
RssFetchObservation RssHttpFetch::observation()const{std::lock_guard lock(impl_->box->mutex);return impl_->box->view;}
void RssHttpFetch::requestCancel()noexcept{impl_->box->cancel();}
void RssHttpFetch::close()noexcept{if(impl_){impl_->close();}}
std::expected<std::optional<RssFetchCompletion>,RssFetchError> RssHttpFetch::poll(
    const storage::ProfileLease& caller,std::uint64_t session){
  auto& value=*impl_;
  try{
  const auto reject=[&](Error error){const auto first=value.stop(error);value.join();return std::unexpected(first);};
  if(std::this_thread::get_id()!=value.worker){return std::unexpected(Error::WrongWorker);}
  if(value.closed){std::lock_guard lock(value.box->mutex);return std::unexpected(value.box->view.failure.value_or(Error::Closed));}
  if(value.delivered){return std::optional<RssFetchCompletion>{};}
  if(value.box->cancelled){return reject(value.box->cancelReason.load());}
  if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-value.started).count()>=value.description().totalMillis){
    return reject(Error::Deadline);
  }
  const auto view=observation();
  if(view.failure){value.join();return std::unexpected(*view.failure);}
  // Active DNS/connect/body phases also observe exact source revocation on
  // every owner poll; a held peer cannot defer it until response completion.
  const auto observed=value.validate(caller,session);
  if(!observed){return reject(observed.error());}
  if(value.box->cancelled){return reject(value.box->cancelReason.load());}
  if(pending(view.phase)){
    {std::lock_guard lock(value.box->mutex);if(value.box->approvalInFlight){return std::optional<RssFetchCompletion>{};}}
    {std::lock_guard lock(value.box->mutex);value.box->approvalInFlight=true;}
    if(!value.network||!value.network->approve(view.phase,value.nonce,view.hop,*observed)){
      return reject(Error::Closed);
    }
    return std::optional<RssFetchCompletion>{};
  }
  if(view.phase!=Phase::BeforeDelivery){return std::optional<RssFetchCompletion>{};}
  // No source gate held while joining callbacks or synchronously parsing on
  // this existing profile worker. Source/current and receiver fences bracket it.
  value.join();const auto before=value.validate(caller,session);
  if(!before){return reject(before.error());}
  QByteArray body;QUrl finalUrl;std::chrono::steady_clock::time_point deliveryStarted;
  {std::lock_guard lock(value.box->mutex);body=std::move(value.box->body);finalUrl=value.box->finalUrl;deliveryStarted=value.box->phaseStarted;}
  const auto expired=[&]{
    const auto now=std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now-deliveryStarted).count()>=value.description().phaseMillis
        ||std::chrono::duration_cast<std::chrono::milliseconds>(now-value.started).count()>=value.description().totalMillis;
  };
  if(expired()){return reject(Error::Deadline);}
  QStringDecoder decoder(QStringDecoder::Utf8);const QString text=decoder.decode(body);
  // XML1.0 forbids NUL. Reject it before any raw-byte XML parser can autodetect
  // BOM-less UTF16 from otherwise UTF8-valid ASCII/NUL bytes.
  if(decoder.hasError()||text.contains(QChar(0))){return reject(Error::Encoding);}
  const QRegularExpression declaration(QStringLiteral("^(?:\\x{FEFF})?\\s*<\\?xml[^?]*encoding\\s*=\\s*['\"]([^'\"]+)['\"]"),QRegularExpression::CaseInsensitiveOption);
  const auto declared=declaration.match(text);
  if(declared.hasMatch()&&declared.captured(1).compare(QStringLiteral("UTF-8"),Qt::CaseInsensitive)!=0&&declared.captured(1).compare(QStringLiteral("UTF8"),Qt::CaseInsensitive)!=0){return reject(Error::Encoding);}
  QXmlStreamReader root(body);while(!root.atEnd()&&!root.isStartElement()){root.readNext();if(root.tokenType()==QXmlStreamReader::DTD){return reject(Error::Parse);}}
  if(!root.isStartElement()||(root.name().compare(u"rss",Qt::CaseInsensitive)!=0&&root.name().compare(u"feed",Qt::CaseInsensitive)!=0)){return reject(Error::Parse);}
  auto context=value.description().context;
  const QRegularExpression base(QStringLiteral("<base.*?href=\"([^\"]+)\".*?>"),QRegularExpression::CaseInsensitiveOption);
  const auto matched=base.match(text);if(matched.hasMatch()){context.basePage=matched.captured(1);}
  if(value.box->cancelled){return reject(value.box->cancelReason.load());}
  auto document=parseRssDocument(std::move(body),context,value.description().documentLimits);
  const auto after=value.validate(caller,session);
  if(value.box->cancelled){return reject(value.box->cancelReason.load());}
  if(!after){return reject(after.error());}
  if(expired()){return reject(Error::Deadline);}
  if(!document){const auto error=document.error()==RssError::Memory?Error::Memory:document.error()==RssError::Limit?Error::Limit:Error::Parse;
    return reject(error);}
  RssFetchCompletion result;result.session=value.session;result.nonce=value.nonce;
  result.initialUrl=QUrl::fromEncoded(QByteArray::fromStdString(value.description().literalGetUrl),QUrl::StrictMode);
  result.finalUrl=std::move(finalUrl);result.context=std::move(context);result.document=std::move(*document);
  result.gets=view.gets;result.responseOctets=view.responseOctets;
  std::optional<Error> lateFailure;
  {std::lock_guard lock(value.box->mutex);
    if(value.box->cancelled||value.box->view.failure){lateFailure=value.box->view.failure.value_or(value.box->cancelReason.load());}
    else{value.delivered=true;value.box->view.phase=Phase::Complete;}
  }
  // Cancellation and successful delivery share the same first-terminal latch.
  // Reject outside its mutex; stop/join must never recursively acquire it.
  if(lateFailure){return reject(*lateFailure);}
  return std::optional<RssFetchCompletion>{std::move(result)};
  }catch(const std::bad_alloc&){const auto first=value.stop(Error::Memory);value.join();return std::unexpected(first);}
}

SubscriptionMetainfoFetch::SubscriptionMetainfoFetch(std::unique_ptr<RssHttpFetch::Impl> value):impl_(std::move(value)){}
SubscriptionMetainfoFetch::~SubscriptionMetainfoFetch(){close();}
std::expected<std::unique_ptr<SubscriptionMetainfoFetch>,RssFetchError> SubscriptionMetainfoFetch::openOneShot(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,
    SavedSubscriptionSelector selected,std::string resultKey,std::stop_token stop){
  auto value=RssHttpFetch::Impl::make(lease,path,session,SavedSubscriptionResultSelector{std::move(selected),std::move(resultKey)},stop);
  if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<SubscriptionMetainfoFetch>(new SubscriptionMetainfoFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<std::unique_ptr<SubscriptionMetainfoFetch>,RssFetchError> SubscriptionMetainfoFetch::openAutomaticOneShot(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,
    SavedSubscriptionSelector selected,std::string resultKey,std::stop_token stop){
  auto value=RssHttpFetch::Impl::make(lease,path,session,RssHttpFetch::Impl::AutomaticResult{SavedSubscriptionResultSelector{std::move(selected),std::move(resultKey)}},stop);
  if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<SubscriptionMetainfoFetch>(new SubscriptionMetainfoFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
std::expected<std::unique_ptr<SubscriptionMetainfoFetch>,RssFetchError> SubscriptionMetainfoFetch::openFixture(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,
    SavedSubscriptionSelector selected,std::string resultKey,RssHttpFetch::PrivateEndpointFixture fixture,std::stop_token stop){
  auto value=RssHttpFetch::Impl::make(lease,path,session,SavedSubscriptionResultSelector{std::move(selected),std::move(resultKey)},stop,std::move(fixture));
  if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<SubscriptionMetainfoFetch>(new SubscriptionMetainfoFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
std::expected<std::unique_ptr<SubscriptionMetainfoFetch>,RssFetchError> SubscriptionMetainfoFetch::openAutomaticFixture(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,
    SavedSubscriptionSelector selected,std::string resultKey,RssHttpFetch::PrivateEndpointFixture fixture,std::stop_token stop){
  auto value=RssHttpFetch::Impl::make(lease,path,session,RssHttpFetch::Impl::AutomaticResult{SavedSubscriptionResultSelector{std::move(selected),std::move(resultKey)}},stop,std::move(fixture));
  if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<SubscriptionMetainfoFetch>(new SubscriptionMetainfoFetch(std::move(*value)));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
#endif
bool SubscriptionMetainfoFetch::matchesSubscriptionResultSource(const storage::ProfileLease& caller,
    const std::filesystem::path& profile,std::uint64_t session,const SavedSubscriptionSelector& selected,
    std::string_view resultKey,std::stop_token receiver)const noexcept{
  try{
    if(!impl_||impl_->closed||std::this_thread::get_id()!=impl_->worker||!impl_->lease||!impl_->source
        ||impl_->automatic()||impl_->bodyKind!=BodyKind::TorrentMetainfo||!caller.sharesOwnership(*impl_->lease)
        ||impl_->profile!=profile||impl_->session!=session||impl_->receiverToken!=receiver){return false;}
    const auto* saved=std::get_if<SavedSubscriptionResultSnapshot>(&*impl_->source);if(!saved){return false;}
    const auto& actual=saved->description().selected;
    return actual.subscription==selected&&std::string_view(actual.resultKey)==resultKey;
  }catch(...){return false;}
}
bool SubscriptionMetainfoFetch::matchesAutomaticSubscriptionResultSource(const storage::ProfileLease& caller,
    const std::filesystem::path& profile,std::uint64_t session,const SavedSubscriptionSelector& selected,
    std::string_view resultKey,std::stop_token receiver)const noexcept{
  try{
    if(!impl_||impl_->closed||std::this_thread::get_id()!=impl_->worker||!impl_->lease||!impl_->source
        ||!impl_->automatic()||impl_->bodyKind!=BodyKind::TorrentMetainfo||!caller.sharesOwnership(*impl_->lease)
        ||impl_->profile!=profile||impl_->session!=session||impl_->receiverToken!=receiver){return false;}
    const auto* saved=std::get_if<SavedSubscriptionResultSnapshot>(&*impl_->source);if(!saved){return false;}
    const auto& actual=saved->description().selected;
    return actual.subscription==selected&&std::string_view(actual.resultKey)==resultKey;
  }catch(...){return false;}
}
RssFetchObservation SubscriptionMetainfoFetch::observation()const{std::lock_guard lock(impl_->box->mutex);return impl_->box->view;}
void SubscriptionMetainfoFetch::requestCancel()noexcept{impl_->box->cancel();}
void SubscriptionMetainfoFetch::close()noexcept{if(impl_){impl_->close();}}
std::expected<std::optional<SubscriptionMetainfoCompletion>,RssFetchError> SubscriptionMetainfoFetch::poll(
    const storage::ProfileLease& caller,std::uint64_t session){
  auto& value=*impl_;
  try{
  const auto reject=[&](Error error){const auto first=value.stop(error);value.join();return std::unexpected(first);};
  if(std::this_thread::get_id()!=value.worker){return std::unexpected(Error::WrongWorker);}
  if(value.closed){std::lock_guard lock(value.box->mutex);return std::unexpected(value.box->view.failure.value_or(Error::Closed));}
  if(value.bodyKind!=BodyKind::TorrentMetainfo){return reject(Error::Source);}
  if(value.delivered){return std::optional<SubscriptionMetainfoCompletion>{};}
  if(value.box->cancelled){return reject(value.box->cancelReason.load());}
  if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-value.started).count()>=value.description().totalMillis){return reject(Error::Deadline);}
  const auto view=observation();
  if(view.failure){value.join();return std::unexpected(*view.failure);}
  const auto observed=value.validate(caller,session);
  if(!observed){return reject(observed.error());}
  if(value.box->cancelled){return reject(value.box->cancelReason.load());}
  if(pending(view.phase)){
    {std::lock_guard lock(value.box->mutex);if(value.box->approvalInFlight){return std::optional<SubscriptionMetainfoCompletion>{};}}
    {std::lock_guard lock(value.box->mutex);value.box->approvalInFlight=true;}
    if(!value.network||!value.network->approve(view.phase,value.nonce,view.hop,*observed)){return reject(Error::Closed);}
    return std::optional<SubscriptionMetainfoCompletion>{};
  }
  if(view.phase!=Phase::BeforeDelivery){return std::optional<SubscriptionMetainfoCompletion>{};}
  value.join();const auto before=value.validate(caller,session);
  if(!before){return reject(before.error());}
  QByteArray body;QUrl finalUrl;std::chrono::steady_clock::time_point deliveryStarted;
  {std::lock_guard lock(value.box->mutex);body=std::move(value.box->body);finalUrl=value.box->finalUrl;deliveryStarted=value.box->phaseStarted;}
  const auto expired=[&]{const auto now=std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now-deliveryStarted).count()>=value.description().phaseMillis
        ||std::chrono::duration_cast<std::chrono::milliseconds>(now-value.started).count()>=value.description().totalMillis;};
  if(expired()){return reject(Error::Deadline);}
  if(body.isEmpty()){return reject(Error::Parse);}
  if(body.size()>4*1024*1024){return reject(Error::Limit);}
  // Binary bytes are never decoded as text/XML, written to a profile, treated
  // as an inspected torrent or permitted to mutate read/scan/intake state here.
  SubscriptionMetainfoCompletion result;result.session=value.session;result.nonce=value.nonce;
  result.initialUrl=QUrl::fromEncoded(QByteArray::fromStdString(value.description().literalGetUrl),QUrl::StrictMode);
  result.finalUrl=std::move(finalUrl);result.bytes=std::move(body);result.gets=view.gets;result.responseOctets=view.responseOctets;
  const auto after=value.validate(caller,session);
  if(value.box->cancelled){return reject(value.box->cancelReason.load());}
  if(!after){return reject(after.error());}
  if(expired()){return reject(Error::Deadline);}
  std::optional<Error> lateFailure;
  {std::lock_guard lock(value.box->mutex);
    if(value.box->cancelled||value.box->view.failure){lateFailure=value.box->view.failure.value_or(value.box->cancelReason.load());}
    else if(!value.lease||value.lease->sourceRevision()!=*after){lateFailure=Error::Source;}
    else{value.delivered=true;value.box->view.phase=Phase::Complete;}
  }
  if(lateFailure){return reject(*lateFailure);}
  return std::optional<SubscriptionMetainfoCompletion>{std::move(result)};
  }catch(const std::bad_alloc&){const auto first=value.stop(Error::Memory);value.join();return std::unexpected(first);}
}

MetasearchHttpFetch::MetasearchHttpFetch(std::unique_ptr<RssHttpFetch::Impl> value):impl_(std::move(value)){}
MetasearchHttpFetch::~MetasearchHttpFetch(){close();}
std::expected<std::unique_ptr<MetasearchHttpFetch>,RssFetchError> MetasearchHttpFetch::openOneShot(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,
    SavedMetasearchSelector selected,std::string query,std::stop_token stop){
  auto value=RssHttpFetch::Impl::make(lease,path,session,RssHttpFetch::Impl::MetasearchQuery{std::move(selected),std::move(query)},stop);if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<MetasearchHttpFetch>(new MetasearchHttpFetch(std::move(*value)));}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
std::expected<std::unique_ptr<MetasearchHttpFetch>,RssFetchError> MetasearchHttpFetch::openFixture(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,
    SavedMetasearchSelector selected,std::string query,RssHttpFetch::PrivateEndpointFixture fixture,std::stop_token stop){
  auto value=RssHttpFetch::Impl::make(lease,path,session,RssHttpFetch::Impl::MetasearchQuery{std::move(selected),std::move(query)},stop,std::move(fixture));if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<MetasearchHttpFetch>(new MetasearchHttpFetch(std::move(*value)));}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
#endif
bool MetasearchHttpFetch::matchesQuery(const storage::ProfileLease& caller,const std::filesystem::path& path,
    std::uint64_t session,const SavedMetasearchSelector& selected,std::string_view query,std::stop_token stop)const noexcept{
  try{if(!impl_||impl_->closed||std::this_thread::get_id()!=impl_->worker||!impl_->source||!impl_->lease||!caller.sharesOwnership(*impl_->lease)||impl_->profile!=path||impl_->session!=session||impl_->receiverToken!=stop||impl_->bodyKind!=BodyKind::MetasearchDocument){return false;}const auto* source=std::get_if<SavedMetasearchSourceSnapshot>(&*impl_->source);return source&&source->description().selected==selected&&std::string_view(source->description().query)==query;}catch(...){return false;}
}
RssFetchObservation MetasearchHttpFetch::observation()const{std::lock_guard lock(impl_->box->mutex);return impl_->box->view;}
void MetasearchHttpFetch::requestCancel()noexcept{impl_->box->cancel();}
void MetasearchHttpFetch::close()noexcept{if(impl_){impl_->close();}}
std::expected<std::optional<MetasearchFetchCompletion>,RssFetchError> MetasearchHttpFetch::poll(const storage::ProfileLease& caller,std::uint64_t session){
  auto value=impl_->pollMetasearch(caller,session,BodyKind::MetasearchDocument);if(!value){return std::unexpected(value.error());}if(!*value){return std::optional<MetasearchFetchCompletion>{};}auto& raw=**value;MetasearchFetchCompletion result;result.session=raw.session;result.nonce=raw.nonce;result.initialUrl=std::move(raw.initialUrl);result.finalUrl=std::move(raw.finalUrl);result.context=std::move(raw.context);result.bytes=std::move(raw.bytes);result.gets=raw.gets;result.responseOctets=raw.responseOctets;return std::optional<MetasearchFetchCompletion>{std::move(result)};
}

MetasearchMetainfoFetch::MetasearchMetainfoFetch(std::unique_ptr<RssHttpFetch::Impl> value):impl_(std::move(value)){}
MetasearchMetainfoFetch::~MetasearchMetainfoFetch(){close();}
std::expected<std::unique_ptr<MetasearchMetainfoFetch>,RssFetchError> MetasearchMetainfoFetch::openOneShot(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,
    const SavedMetasearchResultHandle& selected,std::stop_token stop){
  auto value=RssHttpFetch::Impl::make(lease,path,session,RssHttpFetch::Impl::MetasearchResult{&selected},stop);if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<MetasearchMetainfoFetch>(new MetasearchMetainfoFetch(std::move(*value)));}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
std::expected<std::unique_ptr<MetasearchMetainfoFetch>,RssFetchError> MetasearchMetainfoFetch::openFixture(
    const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,
    const SavedMetasearchResultHandle& selected,RssHttpFetch::PrivateEndpointFixture fixture,std::stop_token stop){
  auto value=RssHttpFetch::Impl::make(lease,path,session,RssHttpFetch::Impl::MetasearchResult{&selected},stop,std::move(fixture));if(!value){return std::unexpected(value.error());}
  try{return std::unique_ptr<MetasearchMetainfoFetch>(new MetasearchMetainfoFetch(std::move(*value)));}catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
#endif
bool MetasearchMetainfoFetch::matchesHandle(const storage::ProfileLease& caller,const std::filesystem::path& path,
    std::uint64_t session,const SavedMetasearchResultHandle& selected,std::stop_token stop)const noexcept{
  try{if(!impl_||impl_->closed||std::this_thread::get_id()!=impl_->worker||!impl_->source||!impl_->lease||!caller.sharesOwnership(*impl_->lease)||impl_->profile!=path||impl_->session!=session||impl_->receiverToken!=stop||impl_->bodyKind!=BodyKind::TorrentMetainfo){return false;}const auto* source=std::get_if<SavedMetasearchResultHandle>(&*impl_->source);return source&&source->sameIdentity(selected);}catch(...){return false;}
}
RssFetchObservation MetasearchMetainfoFetch::observation()const{std::lock_guard lock(impl_->box->mutex);return impl_->box->view;}
void MetasearchMetainfoFetch::requestCancel()noexcept{impl_->box->cancel();}
void MetasearchMetainfoFetch::close()noexcept{if(impl_){impl_->close();}}
std::expected<std::optional<MetasearchMetainfoCompletion>,RssFetchError> MetasearchMetainfoFetch::poll(const storage::ProfileLease& caller,std::uint64_t session){
  auto value=impl_->pollMetasearch(caller,session,BodyKind::TorrentMetainfo);if(!value){return std::unexpected(value.error());}if(!*value){return std::optional<MetasearchMetainfoCompletion>{};}auto& raw=**value;MetasearchMetainfoCompletion result;result.session=raw.session;result.nonce=raw.nonce;result.initialUrl=std::move(raw.initialUrl);result.finalUrl=std::move(raw.finalUrl);result.bytes=std::move(raw.bytes);result.gets=raw.gets;result.responseOctets=raw.responseOctets;return std::optional<MetasearchMetainfoCompletion>{std::move(result)};
}
} // namespace biglybtc::search
