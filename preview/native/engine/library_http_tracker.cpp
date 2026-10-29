#include "library_http_tracker.hpp"
#include <QEventLoop>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QNetworkProxy>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <bit>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace biglybtc::engine::detail {
namespace {
enum class Proposal {None,Address,Redirect,Authentication};
struct Mailbox {
  std::mutex mutex;
  std::atomic<bool> cancelled=false;
  QList<QHostAddress> resolved,proxyResolved;
  std::vector<std::string> resolverSubmissions;
  LibraryHttpRoute proposedRoute;
  std::uint64_t generation=0;std::int64_t observedNanos=0,observedWallMillis=0;
  Proposal kind=Proposal::None;
  QByteArray body,realm;
  bool proposal=false,approved=false,done=false,unknown=false,hasBody=false,trackerHostNotFound=false;
};
// All QObject/socket operations run only on this reusable private event loop.
class Operation final:public QObject {
public:
  std::shared_ptr<Mailbox> box;
  LibraryHttpRoute route;
  QByteArray query;
  QSslConfiguration tls;
  QTcpSocket* socket=nullptr;
  QTimer deadline;QElapsedTimer age;
  int lookup=-1;LibraryHttpResolver resolver;bool trackerHostNotFound=false;
  std::uint64_t generation=1;
  bool finished=false,authorised=false,awaitingRedirect=false,awaitingAuthentication=false;
  explicit Operation(std::shared_ptr<Mailbox> value,LibraryHttpRoute r,QByteArray q,QSslConfiguration t,LibraryHttpResolver dns,QObject* parent)
      :QObject(parent),box(std::move(value)),route(std::move(r)),query(std::move(q)),tls(std::move(t)),resolver(std::move(dns)){
    visited.push_back(route.scrape.toEncoded());age.start();deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,this,[this]{finish(!authorised);});resolve();
  }
  ~Operation()override{cancelLookup();if(socket){QObject::disconnect(socket,nullptr,this,nullptr);socket->abort();}}
  void resumeRedirect(std::uint64_t expected){
    if(!current(expected)||!awaitingRedirect){return;}awaitingRedirect=false;resolve();
  }
  void declineAuthentication(std::uint64_t expected,bool unknown){
    if(current(expected)&&awaitingAuthentication){finish(unknown);}
  }
  void resumeAuthentication(std::uint64_t expected,QByteArray value){
    if(!current(expected)||!awaitingAuthentication){return;}awaitingAuthentication=false;authorization=std::move(value);resolve();
  }
  void approve(std::uint64_t expected,const QHostAddress& remote,const QHostAddress& local,const QHostAddress& destination={}){
    if(!current(expected)||awaitingRedirect||awaitingAuthentication||socket){return;}
    if(!arm()){return;}authorised=true;const auto hop=generation;
    const bool encrypted=route.scrape.scheme()=="https";socksTarget=destination;
    if(encrypted){auto* ssl=new QSslSocket(this);ssl->setSslConfiguration(tls);ssl->setPeerVerifyMode(QSslSocket::VerifyPeer);
      ssl->setPeerVerifyName(route.scrape.host());socket=ssl;
      QObject::connect(ssl,&QSslSocket::encrypted,this,[this,hop]{if(current(hop)){guarded([this]{send();});}});
      QObject::connect(ssl,&QSslSocket::sslErrors,this,[this,hop](const QList<QSslError>&){if(current(hop)){finish(false);}});
    }else{socket=new QTcpSocket(this);}
    // The actual TCP socket is ALWAYS directly bound to the approved numeric
    // proxy/target endpoint. Qt application/system proxy factories never run.
    socket->setProxy(QNetworkProxy::NoProxy);socket->setReadBufferSize(150*1024);
    QObject::connect(socket,&QTcpSocket::connected,this,[this,hop,encrypted]{if(current(hop)){guarded([&]{if(route.socks5){beginSocks();}else if(!encrypted){send();}});}});
    QObject::connect(socket,&QTcpSocket::readyRead,this,[this,hop]{if(current(hop)){guarded([this]{read(false);});}});
    QObject::connect(socket,&QTcpSocket::disconnected,this,[this,hop]{if(current(hop)){guarded([this]{read(true);});}});
    QObject::connect(socket,&QTcpSocket::errorOccurred,this,[this,hop](QAbstractSocket::SocketError e){
      if(current(hop)){guarded([&]{if(e==QAbstractSocket::RemoteHostClosedError){read(true);}else{finish(false);}});}
    });
    if(!socket->bind(local,0)){finish(false);return;}
    const auto port=route.socks5?route.socks5->port:static_cast<quint16>(route.scrape.port(encrypted?443:80));
    if(encrypted&&!route.socks5){static_cast<QSslSocket*>(socket)->connectToHostEncrypted(remote.toString(),port,route.scrape.host());}
    else{socket->connectToHost(remote,port);}
  }
private:
  enum class SocksStage {None,Greeting,Authentication,Connect,Open};
  SocksStage socksStage=SocksStage::None;
  QByteArray socksBytes;QHostAddress socksTarget;QList<QHostAddress> proxyAddresses;
  QByteArray received,authorization;QList<QByteArray> visited;std::size_t transferred=0;unsigned authentications=0;
  bool current(std::uint64_t hop)const noexcept{return !finished&&!box->cancelled&&generation==hop;}
  template<class F>void guarded(F&& action)noexcept{try{action();}catch(...){finish(true);}}
  bool arm(){const auto left=60000-age.elapsed();if(left<=0){finish(!authorised);return false;}deadline.start(static_cast<int>(std::min<qint64>(30000,left)));return true;}
  void clearSocket(){socksStage=SocksStage::None;socksBytes.clear();if(socket){auto* old=std::exchange(socket,nullptr);QObject::disconnect(old,nullptr,this,nullptr);old->abort();old->deleteLater();}}
  void cancelLookup()noexcept{if(lookup>=0){const auto old=std::exchange(lookup,-1);try{resolver.cancel(old);}catch(...){}}}
  void submitLookup(const QString& host,bool proxy){
    bool room=false;{std::lock_guard lock(box->mutex);room=box->resolverSubmissions.size()<24;
      if(room){box->resolverSubmissions.push_back(host.toStdString());}}
    if(!room){finish(true);return;}
    const auto hop=generation;
    lookup=resolver.submit(host,this,[this,hop,proxy](const QHostInfo& info){
      if(!current(hop)){return;}guarded([&]{lookup=-1;if(info.error()!=QHostInfo::NoError){
          // InetSocketAddress preserves an unresolved name after HostNotFound;
          // SocksSocketImpl sends that name to the SAME explicitly saved proxy.
          // The proposal still needs fresh owner approval before any socket.
          if(!proxy&&route.socks5&&info.error()==QHostInfo::HostNotFound){trackerHostNotFound=true;propose({});return;}
          finish(route.socks5.has_value());return;}
        if(proxy){proxyAddresses=info.addresses();resolveTarget();}else{propose(info.addresses());}});
    });
    if(lookup<0){finish(true);}
  }
  void resolve(){
    if(!current(generation)||!arm()){return;}proxyAddresses.clear();trackerHostNotFound=false;QHostAddress numeric;
    if(route.socks5){
      if(numeric.setAddress(route.socks5->host)){proxyAddresses={numeric};resolveTarget();}
      else{submitLookup(route.socks5->host,true);}
    }else{resolveTarget();}
  }
  void resolveTarget(){
    if(!current(generation)){return;}QHostAddress numeric;
    if(numeric.setAddress(route.scrape.host())){propose({numeric});}
    else if(route.socks5&&route.socks5->remoteDns){propose({});} // Never submit this tracker name locally.
    else{submitLookup(route.scrape.host(),false);}
  }
  void propose(const QList<QHostAddress>& addresses){
    if(!current(generation)){return;}
    if((addresses.isEmpty()&&!(route.socks5&&(route.socks5->remoteDns||trackerHostNotFound)))||addresses.size()>32
        ||(route.socks5&&(proxyAddresses.isEmpty()||proxyAddresses.size()>32))){finish(true);return;}
    std::lock_guard lock(box->mutex);box->resolved=addresses;box->proxyResolved=proxyAddresses;box->trackerHostNotFound=trackerHostNotFound;box->proposedRoute=route;box->kind=Proposal::Address;
    box->generation=generation;box->proposal=true;box->approved=false;
  }
  void socksWrite(const QByteArray& bytes){if(!socket||socket->write(bytes)!=bytes.size()){finish(false);}}
  void beginSocks(){
    if(!route.socks5){finish(true);return;}socksStage=SocksStage::Greeting;
    socksWrite(route.socks5->username.isEmpty()?QByteArray::fromHex("050100"):QByteArray::fromHex("05020002"));
  }
  void socksConnect(){
    socksStage=SocksStage::Connect;QByteArray packet=QByteArray::fromHex("050100");
    if(socksTarget.isNull()){
      const auto host=QUrl::toAce(route.scrape.host());if(host.isEmpty()||host.size()>255){finish(true);return;}
      packet+=char{3};packet+=static_cast<char>(host.size());packet+=host;
    }else if(socksTarget.protocol()==QAbstractSocket::IPv4Protocol){
      packet+=char{1};const auto ip=socksTarget.toIPv4Address();for(int shift:{24,16,8,0}){packet+=static_cast<char>((ip>>shift)&255);}
    }else{
      packet+=char{4};const auto ip=socksTarget.toIPv6Address();for(int i=0;i<16;++i){packet+=static_cast<char>(ip[i]);}
    }
    const auto port=static_cast<quint16>(route.scrape.port(route.scrape.scheme()=="https"?443:80));
    packet+=static_cast<char>(port>>8);packet+=static_cast<char>(port&255);socksWrite(packet);
  }
  void readSocks(bool eof){
    if(!socket||socket->bytesAvailable()>512-socksBytes.size()){finish(true);return;}
    socksBytes+=socket->read(513-socksBytes.size());if(socksBytes.size()>512){finish(true);return;}
    const auto byte=[&](qsizetype i){return static_cast<unsigned char>(socksBytes[i]);};
    if(socksStage==SocksStage::Greeting||socksStage==SocksStage::Authentication){
      if(socksBytes.size()<2){if(eof){finish(false);}return;}
      if(socksBytes.size()!=2){finish(true);return;}
      const auto version=byte(0),value=byte(1);socksBytes.clear();
      if(socksStage==SocksStage::Authentication){if(version!=1){finish(true);}else if(value){finish(false);}else{socksConnect();}return;}
      if(version!=5){finish(true);return;} // Never retry SOCKS4/direct.
      if(value==0){socksConnect();return;}
      if(value==255){finish(false);return;}
      if(value!=2||!route.socks5||route.socks5->username.isEmpty()){finish(true);return;}
      socksStage=SocksStage::Authentication;QByteArray packet(1,char{1});packet+=static_cast<char>(route.socks5->username.size());packet+=route.socks5->username;
      packet+=static_cast<char>(route.socks5->password.size());packet+=route.socks5->password;socksWrite(packet);return;
    }
    if(socksStage!=SocksStage::Connect){finish(true);return;}
    if(socksBytes.size()<4){if(eof){finish(false);}return;}
    if(byte(0)!=5||byte(2)!=0){finish(true);return;}if(byte(1)!=0){finish(false);return;}
    qsizetype needed=0;
    if(byte(3)==1){needed=10;}else if(byte(3)==4){needed=22;}
    else if(byte(3)==3){if(socksBytes.size()<5){if(eof){finish(false);}return;}if(!byte(4)){finish(true);return;}needed=7+byte(4);}
    else{finish(true);return;}
    if(socksBytes.size()<needed){if(eof){finish(false);}return;}if(socksBytes.size()!=needed){finish(true);return;}
    socksBytes.clear();socksStage=SocksStage::Open;
    if(route.scrape.scheme()=="https"){static_cast<QSslSocket*>(socket)->startClientEncryption();}else{send();}
  }
  void challenge(const QByteArray& realm){
    if(!authorization.isEmpty()){finish(false);return;} // Actual supplied credentials were rejected.
    if(authentications==2){finish(true);return;} // Local origin budget is not a tracker failure.
    ++authentications;
    ++generation;authorised=false;awaitingAuthentication=true;clearSocket();received.clear();if(!arm()){return;}
    std::lock_guard lock(box->mutex);box->realm=realm;box->resolved.clear();box->proposedRoute=route;box->kind=Proposal::Authentication;
    box->generation=generation;box->proposal=true;box->approved=false;
  }
  void redirect(const QByteArray& location){
    const auto raw=QByteArray::fromPercentEncoding(query);if(raw.size()!=20||visited.size()>=6){finish(true);return;}
    const auto next=libraryHttpRedirectRoute(route,location,std::span<const std::byte,20>{reinterpret_cast<const std::byte*>(raw.constData()),20});
    if(!next||visited.contains(next->scrape.toEncoded())){finish(true);return;}
    // Invalidate all old socket/DNS callbacks before dropping the old transport.
    const auto effectivePort=[](const QUrl& url){return url.port(url.scheme()=="https"?443:80);};
    if(next->scrape.scheme()!=route.scrape.scheme()||next->scrape.host()!=route.scrape.host()||effectivePort(next->scrape)!=effectivePort(route.scrape)){authorization.clear();}
    ++generation;authorised=false;awaitingRedirect=true;clearSocket();received.clear();route=*next;
    visited.push_back(route.scrape.toEncoded());if(!arm()){return;}
    // No redirected DNS is submitted until the real owner acknowledges this
    // route proposal, even for the same hostname, address or port.
    std::lock_guard lock(box->mutex);box->resolved.clear();box->proposedRoute=route;box->kind=Proposal::Redirect;
    box->generation=generation;box->proposal=true;box->approved=false;
  }
  void send(){
    if(!current(generation)||(route.socks5&&socksStage!=SocksStage::Open)){return;}
    auto path=route.scrape.path(QUrl::FullyEncoded).toUtf8();if(path.isEmpty()){path="/";}
    const auto existing=route.scrape.query(QUrl::FullyEncoded).toUtf8();path+=(route.scrape.hasQuery()?"?"+existing+"&":"?");path+="info_hash="+query;
    auto host=route.scrape.host().contains(':')?route.scrape.host().toUtf8():QUrl::toAce(route.scrape.host());if(host.contains(':')){host='['+host+']';}
    const auto port=route.scrape.port();if(port!=-1){host+=':'+QByteArray::number(port);}
    const auto auth=authorization.isEmpty()?QByteArray{}:"Authorization: "+authorization+"\r\n";
    const auto packet="GET "+path+" HTTP/1.1\r\nHost: "+host+"\r\nUser-Agent: BiglyBTC\r\nAccept-Encoding: gzip\r\n"+auth+"Connection: close\r\n\r\n";
    if(socket->write(packet)!=packet.size()){finish(false);}
  }
  void read(bool eof){
    if(!current(generation)||!socket){return;}
    if(route.socks5&&socksStage!=SocksStage::Open){readSocks(eof);return;}
    const auto available=socket->bytesAvailable();const auto remaining=150*1024-received.size();
    if(remaining<0||available>remaining||available<0||static_cast<std::size_t>(available)>512*1024-transferred){finish(true);return;}
    const auto limit=std::min<qint64>(remaining,static_cast<qint64>(512*1024-transferred));
    const auto fresh=socket->read(limit+1);if(fresh.size()>limit){finish(true);return;}transferred+=static_cast<std::size_t>(fresh.size());received+=fresh;
    const auto decoded=http_tracker_wire::response(received,eof);
    if(decoded.unsupported){finish(true);return;}if(!decoded.complete){return;}
    if(!decoded.location.isEmpty()){redirect(decoded.location);return;}
    if(decoded.basicAuthentication){challenge(decoded.basicRealm);return;}
    {std::lock_guard lock(box->mutex);box->body=decoded.bytes;}
    finish(false,!decoded.failed);
  }
  void finish(bool unknown,bool body=false){
    if(finished){return;}finished=true;deadline.stop();cancelLookup();clearSocket();
    const auto observed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    {std::lock_guard lock(box->mutex);box->observedNanos=observed;box->observedWallMillis=wall;box->unknown=unknown;box->hasBody=body;box->done=true;box->proposal=false;box->kind=Proposal::None;if(!body){box->body.clear();}}
  }
};
struct NetworkThread {
  struct Context:QObject {std::unique_ptr<Operation> operation;QEventLoop& loop;explicit Context(QEventLoop& l):loop(l){};};
  std::mutex mutex;std::condition_variable ready;std::atomic<unsigned> queued{0};
  Context* context=nullptr;bool started=false;
  std::thread thread;
  NetworkThread():thread([this]{
    QEventLoop loop;Context live(loop);
    {std::lock_guard lock(mutex);context=&live;started=true;}ready.notify_one();
    loop.exec();live.operation.reset();
    {std::lock_guard lock(mutex);context=nullptr;}
  }){std::unique_lock lock(mutex);ready.wait(lock,[this]{return started;});}
  ~NetworkThread(){
    {std::lock_guard lock(mutex);if(context){auto* c=context;QMetaObject::invokeMethod(c,[c]{c->operation.reset();c->loop.quit();},Qt::QueuedConnection);}}
    if(thread.joinable()){thread.join();}
  }
  template<class F> bool post(F&& fn) noexcept {
    bool reserved=false;try{std::lock_guard lock(mutex);if(!context||queued.load()>=4){return false;}auto* c=context;++queued;reserved=true;
      const bool posted=QMetaObject::invokeMethod(c,[this,c,fn=std::forward<F>(fn)]()mutable{--queued;try{fn(*c);}catch(...){c->operation.reset();}},Qt::QueuedConnection);
      if(!posted){--queued;}reserved=false;return posted;
    }catch(...){if(reserved){--queued;}return false;}
  }
};
std::optional<unsigned> scrapeFuzz(){
  static const auto jitter=[]()->std::optional<unsigned>{std::uint32_t entropy=0;for(unsigned i=0;i<8;++i){if(RAND_bytes(reinterpret_cast<unsigned char*>(&entropy),sizeof(entropy))!=1){return {};}if(entropy<UINT32_MAX-UINT32_MAX%180){return entropy%180;}}return {};}();return jitter;
}
QByteArray escaped(std::span<const std::byte> hash){
  constexpr char hex[]="0123456789ABCDEF";QByteArray out;
  // Java URLEncoder ISO8859-1 then '+'→'%20', including its literal '*' rule.
  for(const auto v:hash){const auto c=std::to_integer<unsigned char>(v);
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'||c=='*'){out+=static_cast<char>(c);}
    else{out+='%';out+=hex[c>>4];out+=hex[c&15];}
  }return out;
}
}
struct LibraryHttpTracker::Impl {
  const std::thread::id worker=std::this_thread::get_id();
  struct Round{Request request;LibraryHttpRoute route;std::shared_ptr<Mailbox> box;std::int64_t started,wall;bool singleOnly;};
  std::optional<Round> round;
  std::unique_ptr<NetworkThread> network;
  QSslConfiguration tls=QSslConfiguration::defaultConfiguration();
  LibraryHttpResolver resolver;
  unsigned fuzz=0;bool fuzzReady=false;
  std::vector<std::string> lastResolverSubmissions;
};
std::optional<std::int64_t> http_tracker_wire::announceDeadline(const core::QueueTrackerTerminal& value,unsigned fuzz){
  if(value.kind!=core::QueueTrackerTerminalKind::Announce||!value.comparable||!value.present||(value.present&~7u)||value.observedMillis<0||fuzz>=180){return {};}
  const auto narrow=[](std::int64_t n){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(n));};
  const auto seeds=(value.present&1)?narrow(value.complete):0,peers=(value.present&2)?narrow(value.incomplete):0;
  if(seeds<0||peers<0){return {};}
  const auto delay=interval(0,seeds,fuzz);
  if(delay>0&&value.observedMillis>INT64_MAX-delay){return INT64_MAX;}
  if(delay<0&&value.observedMillis<INT64_MIN-delay){return INT64_MIN;}
  return value.observedMillis+delay; // Negative Java-int interval never shortens an existing deadline.
}
std::optional<std::int64_t> libraryTrackerScrapeDelay(std::int32_t seeds){
  const auto fuzz=scrapeFuzz();return fuzz?std::optional{http_tracker_wire::interval(0,seeds,*fuzz)}:std::nullopt;
}
std::optional<std::int64_t> libraryHttpAnnounceScrapeDeadline(const core::QueueTrackerTerminal& value){
  const auto fuzz=scrapeFuzz();return fuzz?http_tracker_wire::announceDeadline(value,*fuzz):std::nullopt;
}
LibraryHttpTracker::LibraryHttpTracker(LibraryHttpResolver resolver):impl_(std::make_unique<Impl>()){
  if(!resolver.submit&&!resolver.cancel){
    resolver.submit=[](const QString& host,QObject* context,LibraryHttpResolver::Completion completion){
      return QHostInfo::lookupHost(host,context,[completion=std::move(completion)](const QHostInfo& info){completion(info);});};
    resolver.cancel=[](int id){QHostInfo::abortHostLookup(id);};
  }impl_->resolver=std::move(resolver);
}
LibraryHttpTracker::~LibraryHttpTracker(){close();}
const LibraryHttpTracker::Request* LibraryHttpTracker::active()const noexcept{return impl_->round?&impl_->round->request:nullptr;}
std::vector<std::string> LibraryHttpTracker::resolverSubmissions()const{
  if(std::this_thread::get_id()!=impl_->worker){return {};}
  if(!impl_->round){return impl_->lastResolverSubmissions;}
  std::lock_guard lock(impl_->round->box->mutex);return impl_->round->box->resolverSubmissions;
}
bool LibraryHttpTracker::resolvedProposalPending()const{
  if(std::this_thread::get_id()!=impl_->worker||!impl_->round){return false;}
  std::lock_guard lock(impl_->round->box->mutex);return impl_->round->box->proposal&&impl_->round->box->kind==Proposal::Address&&!impl_->round->box->approved&&!impl_->round->box->done;
}
bool LibraryHttpTracker::authenticationProposalPending()const{
  if(std::this_thread::get_id()!=impl_->worker||!impl_->round){return false;}
  std::lock_guard lock(impl_->round->box->mutex);return impl_->round->box->proposal&&impl_->round->box->kind==Proposal::Authentication&&!impl_->round->box->approved&&!impl_->round->box->done;
}
bool LibraryHttpTracker::redirectProposalPending()const{
  if(std::this_thread::get_id()!=impl_->worker||!impl_->round){return false;}
  std::lock_guard lock(impl_->round->box->mutex);return impl_->round->box->proposal&&impl_->round->box->kind==Proposal::Redirect&&!impl_->round->box->approved&&!impl_->round->box->done;
}
bool LibraryHttpTracker::testTlsConfiguration(const QSslConfiguration& config){if(std::this_thread::get_id()!=impl_->worker||impl_->network||impl_->round){return false;}impl_->tls=config;return true;}
std::expected<void,storage::Error> LibraryHttpTracker::start(Request request,const LibraryHttpRoute& route,std::int64_t now,std::int64_t wall,bool singleOnly,const std::optional<QList<QSslCertificate>>& trustAnchors){
  if(std::this_thread::get_id()!=impl_->worker||impl_->round||!impl_->resolver.submit||!impl_->resolver.cancel||request.hash.bytes().size()!=20||!request.nonce||request.responseKey.empty()||request.responseKey.size()>256||request.scope.empty()||request.scope.size()>256||request.configuration.size()>8192||now<0){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"HTTP scrape worker or request changed"});}
  if(!impl_->fuzzReady){const auto jitter=scrapeFuzz();
    if(!jitter){return std::unexpected(storage::Error{storage::ErrorCode::Io,"HTTP scrape entropy unavailable"});}impl_->fuzz=*jitter;impl_->fuzzReady=true;}
  auto tls=impl_->tls;if(trustAnchors){tls.setCaCertificates(*trustAnchors);}
  if(!impl_->network){impl_->network=std::make_unique<NetworkThread>();}
  impl_->lastResolverSubmissions.clear();auto box=std::make_shared<Mailbox>();const auto encoded=escaped(request.hash.bytes());const auto resolver=impl_->resolver;
  impl_->round.emplace(Impl::Round{std::move(request),route,box,now,wall,singleOnly});
  if(!impl_->network->post([box,route,encoded,tls=std::move(tls),resolver](auto& c){c.operation.reset();if(!box->cancelled){c.operation=std::make_unique<Operation>(box,route,encoded,tls,resolver,&c);}})){
    cancel();return std::unexpected(storage::Error{storage::ErrorCode::Io,"HTTP scrape event loop unavailable"});
  }return {};
}
std::expected<std::optional<LibraryHttpTracker::Result>,storage::Error> LibraryHttpTracker::poll(std::int64_t now,std::int64_t wall,const std::function<bool(const Request&)>& authorise,const Authentication& authenticate){
  (void)wall;
  if(std::this_thread::get_id()!=impl_->worker){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"HTTP scrape worker changed"});}
  if(!impl_->round){return std::optional<Result>{};}auto& r=*impl_->round;
  if(now<r.started){cancel();return std::optional<Result>{};}
  QList<QHostAddress> proposal,proxyProposal;QByteArray body,realm;bool done=false,unknown=false,hasBody=false,trackerHostNotFound=false;
  Proposal kind=Proposal::None;std::uint64_t generation=0;std::int64_t observedNanos=0,observedWall=0;LibraryHttpRoute proposedRoute;
  {std::lock_guard lock(r.box->mutex);if(r.box->proposal&&!r.box->approved){proposal=r.box->resolved;proxyProposal=r.box->proxyResolved;trackerHostNotFound=r.box->trackerHostNotFound;kind=r.box->kind;generation=r.box->generation;proposedRoute=r.box->proposedRoute;realm=r.box->realm;r.box->approved=true;}done=r.box->done;unknown=r.box->unknown;hasBody=r.box->hasBody;if(done){body=r.box->body;observedNanos=r.box->observedNanos;observedWall=r.box->observedWallMillis;}}
  if((kind!=Proposal::None||done)&&!authorise(r.request)){cancel();return std::optional<Result>{};}
  if(kind!=Proposal::None&&!done){
    const auto checked=libraryHttpRedirectRoute(r.route,proposedRoute.scrape.toEncoded(),std::span<const std::byte,20>{r.request.hash.bytes().data(),20});
    if(!checked||*checked!=proposedRoute){cancel();return std::optional<Result>{};}
    const auto box=r.box;bool posted=false;
    if(kind==Proposal::Authentication){
      std::expected<std::optional<QByteArray>,storage::Error> credential;
      try{if(authenticate){credential=authenticate(r.request,proposedRoute,realm);}}catch(...){cancel();return std::unexpected(storage::Error{storage::ErrorCode::Io,"Saved tracker authentication could not be read"});}
      if(!credential&&credential.error().code!=storage::ErrorCode::Unsupported){cancel();return std::unexpected(credential.error());}
      if(!credential||!*credential){
        // An actual401 with no stored credential retains the existing network
        // failure semantics. Unsupported credential dialect stays Unknown.
        const bool unknown=!credential;
        posted=impl_->network->post([box,generation,unknown](auto& c){if(c.operation&&c.operation->box==box&&!box->cancelled){c.operation->declineAuthentication(generation,unknown);}});
      }else{
        auto header=std::move(**credential);
        if(header.size()>2048||!header.startsWith("Basic ")||!std::ranges::all_of(header,[](unsigned char c){return c>=32&&c<=126;})){cancel();return std::optional<Result>{};}
        posted=impl_->network->post([box,generation,header=std::move(header)](auto& c)mutable{if(c.operation&&c.operation->box==box&&!box->cancelled){c.operation->resumeAuthentication(generation,std::move(header));}});
      }
    }
    else if(kind==Proposal::Redirect){posted=impl_->network->post([box,generation](auto& c){if(c.operation&&c.operation->box==box&&!box->cancelled){c.operation->resumeRedirect(generation);}});}
    else{
      const auto endpoint=proposedRoute.socks5?libraryHttpProxyEndpoint(proposedRoute,proxyProposal):libraryHttpEndpoint(proposedRoute,proposal);
      const auto target=proposedRoute.socks5?libraryHttpProxyTarget(proposedRoute,proposal,trackerHostNotFound):std::optional<QHostAddress>{QHostAddress{}};
      if(!endpoint||!target){cancel();return std::optional<Result>{};}
      posted=impl_->network->post([box,generation,endpoint,target](auto& c){if(c.operation&&c.operation->box==box&&!box->cancelled){c.operation->approve(generation,endpoint->first,endpoint->second,*target);}});
    }
    if(!posted){cancel();return std::unexpected(storage::Error{storage::ErrorCode::Busy,"HTTP route approval could not be queued"});}
  }
  if(!done){if(now-r.started>=60000){close();}return std::optional<Result>{};}
  if(observedNanos<=0||observedWall<0){cancel();return std::optional<Result>{};}
  core::QueueTrackerTerminal terminal;terminal.observedMillis=observedNanos/1000000;terminal.observedWallMillis=observedWall;terminal.requestStartedWallMillis=r.wall;
  if(hasBody){terminal=http_tracker_wire::scrape(body.toStdString(),std::span<const std::byte,20>{r.request.hash.bytes().data(),20},terminal.observedMillis,observedWall,r.wall,r.singleOnly,impl_->fuzz);}
  else{terminal.definitiveFailure=!unknown;}
  Result result{r.request,terminal,observedNanos};cancel();return std::optional<Result>{std::move(result)};
}
void LibraryHttpTracker::cancel()noexcept{
  if(!impl_||!impl_->round){return;}const auto box=impl_->round->box;box->cancelled=true;
  try{std::lock_guard lock(box->mutex);impl_->lastResolverSubmissions=box->resolverSubmissions;}catch(...){impl_->lastResolverSubmissions.clear();}
  impl_->round.reset();
  if(impl_->network){impl_->network->post([box](auto& c){if(c.operation&&c.operation->box==box){c.operation.reset();}});}
}
void LibraryHttpTracker::close()noexcept{cancel();if(impl_){impl_->network.reset();}}
} // namespace biglybtc::engine::detail
