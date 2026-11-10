// SPDX-License-Identifier: GPL-2.0-or-later
#include "room.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace biglybtc;
namespace chat=services::msgsync;namespace crypto=plugins::msgsync;
void need(bool good,const char* text){if(!good){throw std::runtime_error(text);}}
template<class T,class E>T take(std::expected<T,E> value){need(value.has_value(),"Unexpected chat error");return std::move(*value);}
chat::Room room(unsigned n,std::string channel="Generated general room",bool ro=false){
 auto identity=take(crypto::generateMessageIdentity());auto signer=take(crypto::MessageSigner::open(identity.publicKey,identity.privateKey));chat::Uid uid{};for(int i=0;i<4;++i){uid[static_cast<std::size_t>(i)]=static_cast<std::uint8_t>(n>>(i*8));}
 return take(chat::Room::open(std::move(channel),uid,std::move(signer),std::string("d1:a4:")+std::string("\x7f\0\0\x01",4)+"1:pi43123e1:vi55ee",ro));
}
void exchange(chat::Room& sender,chat::Room& receiver,std::int64_t now){for(int n=0;n<40;++n){auto request=take(receiver.request(now));auto reply=take(sender.answer(std::move(request),now));auto result=take(receiver.receive(std::move(reply),now));need(result.rejected==0,"Valid signed message rejected");if(!result.more){return;}}throw std::runtime_error("History convergence did not terminate");}
int main(){try{
 auto a=room(1),b=room(2);const auto first=take(a.send("Hello & Unicode: Привет","Native",100000));exchange(a,b,100000);
 auto values=take(b.messages(100000));need(values.size()==1&&values[0].text=="Hello & Unicode: Привет"&&values[0].nickname=="Native"&&!values[0].local,"General room message/payload differs");
 take(a.send("second","Native",101000));exchange(a,b,101000);values=take(b.messages(101000));need(values.size()==2&&values[1].previous==first&&values[1].sequence==2,"Previous ID/sequence lost");
 auto replay=take(b.request(101000));take(a.answer(replay,101000));need(!a.answer(replay,101000),"Duplicate request must not be delivered again");
 auto wrong=room(3,"Other generated room");need(!wrong.answer(take(b.request(101000)),101000),"Cross-room envelope accepted");
 auto readOnly=room(4,"Generated read-only room",true);need(!readOnly.send("must not send","blocked",101000),"Read-only room sent a message");
 auto saved=take(b.exportHistory(102000));auto restored=room(9);auto result=take(restored.importHistory(saved,104000));need(result.added==2,"Original encrypted history not restored");values=take(restored.messages(104000));need(values.size()==2&&values[0].ageSeconds==4&&values[1].ageSeconds==3,"History elapsed age differs");
 auto altered=saved;const auto p=altered.find("1:s");need(p!=std::string::npos,"Missing signature field");altered[p+6]^=1;need(!restored.importHistory(altered,104000)||take(restored.messages(104000)).size()==2,"Corrupt history altered trusted state");
 // Original Buddy conversation sequence spans all authors and survives
 // encrypted history handoff; an own-message-only counter loses continuity.
 auto identity=take(crypto::generateMessageIdentity());chat::Uid sameUid{};sameUid[0]=77;
 auto original=take(chat::Room::open("Generated continuity room",sameUid,take(crypto::MessageSigner::open(identity.publicKey,identity.privateKey)),"de"));auto other=room(78,"Generated continuity room");
 const auto head=take(original.send("first","Generated",110000));exchange(original,other,110000);const auto reply=take(other.send("reply","Generated peer",110000));auto peerMessages=take(other.messages(110000));need(peerMessages.back().previous==head&&peerMessages.back().sequence==2,"Conversation chain ignored another author");exchange(other,original,110000);
 auto reopened=take(chat::Room::open("Generated continuity room",sameUid,take(crypto::MessageSigner::open(identity.publicKey,identity.privateKey)),"de"));take(reopened.importHistory(take(original.exportHistory(111000)),112000));take(reopened.send("after reopen","Generated",112000));auto continued=take(reopened.messages(112000));need(continued.back().previous==reply&&continued.back().sequence==3,"History reset conversation previous ID/sequence");const auto stable=continued.back().timestamp;need(take(reopened.messages(112999)).back().timestamp==stable,"Display timestamp drifted within an age second");
 auto hub=room(9000);const auto start=std::chrono::steady_clock::now();
 for(unsigned n=10;n<522;++n){auto peer=room(n);take(peer.send("Group message "+std::to_string(n),"Generated participant "+std::to_string(n),200000+n*1000));for(int retry=0;retry<20;++retry){exchange(peer,hub,200000+n*1000);const auto retained=take(hub.messages(200000+n*1000));if(!retained.empty()&&retained.back().text=="Group message "+std::to_string(n)){break;}}need(hub.retainedMessages()<=128&&hub.retainedKeys()<=128,"Group participant churn grew unbounded state");}
 need(hub.retainedMessages()==128,"Busy-room retained window differs");auto sink=room(9999);for(int retry=0;retry<20&&sink.retainedMessages()<128;++retry){exchange(hub,sink,800000);}
 need(sink.retainedMessages()==128,"Busy-room periodic catch-up incomplete after fresh bloom rounds");
 auto final=take(sink.messages(800000));need(final.front().text=="Group message 394"&&final.back().text=="Group message 521","Busy-room oldest/newest ordering differs");
 auto again=take(sink.importHistory(take(sink.exportHistory(800000)),800000));need(again.added==0&&again.duplicates==128,"History duplicate suppression differs");
 std::cout<<"PASS general rooms: send/receive, Unicode, ordering, replay, isolation, read-only, original encrypted history;512 generated identities, bounded128-message catch-up; "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"ms\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
