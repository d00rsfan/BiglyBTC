// SPDX-License-Identifier: GPL-2.0-or-later
#include "datagram.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
using namespace biglybtc::services::i2p;
namespace {
void require(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
template<class T>T take(Result<T> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
Bytes hex(std::string_view text){
  require(text.size()%2==0,"Bad authored hex fixture");Bytes bytes;
  const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
  for(std::size_t i=0;i<text.size();i+=2){bytes.push_back(static_cast<Byte>((digit(text[i])<<4)|digit(text[i+1])));}return bytes;
}
Bytes destination(SigningType type,View pub,bool keyCertificate=false,std::uint16_t encryption=0){
  const bool cert=type!=SigningType::DsaSha1||keyCertificate;Bytes result(cert?391:387);
  for(std::size_t i=0;i<384;++i){result[i]=static_cast<Byte>(i*29+7);}
  require(pub.size()==(type==SigningType::DsaSha1?128:32),"Wrong authored public key width");
  std::copy(pub.begin(),pub.end(),result.begin()+384-pub.size());result[384]=cert?5:0;result[385]=0;result[386]=cert?4:0;
  if(cert){result[387]=0;result[388]=static_cast<Byte>(type);result[389]=0;result[390]=static_cast<Byte>(encryption);}return result;
}
void signaturesAndDefaultDestinations(){
  const auto edSeed=hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
  const auto edPublic=hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  require(take(signingPublicKey(SigningType::Ed25519,edSeed))==edPublic,"RFC8032 Ed25519 key derivation disagrees");
  Bytes dsaPrivate(20);dsaPrivate.back()=1;
  const auto generator=hex("0c1f4d27d40093b429e962d7223824e0bbc47e7c832a39236fc683af84889581075ff9082ed32353d4374d7301cda1d23c431f4698599dda02451824ff369752593647cc3ddc197de985e43d136cdcfc6bd5409cd2f450821142a5e6f8eb1c3ab5d0484b8129fcf17bce4f7f33321c3cb3dbb14a905e7b2b3e93be4708cbcc82");
  require(take(signingPublicKey(SigningType::DsaSha1,dsaPrivate))==generator,"Original DSA g^1 public key disagrees");
  for(const auto type:{SigningType::DsaSha1,SigningType::Ed25519}){
    const auto& secret=type==SigningType::DsaSha1?dsaPrivate:edSeed;const auto& pub=type==SigningType::DsaSha1?generator:edPublic;
    const auto raw=destination(type,pub);const auto parsed=take(decodeDestination(raw));
    require(parsed.wire==raw&&parsed.signingType==type&&parsed.signingPublicKey==pub&&parsed.encryptionType==0,"Destination sign key/padding projection changed");
    const auto alternate=take(decodeDestination(destination(type,pub,true,4)));require(alternate.encryptionType==4&&alternate.signingPublicKey==pub,"X25519 key certificate changed signing projection");
    Hash recipient{};recipient.fill(0x45);
    const auto overhead=raw.size()+2+(type==SigningType::DsaSha1?40:64);
    for(const auto size:{std::size_t{0},std::size_t{1},std::size_t{1024},MaximumWireBytes-overhead}){
      Bytes payload(size);for(std::size_t i=0;i<size;++i){payload[i]=static_cast<Byte>(i*13);}
      const auto wire=take(encode2(raw,secret,recipient,payload));const auto loaded=take(decode2(wire,recipient));
      require(loaded.payload==payload&&loaded.sender.wire==raw&&wire.size()==overhead+size,"Datagram2 exact payload/sender projection failed");
      auto otherRecipient=recipient;otherRecipient[0]^=1;const auto wrong=decode2(wire,otherRecipient);require(!wrong&&wrong.error().code==ErrorCode::BadSignature,"Recipient binding did not reject wrong destination hash");
    }
    const auto tooLarge=encode2(raw,secret,recipient,Bytes(MaximumWireBytes-overhead+1));require(!tooLarge&&tooLarge.error().code==ErrorCode::TooLarge,"Datagram2 maker cap disappeared");
    auto wire=take(encode2(raw,secret,recipient,Bytes{0,0xff,0x51}));auto bad=wire;bad[raw.size()+2]^=1;require(!decode2(bad,recipient),"Modified payload verified");bad=wire;bad.back()^=1;require(!decode2(bad,recipient),"Modified signature verified");
    auto mismatch=secret;mismatch.back()^=2;const auto refused=encode2(raw,mismatch,recipient,{});require(!refused&&refused.error().code==ErrorCode::KeyMismatch,"Wrong private key was not refused");
    // Original Datagram2 does NOT sign the sender encryption key/padding.
    // Demonstrate the exact limit; never present raw destination hash as a
    // cryptographically authenticated sender/session authority to callers.
    bad=wire;bad[0]^=1;auto padding=take(decode2(bad,recipient));require(padding.sender.hash!=parsed.hash&&padding.payload==Bytes({0,0xff,0x51}),"Unexpected sender-padding authentication claim");
    for(const Byte extension:{Byte{0x10},Byte{0x20},Byte{0x40}}){bad=wire;bad[raw.size()+1]|=extension;const auto unsupported=decode2(bad,recipient);require(!unsupported&&unsupported.error().code==ErrorCode::Unsupported,"Unsupported options/offline flag silently admitted");}
    bad=wire;bad[raw.size()+1]=3;require(!decode2(bad,recipient),"Datagram3 version accepted as signed Datagram2");
    bad.assign(wire.begin(),wire.begin()+raw.size()+2);require(!decode2(bad,recipient),"Truncated Datagram2 signature accepted");
  }
  const auto bad=signingPublicKey(SigningType::DsaSha1,Bytes(20));require(!bad&&bad.error().code==ErrorCode::KeyMismatch,"DSA zero exponent accepted");
}
void unsignedDatagram3AndMalformedInput(){
  const auto seed=hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");const auto raw=destination(SigningType::Ed25519,take(signingPublicKey(SigningType::Ed25519,seed)));const auto sender=take(decodeDestination(raw));
  for(const auto size:{std::size_t{0},std::size_t{3},MaximumWireBytes-34}){
    const Bytes payload(size,0xab);const auto wire=take(encode3(raw,payload));const auto loaded=take(decode3(wire));require(loaded.claimedSender==sender.hash&&loaded.payload==payload,"Datagram3 hash/payload envelope changed");
    auto forged=wire;forged[0]^=1;const auto claimed=take(decode3(forged));require(claimed.claimedSender!=sender.hash,"Unsigned Datagram3 was falsely described as authenticated");
  }
  require(!encode3(raw,Bytes(MaximumWireBytes-33)),"Datagram3 maker cap disappeared");require(!decode3(Bytes(33)),"Truncated Datagram3 header accepted");require(!decode3(Bytes(MaximumWireBytes+1)),"Oversize Datagram3 accepted");
  auto bad=take(encode3(raw,{}));bad[33]|=0x10;auto options=decode3(bad);require(!options&&options.error().code==ErrorCode::Unsupported,"Datagram3 optional properties silently admitted");
  bad=raw;bad.push_back(0);require(!decodeDestination(bad),"Destination trailing bytes accepted");bad=raw;bad[386]=0xff;require(!decodeDestination(bad),"Truncated destination key certificate accepted");
  bad=raw;bad[388]=8;const auto type=decodeDestination(bad);require(!type&&type.error().code==ErrorCode::Unsupported,"Unsupported prehash signing type accepted");
  bad=raw;bad[384]=1;require(!decodeDestination(bad),"Unsupported destination certificate accepted");
}
// The export bridge is test-only: fixed generated leaf names, a private owned
// directory, bounded regular input files and exclusive outputs. It invokes the
// production codec directly, never a second implementation of its wire format.
struct Descriptor {
  int value;
  explicit Descriptor(int fd):value(fd){}
  ~Descriptor(){if(value>=0){::close(value);}}
  Descriptor(const Descriptor&)=delete;
  Descriptor& operator=(const Descriptor&)=delete;
};
void ioRequire(bool ok,const std::string& operation){
  if(!ok){const int saved=errno;throw std::runtime_error(operation+": "+std::strerror(saved));}
}
Bytes readFixture(int directory,const std::string& leaf,bool requireOwnership=true){
  Descriptor file(::openat(directory,leaf.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));ioRequire(file.value>=0,"open "+leaf);
  struct stat before{},after{},named{};ioRequire(::fstat(file.value,&before)==0,"fstat "+leaf);
  require(S_ISREG(before.st_mode)&&before.st_size>=0&&before.st_size<=static_cast<off_t>(MaximumWireBytes),"Fixture input must be a bounded regular file");
  require(!requireOwnership||(before.st_uid==::getuid()&&before.st_nlink==1),"Interop input must be an owned single-link file");
  Bytes bytes(static_cast<std::size_t>(before.st_size));std::size_t done=0;
  while(done<bytes.size()){
    const auto got=::read(file.value,bytes.data()+done,bytes.size()-done);if(got<0&&errno==EINTR){continue;}
    ioRequire(got>=0,"read "+leaf);require(got>0,"Fixture input became short");done+=static_cast<std::size_t>(got);
  }
  ioRequire(::fstat(file.value,&after)==0&&::fstatat(directory,leaf.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0,"revalidate "+leaf);
  const auto same=[](const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size
      &&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec
      &&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;};
  require(same(before,after)&&same(before,named),"Fixture input changed while being read");return bytes;
}
void writeFixture(int directory,const std::string& leaf,View bytes){
  Descriptor file(::openat(directory,leaf.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));ioRequire(file.value>=0,"exclusive create "+leaf);
  std::size_t done=0;while(done<bytes.size()){
    const auto sent=::write(file.value,bytes.data()+done,bytes.size()-done);if(sent<0&&errno==EINTR){continue;}
    ioRequire(sent>=0,"write "+leaf);require(sent>0,"Fixture write made no progress");done+=static_cast<std::size_t>(sent);
  }
  ioRequire(::fsync(file.value)==0,"sync "+leaf);
}
void retainedJavaFixtures(const char* path){
  // Public checked-in source fixtures may belong to another checkout owner.
  // Only the separate private interop export path requires same-UID leaves.
  Descriptor directory(::open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));ioRequire(directory.value>=0,"open retained Java fixture directory");
  const auto read=[&](const std::string& leaf){return readFixture(directory.value,leaf,false);};
  struct Signer {const char* name;SigningType type;std::size_t maximumPayload;const char* recipientHash;};
  constexpr std::array<Signer,2> signers{{
    {"dsa",SigningType::DsaSha1,62035,"10cdba035bd41026a230a4d58981e0122b442d07e8c2f1038e7a5142c6c20a66"},
    {"ed25519",SigningType::Ed25519,62007,"c439e09b76788b0157a984d060cb0cd462bd38b16f27c1cd45c20df5c4cc9b3a"}
  }};
  for(const auto& signer:signers){
    const auto prefix=std::string(signer.name);
    const auto fromRaw=read(prefix+".sender"),toRaw=read(prefix+".recipient"),knownTestKey=read(prefix+".known-test-key");
    const auto from=take(decodeDestination(fromRaw)),to=take(decodeDestination(toRaw));
    const auto digest=hex(signer.recipientHash);Hash recipient{};std::copy(digest.begin(),digest.end(),recipient.begin());
    require(from.wire==fromRaw&&to.wire==toRaw&&from.signingType==signer.type&&to.signingType==signer.type,"Retained original Destination bytes/type changed");
    require(to.hash==recipient,"Retained exact recipient hash changed");
    require(take(signingPublicKey(signer.type,knownTestKey))==from.signingPublicKey,"Retained synthetic signing key mismatch");
    struct Payload {const char* label;std::size_t size;};
    const std::array<Payload,4> cases{{{"empty",0},{"binary",7},{"1024",1024},{"maximum",signer.maximumPayload}}};
    for(const auto& test:cases){
      const auto base=prefix+"-"+test.label;const auto payload=read(base+".payload");
      require(payload.size()==test.size,"Retained original payload size changed");
      if(std::string_view(test.label)=="binary"){require(payload==hex("0001ff51008000"),"Retained original binary payload changed");}
      else{for(std::size_t i=0;i<payload.size();++i){require(payload[i]==static_cast<Byte>(i*13),"Retained original patterned payload changed");}}
      const auto java2=read(base+".java2"),java3=read(base+".java3");
      const auto signedPacket=take(decode2(java2,recipient));const auto unsignedPacket=take(decode3(java3));
      require(signedPacket.sender.wire==fromRaw&&signedPacket.payload==payload,"Retained Java Datagram2 exact sender/payload verification failed");
      require(unsignedPacket.claimedSender==from.hash&&unsignedPacket.payload==payload,"Retained UNSIGNED Java Datagram3 claimed sender/payload changed");
      auto wrongRecipient=recipient;wrongRecipient[0]^=1;const auto wrong=decode2(java2,wrongRecipient);
      require(!wrong&&wrong.error().code==ErrorCode::BadSignature,"Retained Java Datagram2 lost recipient binding");
      auto altered=java2;altered.at(altered.size()-1)^=1;require(!decode2(altered,recipient),"Altered retained Java signature verified");
      if(!payload.empty()){altered=java2;altered.at(fromRaw.size()+2)^=1;require(!decode2(altered,recipient),"Altered retained Java payload verified");}
      const auto native2=take(encode2(fromRaw,knownTestKey,recipient,payload));
      const auto siglen=signer.type==SigningType::DsaSha1?40U:64U;
      require(native2.size()==java2.size()&&std::equal(java2.begin(),java2.end()-siglen,native2.begin()),"Native Datagram2 default envelope differs from retained Java");
      if(signer.type==SigningType::Ed25519){require(native2==java2,"Native deterministic Ed25519 differs from retained Java signature");}
      // DSA signatures are randomized: compare the envelope and verify the
      // signature, rather than demand equality with one Java random sample.
      require(take(decode2(native2,recipient)).payload==payload,"Native retained-fixture signing failed");
      require(take(encode3(fromRaw,payload))==java3,"Native unsigned Datagram3 differs from retained Java bytes");
      std::cout<<"PASS retained official Java Datagram2/3 "<<base<<'\n';
    }
  }
}
void interop(const char* path){
  require(path[0]=='/',"Interop fixture root must be absolute");
  Descriptor directory(::open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));ioRequire(directory.value>=0,"open interop fixture root");
  struct stat info{};ioRequire(::fstat(directory.value,&info)==0,"stat interop fixture root");
  require(info.st_uid==::getuid()&&(info.st_mode&0777)==0700,"Interop fixture root must be owned and private");
  for(const auto* name:{"dsa","ed25519"}){
    const auto type=std::string_view(name)=="dsa"?SigningType::DsaSha1:SigningType::Ed25519;
    const auto from=take(decodeDestination(readFixture(directory.value,std::string(name)+".sender")));
    const auto to=take(decodeDestination(readFixture(directory.value,std::string(name)+".recipient")));
    const auto secret=readFixture(directory.value,std::string(name)+".known-test-key");
    require(from.signingType==type&&to.signingType==type,"Interop source signing type changed");
    require(take(signingPublicKey(type,secret))==from.signingPublicKey,"Original/native public signing key mismatch");
    for(const auto* label:{"empty","binary","1024","maximum"}){
      const auto base=std::string(name)+"-"+label;
      const auto payload=readFixture(directory.value,base+".payload");
      const auto java2=readFixture(directory.value,base+".java2"),java3=readFixture(directory.value,base+".java3");
      const auto signedPacket=take(decode2(java2,to.hash));const auto unsignedPacket=take(decode3(java3));
      require(signedPacket.sender.wire==from.wire&&signedPacket.payload==payload,"Original Datagram2 failed native exact sender/payload verification");
      require(unsignedPacket.claimedSender==from.hash&&unsignedPacket.payload==payload,"Original Datagram3 failed native exact hash/payload decode");
      const auto wrong=decode2(java2,from.hash);require(!wrong&&wrong.error().code==ErrorCode::BadSignature,"Native accepted original Datagram2 for the wrong recipient");
      auto altered=java2;altered.at(altered.size()-1)^=1;require(!decode2(altered,to.hash),"Native accepted altered original signature");
      if(!payload.empty()){altered=java2;altered.at(from.wire.size()+2)^=1;require(!decode2(altered,to.hash),"Native accepted altered original payload");}
      writeFixture(directory.value,base+".native2",take(encode2(from.wire,secret,to.hash,payload)));
      const auto native3=take(encode3(from.wire,payload));require(native3==java3,"Unsigned Datagram3 original/native wire mismatch");
      writeFixture(directory.value,base+".native3",native3);
      std::cout<<"PASS native original2/3 verify + native2/3 export "<<base<<" payload="<<payload.size()<<'\n';
    }
  }
}
}
int main(int argc,char** argv){try{
  if(argc==3&&std::string_view(argv[1])=="--interop-dir"){interop(argv[2]);}
  else{
    const bool retained=argc==3&&std::string_view(argv[1])=="--java-fixtures";
    require(argc==1||retained,"Usage: biglybtc-i2p-datagram-tests [--interop-dir ABSOLUTE_OWNED_ROOT | --java-fixtures FIXTURE_ROOT]");
    signaturesAndDefaultDestinations();unsignedDatagram3AndMalformedInput();
    if(retained){retainedJavaFixtures(argv[2]);}
    std::cout<<"PASS bounded I2P Datagram2/3 default0/7 codec\n";
  }
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
