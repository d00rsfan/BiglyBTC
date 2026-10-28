#include "library_http_tracker.hpp"
#include <QEventLoop>
#include <QHostInfo>
#include <QNetworkProxy>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace biglybtc::engine::detail {
namespace {
struct Mailbox {
  std::mutex mutex;
  std::atomic<bool> cancelled=false;
  QList<QHostAddress> resolved;
  QByteArray body;
  bool proposal=false,approved=false,done=false,unknown=false,hasBody=false;
};
// All QObject and socket operations below run only on the private event loop.
class Operation final:public QObject {
public:
  std::shared_ptr<Mailbox> box;
  LibraryHttpRoute route;
  QByteArray query;
  QSslConfiguration tls;
  QTcpSocket* socket=nullptr;
  QTimer deadline;
  int lookup=-1;
  bool finished=false,authorised=false;
  explicit Operation(std::shared_ptr<Mailbox> value,LibraryHttpRoute r,QByteArray q,QSslConfiguration t,QObject* parent)
      :QObject(parent),box(std::move(value)),route(std::move(r)),query(std::move(q)),tls(std::move(t)){
    deadline.setSingleShot(true);QObject::connect(&deadline,&QTimer::timeout,this,[this]{finish(!authorised);});
    deadline.start(30000); // Transport deadline, independent of original scheduling intervals.
    QHostAddress numeric;
    if(numeric.setAddress(route.scrape.host())){propose({numeric});}
    else{lookup=QHostInfo::lookupHost(route.scrape.host(),this,[this](const QHostInfo& info){
      guarded([&]{lookup=-1;if(box->cancelled){return;}if(info.error()!=QHostInfo::NoError){finish(false);return;}propose(info.addresses());});
    });}
  }
  ~Operation()override{if(lookup>=0){QHostInfo::abortHostLookup(lookup);}if(socket){socket->abort();}}
  void propose(const QList<QHostAddress>& addresses){
    if(finished||box->cancelled){return;}if(addresses.isEmpty()||addresses.size()>32){finish(true);return;}
    std::lock_guard lock(box->mutex);box->resolved=addresses;box->proposal=true;
  }
  void approve(const QHostAddress& remote,const QHostAddress& local){
    if(finished||box->cancelled||socket){return;}
    authorised=true;deadline.start(30000);
    const bool encrypted=route.scrape.scheme()=="https";
    if(encrypted){auto* ssl=new QSslSocket(this);ssl->setSslConfiguration(tls);ssl->setPeerVerifyMode(QSslSocket::VerifyPeer);
      ssl->setPeerVerifyName(route.scrape.host());socket=ssl;
      QObject::connect(ssl,&QSslSocket::encrypted,this,[this]{guarded([this]{send();});});
      QObject::connect(ssl,&QSslSocket::sslErrors,this,[this](const QList<QSslError>&){finish(false);});
    }else{socket=new QTcpSocket(this);QObject::connect(socket,&QTcpSocket::connected,this,[this]{guarded([this]{send();});});}
    socket->setProxy(QNetworkProxy::NoProxy);socket->setReadBufferSize(150*1024);
    QObject::connect(socket,&QTcpSocket::readyRead,this,[this]{guarded([this]{read(false);});});
    QObject::connect(socket,&QTcpSocket::disconnected,this,[this]{guarded([this]{read(true);});});
    QObject::connect(socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError e){
      guarded([&]{if(e==QAbstractSocket::RemoteHostClosedError){read(true);}else{finish(false);}});
    });
    if(!socket->bind(local,0)){finish(false);return;}
    const auto port=static_cast<quint16>(route.scrape.port(encrypted?443:80));
    if(encrypted){static_cast<QSslSocket*>(socket)->connectToHostEncrypted(remote.toString(),port,route.scrape.host());}
    else{socket->connectToHost(remote,port);}
  }
private:
  QByteArray received;
  template<class F>void guarded(F&& action)noexcept{try{action();}catch(...){finish(true);}}
  void send(){
    if(finished||box->cancelled){return;}
    auto path=route.scrape.path(QUrl::FullyEncoded).toUtf8();if(path.isEmpty()){path="/";}
    const auto existing=route.scrape.query(QUrl::FullyEncoded).toUtf8();path+=(route.scrape.hasQuery()?"?"+existing+"&":"?");path+="info_hash="+query;
    auto host=route.scrape.host().contains(':')?route.scrape.host().toUtf8():QUrl::toAce(route.scrape.host());if(host.contains(':')){host='['+host+']';}
    const auto port=route.scrape.port();if(port!=-1){host+=':'+QByteArray::number(port);}
    const auto packet="GET "+path+" HTTP/1.1\r\nHost: "+host+"\r\nUser-Agent: BiglyBTC\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    if(socket->write(packet)!=packet.size()){finish(false);}
  }
  void read(bool eof){
    if(finished||box->cancelled||!socket){return;}
    const auto remaining=150*1024-received.size();if(remaining<0||socket->bytesAvailable()>remaining){finish(true);return;}
    received+=socket->readAll();const auto decoded=http_tracker_wire::response(received,eof);
    if(decoded.unsupported){finish(true);return;}if(!decoded.complete){return;}
    {std::lock_guard lock(box->mutex);box->body=decoded.bytes;}
    finish(false,!decoded.failed);
  }
  void finish(bool unknown,bool body=false){
    if(finished){return;}finished=true;deadline.stop();if(lookup>=0){QHostInfo::abortHostLookup(lookup);lookup=-1;}
    if(socket){socket->abort();}
    // Publish only after this socket is closed; owner reauthenticates delivery.
    {std::lock_guard lock(box->mutex);box->unknown=unknown;box->hasBody=body;box->done=true;if(!body){box->body.clear();}}
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
  unsigned fuzz=0;bool fuzzReady=false;
};
LibraryHttpTracker::LibraryHttpTracker():impl_(std::make_unique<Impl>()){}
LibraryHttpTracker::~LibraryHttpTracker(){close();}
const LibraryHttpTracker::Request* LibraryHttpTracker::active()const noexcept{return impl_->round?&impl_->round->request:nullptr;}
bool LibraryHttpTracker::resolvedProposalPending()const{
  if(std::this_thread::get_id()!=impl_->worker||!impl_->round){return false;}
  std::lock_guard lock(impl_->round->box->mutex);return impl_->round->box->proposal&&!impl_->round->box->approved&&!impl_->round->box->done;
}
bool LibraryHttpTracker::testTlsConfiguration(const QSslConfiguration& config){if(std::this_thread::get_id()!=impl_->worker||impl_->network||impl_->round){return false;}impl_->tls=config;return true;}
std::expected<void,storage::Error> LibraryHttpTracker::start(Request request,const LibraryHttpRoute& route,std::int64_t now,std::int64_t wall,bool singleOnly){
  if(std::this_thread::get_id()!=impl_->worker||impl_->round||request.hash.bytes().size()!=20||!request.nonce||request.responseKey.empty()||request.responseKey.size()>256||request.scope.empty()||request.scope.size()>256||request.configuration.size()>8192||now<0){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"HTTP scrape worker or request changed"});}
  if(!impl_->fuzzReady){static const auto jitter=[]()->std::optional<unsigned>{std::uint32_t entropy=0;for(unsigned i=0;i<8;++i){if(RAND_bytes(reinterpret_cast<unsigned char*>(&entropy),sizeof(entropy))!=1){return {};}if(entropy<UINT32_MAX-UINT32_MAX%180){return entropy%180;}}return {};}();
    if(!jitter){return std::unexpected(storage::Error{storage::ErrorCode::Io,"HTTP scrape entropy unavailable"});}impl_->fuzz=*jitter;impl_->fuzzReady=true;}
  if(!impl_->network){impl_->network=std::make_unique<NetworkThread>();}
  auto box=std::make_shared<Mailbox>();const auto encoded=escaped(request.hash.bytes());
  impl_->round.emplace(Impl::Round{std::move(request),route,box,now,wall,singleOnly});
  if(!impl_->network->post([box,route,encoded,tls=impl_->tls](auto& c){c.operation.reset();if(!box->cancelled){c.operation=std::make_unique<Operation>(box,route,encoded,tls,&c);}})){
    cancel();return std::unexpected(storage::Error{storage::ErrorCode::Io,"HTTP scrape event loop unavailable"});
  }return {};
}
std::expected<std::optional<LibraryHttpTracker::Result>,storage::Error> LibraryHttpTracker::poll(std::int64_t now,std::int64_t wall,const std::function<bool(const Request&)>& authorise){
  if(std::this_thread::get_id()!=impl_->worker){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"HTTP scrape worker changed"});}
  if(!impl_->round){return std::optional<Result>{};}auto& r=*impl_->round;
  if(now<r.started){cancel();return std::optional<Result>{};}
  QList<QHostAddress> proposal;QByteArray body;bool done=false,unknown=false,hasBody=false;
  {std::lock_guard lock(r.box->mutex);if(r.box->proposal&&!r.box->approved){proposal=r.box->resolved;r.box->approved=true;}done=r.box->done;unknown=r.box->unknown;hasBody=r.box->hasBody;if(done){body=r.box->body;}}
  if((!proposal.isEmpty()||done)&&!authorise(r.request)){cancel();return std::optional<Result>{};}
  if(!proposal.isEmpty()&&!done){const auto endpoint=libraryHttpEndpoint(r.route,proposal);if(!endpoint){cancel();return std::optional<Result>{};}
    const auto box=r.box;if(!impl_->network->post([box,endpoint](auto& c){if(c.operation&&c.operation->box==box&&!box->cancelled){c.operation->approve(endpoint->first,endpoint->second);}})){
      cancel();return std::unexpected(storage::Error{storage::ErrorCode::Busy,"HTTP endpoint approval could not be queued"});
    }
  }
  if(!done){if(now-r.started>=60000){close();}return std::optional<Result>{};}
  core::QueueTrackerTerminal terminal;terminal.observedMillis=now;terminal.observedWallMillis=wall;terminal.requestStartedWallMillis=r.wall;
  if(hasBody){terminal=http_tracker_wire::scrape(body.toStdString(),std::span<const std::byte,20>{r.request.hash.bytes().data(),20},now,wall,r.wall,r.singleOnly,impl_->fuzz);}
  else{terminal.definitiveFailure=!unknown;}
  Result result{r.request,terminal};cancel();return std::optional<Result>{std::move(result)};
}
void LibraryHttpTracker::cancel()noexcept{
  if(!impl_||!impl_->round){return;}const auto box=impl_->round->box;box->cancelled=true;impl_->round.reset();
  if(impl_->network){impl_->network->post([box](auto& c){if(c.operation&&c.operation->box==box){c.operation.reset();}});}
}
void LibraryHttpTracker::close()noexcept{cancel();if(impl_){impl_->network.reset();}}
} // namespace biglybtc::engine::detail
