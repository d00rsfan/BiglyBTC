// SPDX-License-Identifier: GPL-2.0-or-later
#include "identity.hpp"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

using namespace biglybtc::services::i2p;
namespace {
void require(bool ok,const char* message){if(!ok){throw std::runtime_error(message);}}
template<class T>T take(Result<T> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
Bytes copy(View view){return Bytes(view.begin(),view.end());}
bool zero(View view){return std::all_of(view.begin(),view.end(),[](Byte byte){return byte==0;});}
Bytes read(const std::filesystem::path& path){
  std::ifstream input(path,std::ios::binary|std::ios::ate);
  if(!input){throw std::runtime_error("Cannot open synthetic identity fixture: "+path.string());}
  const auto length=static_cast<std::streamoff>(input.tellg());
  require(length>=0&&length<=static_cast<std::streamoff>(MaximumClientIdentityInputBytes),"Synthetic fixture length is invalid");
  Bytes bytes(static_cast<std::size_t>(length));input.seekg(0);
  if(!bytes.empty()){input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}
  require(static_cast<bool>(input),"Cannot read complete synthetic identity fixture");return bytes;
}
Bytes keyStream(View destination,View encryptionKey,View signingKey){
  // Test-only assembly directly follows original I2PClientImpl's three writes.
  Bytes out;out.reserve(destination.size()+encryptionKey.size()+signingKey.size());
  out.insert(out.end(),destination.begin(),destination.end());out.insert(out.end(),encryptionKey.begin(),encryptionKey.end());out.insert(out.end(),signingKey.begin(),signingKey.end());return out;
}
void originalDefaultStreamAndDatagram(const std::filesystem::path& fixtures){
  for(const auto* family:{"dsa","ed25519"}){
    const auto prefix=std::string(family);const auto destination=read(fixtures/(prefix+".sender"));
    const auto secret=read(fixtures/(prefix+".known-test-key"));const auto receiver=take(decodeDestination(read(fixtures/(prefix+".recipient"))));
    Bytes encryptionKey(256);for(std::size_t i=0;i<encryptionKey.size();++i){encryptionKey[i]=static_cast<Byte>(i*11+9);}
    const auto expected=keyStream(destination,encryptionKey,secret);auto source=expected;
    auto identity=take(decodeClientIdentity(source));
    require(identity.valid()&&identity.destination()&&identity.destination()->wire==destination,"Original client Destination changed");
    require(copy(identity.keyStream())==expected&&copy(identity.encryptionPrivateKey())==encryptionKey&&copy(identity.signingPrivateKey())==secret,"Original client key order/width changed");
    auto made=take(makeClientIdentity(destination,encryptionKey,secret));require(copy(made.keyStream())==expected,"Client identity export changed exact bytes");
    std::fill(source.begin(),source.end(),0);require(copy(identity.keyStream())==expected,"Client identity borrowed caller's mutable input");
    const Bytes payload{0,1,0xff,0x51,0,0x80};const auto packet=take(identity.makeDatagram2(receiver.hash,payload));
    const auto decoded=take(decode2(packet,receiver.hash));require(decoded.payload==payload&&decoded.sender.wire==destination,"Imported client signing key cannot make default Datagram2");
    for(std::size_t size=0;size<expected.size();++size){require(!decodeClientIdentity(View(expected).first(size)),"Truncated original client key stream accepted");}
    auto extra=expected;extra.push_back(0);const auto trailing=decodeClientIdentity(extra);
    require(!trailing&&trailing.error().code==ErrorCode::InvalidFormat,"Trailing client key data accepted");
    const auto huge=decodeClientIdentity(Bytes(MaximumClientIdentityInputBytes+1));require(!huge&&huge.error().code==ErrorCode::TooLarge,"Client identity input bound disappeared");
    auto wrong=secret;wrong.at(wrong.size()-1)^=2;const auto mismatch=makeClientIdentity(destination,encryptionKey,wrong);
    require(!mismatch&&mismatch.error().code==ErrorCode::KeyMismatch,"Mismatched client signing private key accepted");
    auto changedDestination=destination;changedDestination.at(383)^=1;
    const auto wrongPublic=makeClientIdentity(changedDestination,encryptionKey,secret);require(!wrongPublic&&wrongPublic.error().code==ErrorCode::KeyMismatch,"Mismatched client public signing key accepted");
    const auto offline=decodeClientIdentity(keyStream(destination,encryptionKey,Bytes(secret.size())));
    require(!offline&&offline.error().code==ErrorCode::Unsupported,"Original all-zero offline sentinel treated as online key");
    auto extended=keyStream(destination,encryptionKey,Bytes(secret.size()));extended.insert(extended.end(),100,0);
    const auto offlineExtension=decodeClientIdentity(extended);require(!offlineExtension&&offlineExtension.error().code==ErrorCode::Unsupported,"Offline extension was silently interpreted as online keys");
    require(!makeClientIdentity(destination,Bytes(255),secret)&&!makeClientIdentity(destination,encryptionKey,Bytes(secret.size()-1)),"Client private key width not enforced");
    // I2PClientImpl expressly uses unused random encryption bytes, unrelated
    // to its random public block. Do not invent an ElGamal pair requirement.
    const auto opaque=take(makeClientIdentity(destination,Bytes(256),secret));require(zero(opaque.encryptionPrivateKey()),"Unused original encryption bytes were rewritten");
    changedDestination=destination;changedDestination.at(0)^=1;const auto preserved=take(makeClientIdentity(changedDestination,encryptionKey,secret));
    require(preserved.destination()->wire==changedDestination&&preserved.destination()->hash!=identity.destination()->hash,"Original opaque public block was canonicalized");
    std::cout<<"PASS bounded original online client identity "<<family<<'\n';
  }
}
void ownershipAndRefusal(const std::filesystem::path& fixtures){
  static_assert(!std::is_copy_constructible_v<ClientIdentity> && !std::is_copy_assignable_v<ClientIdentity>);
  static_assert(std::is_nothrow_move_constructible_v<ClientIdentity> && std::is_nothrow_move_assignable_v<ClientIdentity>);
  const auto destination=read(fixtures/"ed25519.sender"),secret=read(fixtures/"ed25519.known-test-key");
  auto source=take(makeClientIdentity(destination,Bytes(256,0xab),secret));const auto expected=copy(source.keyStream());
  const auto oldSecret=source.signingPrivateKey(),oldEncryption=source.encryptionPrivateKey();
  ClientIdentity moved(std::move(source));
  require(!source.valid()&&!source.destination()&&source.keyStream().empty()&&source.signingPrivateKey().empty()&&zero(oldSecret)&&zero(oldEncryption),"Moved client retained secret authority");
  require(moved.valid()&&copy(moved.keyStream())==expected,"Move changed original client bytes");
  ClientIdentity assigned;assigned=std::move(moved);require(assigned.valid()&&!moved.valid()&&copy(assigned.keyStream())==expected,"Move assignment lost/reused client authority");
  const auto retired=assigned.signingPrivateKey();assigned.clear();
  require(!assigned.valid()&&!assigned.destination()&&assigned.encryptionPrivateKey().empty()&&zero(retired),"Clear did not withdraw and cleanse client keys");
  const auto noSigning=assigned.makeDatagram2(Hash{},{});require(!noSigning&&noSigning.error().code==ErrorCode::KeyMismatch,"Retired client identity can still sign");
  assigned.clear(); // idempotent, no file or identity replacement.
  auto nonDefault=destination;nonDefault.at(390)=4;
  const auto otherEncryption=makeClientIdentity(nonDefault,Bytes(32),secret);require(!otherEncryption&&otherEncryption.error().code==ErrorCode::Unsupported,"Non-default encryption type silently reinterpreted");
  const auto dsa=read(fixtures/"dsa.sender"),dsaSecret=read(fixtures/"dsa.known-test-key");auto certificate=dsa;certificate.at(384)=5;certificate.at(386)=4;certificate.insert(certificate.end(),4,0);
  const auto certificateIdentity=take(makeClientIdentity(certificate,Bytes(256),dsaSecret));require(certificateIdentity.keyStream().size()==667,"Explicit four-byte DSA key certificate changed key widths");
  const auto checked=take(decodeClientIdentity(certificateIdentity.keyStream()));require(checked.destination()->wire==certificate,"Original key certificate bytes were lost");
}
// Optional offline interop bridge. Only fixed names inside a fresh Java-owned
// 0700 synthetic directory; never searches profiles or overwrites outputs.
class Descriptor {
public:
  explicit Descriptor(int value):value_(value){}
  ~Descriptor(){if(value_>=0){::close(value_);}}
  Descriptor(const Descriptor&)=delete;
  Descriptor& operator=(const Descriptor&)=delete;
  int get()const noexcept{return value_;}
private:int value_;
};
bool sameFile(const struct stat& a,const struct stat& b){
  return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&
    a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&
    a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
Bytes interopRead(int directory,const std::string& leaf){
  Descriptor input(::openat(directory,leaf.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
  require(input.get()>=0,"Cannot open owned offline identity fixture");struct stat before{},after{};
  require(::fstat(input.get(),&before)==0&&S_ISREG(before.st_mode)&&before.st_uid==::getuid()&&before.st_nlink==1&&
    before.st_size>=0&&before.st_size<=static_cast<off_t>(MaximumClientIdentityInputBytes),"Unsafe/oversized offline identity fixture");
  Bytes bytes(static_cast<std::size_t>(before.st_size));std::size_t offset=0;
  while(offset<bytes.size()){
    const auto count=::read(input.get(),bytes.data()+offset,bytes.size()-offset);
    if(count<0&&errno==EINTR){continue;}
    require(count>0,"Cannot read complete offline identity fixture");offset+=static_cast<std::size_t>(count);
  }
  Byte extra=0;require(::read(input.get(),&extra,1)==0&&::fstat(input.get(),&after)==0&&sameFile(before,after),"Offline identity fixture changed during read");
  return bytes;
}
void interopWrite(int directory,const std::string& leaf,View bytes){
  require(bytes.size()<=MaximumClientIdentityInputBytes,"Offline identity output exceeds bound");
  Descriptor output(::openat(directory,leaf.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
  require(output.get()>=0,"Offline identity output already exists or cannot be created");std::size_t offset=0;
  while(offset<bytes.size()){
    const auto count=::write(output.get(),bytes.data()+offset,bytes.size()-offset);
    if(count<0&&errno==EINTR){continue;}
    require(count>0,"Cannot write complete offline identity output");offset+=static_cast<std::size_t>(count);
  }
  require(::fsync(output.get())==0,"Cannot sync offline identity output");
}
void originalJavaInterop(const std::filesystem::path& root){
  require(root.is_absolute(),"Offline identity interop root must be absolute");
  Descriptor directory(::open(root.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));struct stat bound{};
  require(directory.get()>=0&&::fstat(directory.get(),&bound)==0&&bound.st_uid==::getuid()&&
    (bound.st_mode&0777)==0700,"Offline identity interop root must be an owned private directory");
  for(const auto* name:{"dsa-default","dsa-key-certificate","ed25519-default","ed25519-zero-encryption"}){
    const std::string label(name);const auto source=interopRead(directory.get(),label+".java.keys");
    const auto sender=interopRead(directory.get(),label+".sender"),encryption=interopRead(directory.get(),label+".decrypt");
    const auto signing=interopRead(directory.get(),label+".sign"),payload=interopRead(directory.get(),label+".payload");
    const auto receiver=take(decodeDestination(interopRead(directory.get(),label+".recipient")));
    auto imported=take(decodeClientIdentity(source));
    require(imported.destination()->wire==sender&&copy(imported.encryptionPrivateKey())==encryption&&
      copy(imported.signingPrivateKey())==signing&&copy(imported.keyStream())==source,"Actual Java identity import fields differ");
    auto exported=take(makeClientIdentity(sender,encryption,signing));
    require(copy(exported.keyStream())==source,"Native identity export differs from actual Java object writes");
    const auto javaPacket=interopRead(directory.get(),label+".java2");const auto decoded=take(decode2(javaPacket,receiver.hash));
    require(decoded.sender.wire==sender&&decoded.payload==payload,"Native failed actual Java imported-session Datagram2");
    const auto wrong=take(decodeDestination(sender));require(!decode2(javaPacket,wrong.hash),"Native accepted Java packet for wrong recipient");
    auto changed=javaPacket;changed.at(changed.size()-1)^=1;require(!decode2(changed,receiver.hash),"Native accepted changed Java signature");
    changed=javaPacket;changed.at(sender.size()+2)^=1;require(!decode2(changed,receiver.hash),"Native accepted changed Java payload");
    const auto packet=exported.makeDatagram2(receiver.hash,payload);require(packet.has_value(),"Native imported-key signer failed");
    interopWrite(directory.get(),label+".native.keys",exported.keyStream());interopWrite(directory.get(),label+".native2",*packet);
    std::cout<<"PASS actual Java online identity import/export/signature "<<label<<'\n';
  }
  require(::fsync(directory.get())==0,"Cannot sync offline identity fixture directory");
}
} // namespace
int main(int argc,char** argv){try{
  require(argc==2||(argc==4&&std::string(argv[2])=="--interop-dir"),"Usage: biglybtc-i2p-client-identity-tests RETAINED_SYNTHETIC_FIXTURE_ROOT [--interop-dir OWNED_ROOT]");
  originalDefaultStreamAndDatagram(argv[1]);ownershipAndRefusal(argv[1]);
  if(argc==4){originalJavaInterop(argv[3]);}
  std::cout<<"PASS bounded I2P client identity ownership/refusal\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
