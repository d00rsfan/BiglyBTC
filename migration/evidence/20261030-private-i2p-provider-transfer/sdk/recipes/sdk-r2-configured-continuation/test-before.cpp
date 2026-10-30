// Private optional owner controls only; no tracker/public-network subjects.
#include "test.hpp"
#include "libtorrent/biglybtc_private_i2p_owner.hpp"
#include "libtorrent/aux_/biglybtc_private_i2p_owner.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/aux_/resolver.hpp"
#include "libtorrent/i2p_stream.hpp"
#include <boost/asio/post.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/steady_timer.hpp>
#include <algorithm>
#include <iterator>
#include <istream>
#include <chrono>
#include <memory>
using namespace lt;
#if TORRENT_USE_I2P
namespace {
settings_pack isolated(){
  settings_pack p;
  for(int key:{settings_pack::enable_incoming_tcp,settings_pack::enable_outgoing_tcp,settings_pack::enable_incoming_utp,settings_pack::enable_outgoing_utp,settings_pack::enable_dht,settings_pack::enable_lsd,settings_pack::enable_upnp,settings_pack::enable_natpmp,settings_pack::allow_i2p_mixed}){p.set_bool(key,false);}
  p.set_str(settings_pack::listen_interfaces,"");p.set_str(settings_pack::dht_bootstrap_nodes,"");p.set_str(settings_pack::i2p_hostname,"");p.set_int(settings_pack::i2p_port,0);p.set_int(settings_pack::proxy_type,settings_pack::none);return p;
}
BiglybtcPrivateI2pOwnerPolicyV1 policy(){BiglybtcPrivateI2pOwnerPolicyV1 p;p.generation=7;p.admission=11;p.capability.fill(0xf1);p.public_destination_hash.fill(0x41);p.torrent_v1.fill(0x11);p.hashes=1;p.bridge_port=49222;p.peer_port=6881;p.local_peer_port=6881;return p;}
}
TORRENT_TEST(private_i2p_owner_arguments_and_public_status_are_bounded)
{
  TEST_EQUAL(biglybtc_private_i2p_owner_abi_v1(),BiglybtcPrivateI2pOwnerAbiV1);
  auto p=policy();TEST_CHECK(aux::i2p_owner_context::valid(p));p.capability.fill(0);TEST_CHECK(!aux::i2p_owner_context::valid(p));
  p=policy();p.reserved=1;TEST_CHECK(!aux::i2p_owner_context::valid(p));p=policy();p.hashes=4;TEST_CHECK(!aux::i2p_owner_context::valid(p));
  TEST_EQUAL(biglybtc_configure_private_i2p_owner_v1(nullptr,&p),1u);TEST_EQUAL(biglybtc_read_private_i2p_owner_v1(nullptr,nullptr),1u);
  session ses{session_params{isolated()}};p=policy();
  TEST_EQUAL(biglybtc_configure_private_i2p_owner_v1(&ses,&p),0u);
  BiglybtcPrivateI2pOwnerStatusV1 status;
  TEST_EQUAL(biglybtc_read_private_i2p_owner_v1(&ses,&status),0u);TEST_EQUAL(status.phase,1u);TEST_EQUAL(status.generation,7u);TEST_EQUAL(status.admission,11u);TEST_CHECK(status.public_destination_hash==p.public_destination_hash);
  auto const* bytes=reinterpret_cast<unsigned char const*>(&status);
  TEST_CHECK(std::search(bytes,bytes+sizeof(status),p.capability.begin(),p.capability.end())==bytes+sizeof(status));
  TEST_EQUAL(biglybtc_configure_private_i2p_owner_v1(&ses,&p),2u);TEST_EQUAL(biglybtc_withdraw_private_i2p_owner_v1(&ses,8),5u);
  TEST_EQUAL(biglybtc_withdraw_private_i2p_owner_v1(&ses,7),0u);TEST_EQUAL(biglybtc_read_private_i2p_owner_v1(&ses,&status),0u);TEST_EQUAL(status.phase,2u);
}
TORRENT_TEST(private_i2p_owner_refuses_direct_network_settings_and_foreign_torrent)
{
  auto unsafe=isolated();unsafe.set_bool(settings_pack::enable_outgoing_tcp,true);session first{session_params{unsafe}};auto p=policy();TEST_EQUAL(biglybtc_configure_private_i2p_owner_v1(&first,&p),1u);
  session ses{session_params{isolated()}};TEST_EQUAL(biglybtc_configure_private_i2p_owner_v1(&ses,&p),0u);
  entry root;root["announce"]="http://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.b32.i2p/announce";
  root["info"]["length"]=3;root["info"]["name"]="owned.bin";root["info"]["piece length"]=16384;root["info"]["pieces"]=std::string(20,'x');
  std::vector<char> encoded;bencode(std::back_inserter(encoded),root);error_code ec;
  auto ti=std::make_shared<torrent_info>(span<char const>(encoded.data(),encoded.size()),ec,from_span);TEST_CHECK(!ec);
  add_torrent_params add;add.ti=ti;add.save_path=".";auto h=ses.add_torrent(add,ec);TEST_CHECK(ec);TEST_CHECK(!h.is_valid());
  settings_pack changed;changed.set_bool(settings_pack::enable_dht,true);ses.apply_settings(changed);
  BiglybtcPrivateI2pOwnerStatusV1 status;TEST_EQUAL(biglybtc_read_private_i2p_owner_v1(&ses,&status),0u);TEST_EQUAL(status.phase,2u);
}
TORRENT_TEST(private_i2p_owner_full_hash_and_withdrawal_context)
{
  auto p=policy();aux::i2p_owner_context owner(p);info_hash_t h(sha1_hash(std::string(20,'\x11').data()));TEST_CHECK(owner.matches(h));
  auto other=h;other.v1[0]^=1;TEST_CHECK(!owner.matches(other));owner.withdraw();TEST_CHECK(!owner.current());TEST_CHECK(!owner.matches(h));
}
namespace {
void fresh_http_tracker_naming_port(std::uint16_t tracker_port)
{
  // A real http_connection and owned SAM command writer, with a strictly
  // loopback protocol fixture. No Destination/application bytes are invented
  // as a successful lookup: KEY_NOT_FOUND terminates this focused wire test.
  io_context io;tcp::acceptor listener(io,tcp::endpoint(address_v4::loopback(),0));
  tcp::socket peer(io);boost::asio::streambuf input(2048);boost::asio::steady_timer deadline(io);
  auto p=policy();p.bridge_port=listener.local_endpoint().port();
  i2p_connection sam(io);sam.set_owned_bridge(std::make_shared<aux::i2p_owner_context>(p));
  aux::resolver resolver(io);std::string naming;bool opened=false,completed=false,timed_out=false;error_code result;
  std::shared_ptr<http_connection> http;
  http=std::make_shared<http_connection>(io,resolver,[&](error_code const& ec,http_parser const&,span<char const>,http_connection&){
    result=ec;completed=true;deadline.cancel();error_code ignored;sam.close(ignored);peer.close(ignored);listener.close(ignored);
  },true,1024,http_connect_handler{},http_filter_handler{},hostname_filter_handler{}
#if TORRENT_USE_SSL
  ,nullptr
#endif
  );
  std::function<void()> next;
  next=[&]{boost::asio::async_read_until(peer,input,'\n',[&](error_code const& ec,std::size_t){
    if(ec){return;}std::istream stream(&input);std::string line;std::getline(stream,line);std::string reply;
    if(line.find("HELLO VERSION ")==0){reply="HELLO REPLY RESULT=OK VERSION=3.1\n";}
    else if(line.find("SESSION CREATE ")==0){reply="SESSION STATUS RESULT=OK DESTINATION=AAAA\n";}
    else if(line=="NAMING LOOKUP NAME=ME PURPOSE=CONTROL TO_PORT=0 FROM_PORT=0"){reply="NAMING REPLY RESULT=OK NAME=ME VALUE=AAAA\n";}
    else{naming=line;reply="NAMING REPLY RESULT=KEY_NOT_FOUND NAME=owned\n";}
    auto bytes=std::make_shared<std::string>(std::move(reply));
    boost::asio::async_write(peer,boost::asio::buffer(*bytes),[&,bytes](error_code const& written,std::size_t){if(!written){next();}});
  });};
  listener.async_accept(peer,[&](error_code const& ec){if(!ec){next();}});
  deadline.expires_after(std::chrono::seconds(5));deadline.async_wait([&](error_code const& ec){if(ec){return;}timed_out=true;http->close(true);error_code ignored;sam.close(ignored);peer.close(ignored);listener.close(ignored);});
  sam.open("127.0.0.1",p.bridge_port,i2p_session_options{},[&](error_code const& ec){
    opened=!ec;if(ec){return;}
    const auto host=std::string(52,'a')+".b32.i2p";
    const auto port_text=tracker_port==80?std::string{}:":"+std::to_string(tracker_port);
    http->get("http://"+host+port_text+"/announce",seconds(1),nullptr,0,"owned",boost::none,aux::resolver_flags{},std::string{},&sam);
  });
  io.run();
  TEST_CHECK(opened);TEST_CHECK(completed);TEST_CHECK(!timed_out);TEST_EQUAL(result,error_code(i2p_error::key_not_found,i2p_category()));
  TEST_EQUAL(naming,"NAMING LOOKUP NAME="+std::string(52,'a')+".b32.i2p PURPOSE=TRACKER TO_PORT="+std::to_string(tracker_port)+" FROM_PORT=0");
}
}
TORRENT_TEST(private_i2p_owner_fresh_http_tracker_uses_actual_normalized_port)
{
  fresh_http_tracker_naming_port(80);fresh_http_tracker_naming_port(18443);
}
TORRENT_TEST(private_i2p_owner_withdraw_then_destroy_drains_external_executor)
{
  // Use the real session and private API. The externally pumped executor makes
  // the stage-two/request overlap deterministic, without timing sleeps/hooks.
  io_context io;
  session_proxy retirement;
  auto ses=std::make_unique<session>(session_params{isolated()},io);
  bool withdrawn=false;
  boost::asio::post(io,[&]{
    auto p=policy();
    TEST_EQUAL(biglybtc_configure_private_i2p_owner_v1(ses.get(),&p),0u);
    TEST_EQUAL(biglybtc_withdraw_private_i2p_owner_v1(ses.get(),p.generation),0u);
    withdrawn=true;
  });
  // Stop pumping as soon as synchronous withdrawal returns. Its stage-two
  // callback is queued, and cannot run before actual destruction queues the
  // normal call_abort request below.
  for(int count=0;count<64&&!withdrawn;++count){if(io.poll_one()==0){break;}}
  TEST_CHECK(withdrawn);
  retirement=ses->abort();
  ses.reset();
  io.restart();
  io.run_for(std::chrono::seconds(2));
  TEST_CHECK(io.stopped()); // All real session/backend work guards released.
  io.stop(); // Bounded cleanup if a regression leaves an outstanding guard.
}
#endif
