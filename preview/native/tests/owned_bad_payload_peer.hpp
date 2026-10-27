#pragma once
// Test-only actual owned BitTorrent wire peer. No decoded counter injection.
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
namespace biglybtc::test {
class OwnedBadPayloadPeer final {
public:
  explicit OwnedBadPayloadPeer(std::string_view hash,unsigned pieces,std::string payload,unsigned short ownerPort)
      :pieces_(pieces),payload_(std::move(payload)),ownerPort_(ownerPort){
    require(hash.size()==20&&pieces>1&&pieces<=64&&payload_.size()==pieces*16384&&ownerPort_!=0,"Owned bad-peer geometry");
    std::copy(hash.begin(),hash.end(),hash_.begin());thread_=std::thread([this]{run();});
  }
  ~OwnedBadPayloadPeer(){stop_=true;if(thread_.joinable()){thread_.join();}}
  OwnedBadPayloadPeer(const OwnedBadPayloadPeer&)=delete;
  bool actualWork() const noexcept{return good_==1&&duplicate_==1&&corrupt_==1;}
  std::string failure(){std::lock_guard lock(mutex_);return failure_;}
  std::string diagnostic(){
    std::ostringstream out;out<<"connected="<<connected_.load()<<" helloSent="<<helloSent_.load()
        <<" helloReceived="<<helloReceived_.load()<<" eof="<<eof_.load()<<" requests="<<requests_.load()
        <<" good="<<good_.load()<<" duplicate="<<duplicate_.load()<<" corrupt="<<corrupt_.load();
    const auto error=failure();if(!error.empty()){out<<" failure="<<error;}return out.str();
  }
private:
  static void require(bool okay,const char* why){if(!okay){throw std::runtime_error(why);}}
  static std::uint32_t number(const char* data){std::uint32_t value=0;std::memcpy(&value,data,4);return ntohl(value);}
  static void integer(std::string& data,std::uint32_t value){value=htonl(value);data.append(reinterpret_cast<const char*>(&value),4);}
  bool send(int fd,const std::string& data){for(std::size_t n=0;n<data.size()&&!stop_;){const auto size=::send(fd,data.data()+n,data.size()-n,MSG_NOSIGNAL);if(size<=0){return false;}n+=static_cast<std::size_t>(size);}return !stop_;}
  void piece(int fd,unsigned index,bool corrupt){std::string data;integer(data,16384+9);data.push_back(7);integer(data,index);integer(data,0);data+=payload_.substr(index*16384,16384);if(corrupt){data[13]^=1;}require(send(fd,data),"Actual owned block send");}
  void run() noexcept{try{
    const int client=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);require(client>=0,"Owned incoming peer socket");
    struct Close{int fd;~Close(){::close(fd);}} close{client};
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=::inet_addr("127.0.0.2");require(::bind(client,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0,"Owned numeric peer bind");
    sockaddr_in owner{};owner.sin_family=AF_INET;owner.sin_addr.s_addr=::inet_addr("127.0.0.1");owner.sin_port=htons(ownerPort_);
    require(::connect(client,reinterpret_cast<sockaddr*>(&owner),sizeof(owner))==0,"Actual owner listener connection");connected_=true;
    timeval timeout{1,0};::setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    // Initiate actual plaintext inbound BT. pe_enabled accepts this without
    // changing the real owner's encryption policy or decoding an MSE probe.
    std::string hello(1,char(19));hello+="BitTorrent protocol";hello.append(8,'\0');hello.append(hash_.data(),hash_.size());hello.append("-BN3-OWNED-PEER-00001",20);
    require(hello.size()==68&&send(client,hello),"Actual incoming plaintext handshake");helloSent_=true;
    bool handshake=false;int goodPiece=-1;std::string input;std::array<char,32768> buffer{};
    while(!stop_){pollfd ready{client,POLLIN,0};if(::poll(&ready,1,50)<=0){continue;}const auto size=::recv(client,buffer.data(),buffer.size(),0);if(size<=0){
        if(stop_){break;}
        if(size==0){eof_=true;if(!actualWork()){throw std::runtime_error(handshake?"Owner EOF before actual good/duplicate/corrupt work":"Owner EOF before returned BitTorrent handshake");}break;}
        if(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK){continue;}throw std::runtime_error("Actual owned peer receive failed");
      }input.append(buffer.data(),static_cast<std::size_t>(size));
      if(!handshake){
        if(input.size()<68){continue;}require(static_cast<unsigned char>(input[0])==19&&input.substr(1,19)=="BitTorrent protocol"&&std::equal(hash_.begin(),hash_.end(),input.data()+28),"Actual owned infohash handshake");
        std::string available((pieces_+7)/8,char(0xff));if(pieces_%8){available.back()=static_cast<char>(0xffu<<(8-pieces_%8));}
        std::string bits;integer(bits,static_cast<std::uint32_t>(1+available.size()));bits.push_back(5);bits+=available;require(send(client,bits),"Actual owned bitfield");
        std::string unchoke;integer(unchoke,1);unchoke.push_back(1);require(send(client,unchoke),"Actual owned unchoke");input.erase(0,68);handshake=true;helloReceived_=true;
      }
      while(input.size()>=4){const auto bytes=number(input.data());require(bytes<=32768,"Owned message bound");if(input.size()<4+bytes){break;}
        if(bytes==13&&static_cast<unsigned char>(input[4])==6){const auto index=number(input.data()+5),offset=number(input.data()+9),length=number(input.data()+13);require(index<pieces_&&offset==0&&length==16384,"Actual owned block request");++requests_;
          if(goodPiece<0){goodPiece=static_cast<int>(index);piece(client,index,false);++good_;piece(client,index,false);++duplicate_;}
          else if(static_cast<int>(index)!=goodPiece&&corrupt_==0){piece(client,index,true);++corrupt_;}
        }
        input.erase(0,4+bytes);
      }
    }
  }catch(const std::exception& error){std::lock_guard lock(mutex_);failure_=error.what();}}
  unsigned pieces_;std::string payload_;unsigned short ownerPort_=0;std::array<char,20> hash_{};
  std::atomic<bool> stop_{false},connected_{false},helloSent_{false},helloReceived_{false},eof_{false};std::atomic<unsigned> good_{0},duplicate_{0},corrupt_{0},requests_{0};std::thread thread_;std::mutex mutex_;std::string failure_;
};
} // namespace biglybtc::test
