/*

Copyright (c) 2009, 2013-2019, Arvid Norberg
Copyright (c) 2016-2017, Alden Torres
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_I2P_STREAM_HPP_INCLUDED
#define TORRENT_I2P_STREAM_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_USE_I2P

#include <list>
#include <string>
#include <vector>
#include <functional>
#include <cstring>

#include "libtorrent/proxy_base.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/aux_/noexcept_movable.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/hex.hpp" // for to_hex
#include "libtorrent/debug.hpp"
#include "libtorrent/aux_/biglybtc_private_i2p_owner.hpp"
#include <boost/asio/post.hpp>

namespace libtorrent {

namespace i2p_error {

	// error values for the i2p_category error_category.
	enum i2p_error_code
	{
		no_error = 0,
		parse_failed,
		cant_reach_peer,
		i2p_error,
		invalid_key,
		invalid_id,
		timeout,
		key_not_found,
		duplicated_id,
		num_errors
	};

	// hidden
	TORRENT_EXPORT boost::system::error_code make_error_code(i2p_error_code e);
}
}

namespace boost {
namespace system {

template<>
struct is_error_code_enum<libtorrent::i2p_error::i2p_error_code>
{ static const bool value = true; };

}
}


namespace libtorrent {

	// returns the error category for I2P errors
	TORRENT_EXPORT boost::system::error_category& i2p_category();

#if TORRENT_ABI_VERSION == 1
	TORRENT_DEPRECATED
	inline boost::system::error_category& get_i2p_category()
	{ return i2p_category(); }
#endif

struct i2p_session_options
{
	int m_inbound_quantity = 3;
	int m_outbound_quantity = 3;
	int m_inbound_length = 3;
	int m_outbound_length = 3;
	int m_inbound_length_variance = 0;
	int m_outbound_length_variance = 0;
};

struct i2p_stream : proxy_base
{
	explicit i2p_stream(io_context& io_context);
	i2p_stream(i2p_stream&&) noexcept = default;
	// explicitly disallow assignment, to silence msvc warning
	i2p_stream& operator=(i2p_stream const&) = delete;

	enum command_t : std::uint8_t
	{
		cmd_none,
		cmd_create_session,
		cmd_connect,
		cmd_accept,
		cmd_name_lookup,
		cmd_incoming
	};

	void set_command(command_t c) { m_command = c; }

	void set_session_options(i2p_session_options const& session_options)
	{
		m_session_options = session_options;
	}

	void set_session_id(char const* id) { m_id = id; }

	void set_local_i2p_endpoint(string_view d) { m_local = d.to_string(); }
	std::string const& local_i2p_endpoint() const { return m_local; }
	void set_destination(string_view d) { m_dest = d.to_string(); }
	std::string const& destination() const { return m_dest; }
		void set_owned_bridge(std::shared_ptr<aux::i2p_owner_context> value,
			aux::i2p_owner_purpose purpose,std::uint16_t remote=0,std::uint16_t local=0)
		{ m_owner=std::move(value);m_owner_purpose=purpose;m_owner_remote=remote;m_owner_local=local; }
		bool belongs_to_owner(std::shared_ptr<aux::i2p_owner_context> const& value)const noexcept{return m_owner&&m_owner==value&&m_owner->current();}
		template<class Buffers,class Handler> void async_read_some(Buffers const& bytes,Handler handler)
		{
			if(!m_owner){m_sock.async_read_some(bytes,std::move(handler));return;}
			if(!owner_alive()){boost::asio::post(m_sock.get_executor(),[h=std::move(handler)]()mutable{h(boost::asio::error::operation_aborted,0);});return;}
			auto owner=m_owner;m_sock.async_read_some(bytes,[owner,h=std::move(handler)](error_code ec,std::size_t n)mutable{
				if(owner&&!owner->current()){ec=boost::asio::error::operation_aborted;n=0;}h(ec,n);
			});
		}
		template<class Buffers,class Handler> void async_write_some(Buffers const& bytes,Handler handler)
		{
			if(!m_owner){m_sock.async_write_some(bytes,std::move(handler));return;}
			if(!owner_alive()){boost::asio::post(m_sock.get_executor(),[h=std::move(handler)]()mutable{h(boost::asio::error::operation_aborted,0);});return;}
			auto owner=m_owner;m_sock.async_write_some(bytes,[owner,h=std::move(handler)](error_code ec,std::size_t n)mutable{
				if(owner&&!owner->current()){ec=boost::asio::error::operation_aborted;n=0;}h(ec,n);
			});
		}
		template<class Buffers>std::size_t read_some(Buffers const& bytes,error_code& ec)
		{if(!owner_alive()){ec=boost::asio::error::operation_aborted;return 0;}return m_sock.read_some(bytes,ec);}
		template<class Buffers>std::size_t write_some(Buffers const& bytes,error_code& ec)
		{if(!owner_alive()){ec=boost::asio::error::operation_aborted;return 0;}return m_sock.write_some(bytes,ec);}
#ifndef BOOST_NO_EXCEPTIONS
		template<class Buffers>std::size_t read_some(Buffers const& bytes)
		{error_code ec;auto n=read_some(bytes,ec);if(ec)throw boost::system::system_error(ec);return n;}
		template<class Buffers>std::size_t write_some(Buffers const& bytes)
		{error_code ec;auto n=write_some(bytes,ec);if(ec)throw boost::system::system_error(ec);return n;}
#endif

	template <class Handler>
	void async_connect(endpoint_type const&, Handler h)
	{
			if(m_owner){
				if(!owner_alive()||m_hostname!="127.0.0.1"||m_port!=m_owner->policy.bridge_port){
					boost::asio::post(m_sock.get_executor(),[h=std::move(h)]()mutable{h(error_code(boost::asio::error::operation_aborted));});return;
				}
				// A numeric owned endpoint bypasses the OS resolver completely.
				ADD_OUTSTANDING_ASYNC("i2p_stream::connected");
				m_sock.async_connect(tcp::endpoint(address_v4::loopback(),m_owner->policy.bridge_port),
					wrap_allocator([this](error_code const& ec,Handler hn){connected(ec,std::move(hn));},std::move(h)));
				return;
			}
		// since we don't support regular endpoints, just ignore the one
		// provided and use m_dest.

		// the connect is split up in the following steps:
		// 1. resolve name of proxy server
		// 2. connect to SAM bridge
		// 4 send command message (CONNECT/ACCEPT)

		m_resolver.async_resolve(m_hostname, to_string(m_port).data(), wrap_allocator(
			[this](error_code const& ec, tcp::resolver::results_type ips, Handler hn) {
				do_connect(ec, std::move(ips), std::move(hn));
			}, std::move(h)));
	}

	std::string name_lookup() const { return m_name_lookup; }
	void set_name_lookup(char const* name) { m_name_lookup = name; }

	template <typename Handler>
	void send_name_lookup(Handler h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_name_lookup_response;
			if(m_owner){
				if(!owner_alive()||!owner_name(m_name_lookup)){h(error_code(i2p_error::invalid_key,i2p_category()));return;}
				const char* role=m_owner_purpose==aux::i2p_owner_purpose::Peer?"PEER":m_owner_purpose==aux::i2p_owner_purpose::Tracker?"TRACKER":"CONTROL";
				send_owned("NAMING LOOKUP NAME="+m_name_lookup+" PURPOSE="+role+" TO_PORT="+std::to_string(m_owner_remote)+" FROM_PORT="+std::to_string(m_owner_local)+"\n",std::move(h));return;
			}
		char cmd[1024];
		int size = std::snprintf(cmd, sizeof(cmd), "NAMING LOOKUP NAME=%s\n", m_name_lookup.c_str());
		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, std::size_t(size)), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				start_read_line(ec, std::move(hn));
			}, std::move(h)));
	}

private:
		bool owner_alive()const noexcept{return !m_owner||m_owner->current();}
		static bool owner_name(std::string const& value)noexcept{
			if(value=="ME")return true;
			return value.size()==60&&value.compare(52,8,".b32.i2p")==0
				&&std::all_of(value.begin(),value.begin()+52,[](char c){return (c>='a'&&c<='z')||(c>='2'&&c<='7');});
		}
		static bool owner_destination(std::string const& value)noexcept{
			return !value.empty()&&value.size()<=1024&&std::all_of(value.begin(),value.end(),[](char c){
				return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='~'||c=='=';
			});
		}
		template<typename Handler>void send_owned(std::string bytes,Handler h){
			if(!owner_alive()||bytes.size()>2048){h(error_code(boost::asio::error::operation_aborted));return;}
			auto command=std::make_shared<aux::i2p_owner_command>(std::move(bytes));
			ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
			async_write(m_sock,boost::asio::buffer(command->bytes),wrap_allocator(
				[this,command](error_code ec,std::size_t,Handler hn){
					if(!owner_alive())ec=boost::asio::error::operation_aborted;
					start_read_line(ec,std::move(hn));
				},std::move(h)));
		}

	template <typename Handler>
	void do_connect(error_code const& e, tcp::resolver::results_type ips, Handler h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		if (e || ips.empty())
		{
			h(e);
			error_code ec;
			close(ec);
			return;
		}

		auto i = ips.begin();
		ADD_OUTSTANDING_ASYNC("i2p_stream::connected");
		m_sock.async_connect(i->endpoint(), wrap_allocator(
			[this](error_code const& ec, Handler hn) {
				connected(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void connected(error_code const& e, Handler h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		COMPLETE_ASYNC("i2p_stream::connected");
		if (handle_error(e, h)) return;
			if(!owner_alive()){h(error_code(boost::asio::error::operation_aborted));return;}

		// send hello command
		m_state = read_hello_response;
			if(m_owner){
				send_owned("HELLO VERSION MIN=3.1 MAX=3.1 BIGLYBTC_OWNER="
					+aux::i2p_owner_context::hex(m_owner->policy.capability.data(),32)
					+" GENERATION="+std::to_string(m_owner->policy.generation)+"\n",std::move(h));return;
			}
		static const char cmd[] = "HELLO VERSION MIN=3.1 MAX=3.1\n";

		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, sizeof(cmd) - 1), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				start_read_line(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void start_read_line(error_code const& e, Handler h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		COMPLETE_ASYNC("i2p_stream::start_read_line");
		if (handle_error(e, h)) return;
			if(!owner_alive()){h(error_code(boost::asio::error::operation_aborted));return;}

		ADD_OUTSTANDING_ASYNC("i2p_stream::read_line");
		m_buffer.resize(1);
		async_read(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				read_line(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void read_line(error_code const& e, Handler h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		COMPLETE_ASYNC("i2p_stream::read_line");
		if (handle_error(e, h)) return;
			if(!owner_alive()){h(error_code(boost::asio::error::operation_aborted));return;}
			if(m_owner&&m_buffer.size()>2048){h(error_code(i2p_error::parse_failed,i2p_category()));return;}

		auto const read_pos = int(m_buffer.size());

		// look for \n which means end of the response
		if (m_buffer[read_pos - 1] != '\n')
		{
			ADD_OUTSTANDING_ASYNC("i2p_stream::read_line");
			// read another byte from the socket
			m_buffer.resize(read_pos + 1);
			async_read(m_sock, boost::asio::buffer(&m_buffer[read_pos], 1), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				read_line(ec, std::move(hn));
			}, std::move(h)));
			return;
		}
		m_buffer[read_pos - 1] = 0;

		if (m_command == cmd_incoming)
		{
			// this is the line containing the destination
			// of the incoming connection in an accept call
			m_dest = &m_buffer[0];
			h(e);
			std::vector<char>().swap(m_buffer);
			return;
		}

		error_code invalid_response(i2p_error::parse_failed
			, i2p_category());

		string_view expect1;
		string_view expect2;

		switch (m_state)
		{
			case read_hello_response:
				expect1 = "HELLO"_sv;
				expect2 = "REPLY"_sv;
				break;
			case read_connect_response:
			case read_accept_response:
				expect1 = "STREAM"_sv;
				expect2 = "STATUS"_sv;
				break;
			case read_session_create_response:
				expect1 = "SESSION"_sv;
				expect2 = "STATUS"_sv;
				break;
			case read_name_lookup_response:
				expect1 = "NAMING"_sv;
				expect2 = "REPLY"_sv;
				break;
		}

		TORRENT_ASSERT(m_buffer[int(m_buffer.size()) - 1] == '\0');
		string_view remaining(m_buffer.data(), m_buffer.size() - 1);
		string_view token;

		std::tie(token, remaining) = split_string(remaining, ' ');
		if (expect1.empty() || expect1 != token)
		{ handle_error(invalid_response, h); return; }

		std::tie(token, remaining) = split_string(remaining, ' ');
		if (expect2.empty() || expect2 != token)
		{ handle_error(invalid_response, h); return; }

		int result = 0;

		for(;;)
		{
			string_view name;
			std::tie(name, remaining) = split_string(remaining, '=');
			if (name.empty()) break;
			string_view value;
			if (remaining[0] == '"')
			{
				std::tie(value, remaining) = split_string(remaining.substr(1), '"');
				if (value.empty()) { handle_error(invalid_response, h); return; }
				value.remove_suffix(1);
			}
			else
			{
				std::tie(value, remaining) = split_string(remaining, ' ');
			}
			if (value.empty()) { handle_error(invalid_response, h); return; }

			if ("RESULT"_sv == name)
			{
				if ("OK"_sv == value)
					result = i2p_error::no_error;
				else if ("CANT_REACH_PEER"_sv == value)
					result = i2p_error::cant_reach_peer;
				else if ("I2P_ERROR"_sv == value)
					result = i2p_error::i2p_error;
				else if ("INVALID_KEY"_sv == value)
					result = i2p_error::invalid_key;
				else if ("INVALID_ID"_sv == value)
					result = i2p_error::invalid_id;
				else if ("TIMEOUT"_sv == value)
					result = i2p_error::timeout;
				else if ("KEY_NOT_FOUND"_sv == value)
					result = i2p_error::key_not_found;
				else if ("DUPLICATED_ID"_sv == value)
					result = i2p_error::duplicated_id;
				else
					result = i2p_error::num_errors; // unknown error
			}
			/*else if ("MESSAGE" == name)
			{
			}
			else if ("VERSION"_sv == name)
			{
			}*/
			else if ("VALUE"_sv == name)
			{
				m_name_lookup = value.to_string();
			}
			else if ("DESTINATION"_sv == name)
			{
				m_dest = value.to_string();
			}
		}

		error_code ec(result, i2p_category());
		if (ec)
		{
			std::forward<Handler>(h)(ec);
			return;
		}

		switch (m_state)
		{
		case read_hello_response:
			switch (m_command)
			{
				case cmd_create_session:
					send_session_create(std::move(h));
					break;
				case cmd_accept:
					send_accept(std::move(h));
					break;
				case cmd_connect:
					send_connect(std::move(h));
					break;
				case cmd_none:
				case cmd_name_lookup:
				case cmd_incoming:
					std::forward<Handler>(h)(ec);
					std::vector<char>().swap(m_buffer);
			}
			break;
		case read_connect_response:
		case read_session_create_response:
		case read_name_lookup_response:
			std::forward<Handler>(h)(ec);
			std::vector<char>().swap(m_buffer);
			break;
		case read_accept_response:
			// the SAM bridge is waiting for an incoming
			// connection.
			// wait for one more line containing
			// the destination of the remote peer
			m_command = cmd_incoming;
			m_buffer.resize(1);
			ADD_OUTSTANDING_ASYNC("i2p_stream::read_line");
			async_read(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
				[this](error_code const& err, std::size_t, Handler hn) {
					read_line(err, std::move(hn));
				}, std::move(h)));
			break;
		}
	}

	template <typename Handler>
	void send_connect(Handler h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_connect_response;
			if(m_owner){
				if(!owner_alive()||!owner_destination(m_dest)){h(error_code(i2p_error::invalid_key,i2p_category()));return;}
				const char* role=m_owner_purpose==aux::i2p_owner_purpose::Tracker?"TRACKER":"PEER";
				send_owned("STREAM CONNECT ID="+std::string(m_id?m_id:"")+" DESTINATION="+m_dest
					+" PURPOSE="+role+" TO_PORT="+std::to_string(m_owner_remote)
					+" FROM_PORT="+std::to_string(m_owner_local)+"\n",std::move(h));return;
			}
		char cmd[1024];
		int size = std::snprintf(cmd, sizeof(cmd), "STREAM CONNECT ID=%s DESTINATION=%s\n"
			, m_id, m_dest.c_str());
		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, std::size_t(size)), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				start_read_line(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void send_accept(Handler h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_accept_response;
			if(m_owner){send_owned("STREAM ACCEPT ID="+std::string(m_id?m_id:"")
				+" PURPOSE=PEER TO_PORT="+std::to_string(m_owner->policy.peer_port)
				+" FROM_PORT="+std::to_string(m_owner->policy.local_peer_port)+"\n",std::move(h));return;}
		char cmd[400];
		int size = std::snprintf(cmd, sizeof(cmd), "STREAM ACCEPT ID=%s\n", m_id);
		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, std::size_t(size)), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				start_read_line(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void send_session_create(Handler h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_session_create_response;
			if(m_owner){
				send_owned("SESSION CREATE STYLE=STREAM ID="+std::string(m_id?m_id:"")
					+" DESTINATION=BIGLYBTC_OWNED PUBLIC_HASH="
					+aux::i2p_owner_context::hex(m_owner->policy.public_destination_hash.data(),32)+"\n",std::move(h));return;
			}
		char cmd[400];
		int size = std::snprintf(cmd, sizeof(cmd),
			"SESSION CREATE STYLE=STREAM ID=%s "
			"DESTINATION=TRANSIENT SIGNATURE_TYPE=7 i2cp.leaseSetEncType=4,0 "
			"inbound.quantity=%d outbound.quantity=%d inbound.length=%d outbound.length=%d "
			"inbound.lengthVariance=%d outbound.lengthVariance=%d\n",
			m_id, m_session_options.m_inbound_quantity, m_session_options.m_outbound_quantity,
			m_session_options.m_inbound_length, m_session_options.m_outbound_length,
			m_session_options.m_inbound_length_variance, m_session_options.m_outbound_length_variance);
		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, std::size_t(size)), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				start_read_line(ec, std::move(hn));
			}, std::move(h)));
	}

	// send and receive buffer
	aux::noexcept_movable<aux::vector<char>> m_buffer;
	char const* m_id = nullptr;
	std::string m_dest;
	std::string m_local;
	std::string m_name_lookup;

	i2p_session_options m_session_options;

	enum state_t : std::uint8_t
	{
		read_hello_response,
		read_connect_response,
		read_accept_response,
		read_session_create_response,
		read_name_lookup_response
	};

	command_t m_command;
	state_t m_state;
		std::shared_ptr<aux::i2p_owner_context> m_owner;
		aux::i2p_owner_purpose m_owner_purpose=aux::i2p_owner_purpose::Peer;
		std::uint16_t m_owner_remote=0,m_owner_local=0;
};

class i2p_connection
{
public:
	explicit i2p_connection(io_context& ios);
	~i2p_connection();
	// explicitly disallow assignment, to silence msvc warning
	i2p_connection& operator=(i2p_connection const&) = delete;

	aux::proxy_settings proxy() const;
	void set_owned_bridge(std::shared_ptr<aux::i2p_owner_context> value){m_owner=std::move(value);}
	std::shared_ptr<aux::i2p_owner_context> const& owned_bridge()const noexcept{return m_owner;}

	bool is_open() const
	{
		return m_sam_socket
			&& m_sam_socket->is_open()
			&& m_state != sam_connecting;
	}
	template <typename Handler>
	void open(std::string const& hostname, int port,
		i2p_session_options const& session_options, Handler handler)
	{
		// we already seem to have a session to this SAM router
		if (m_hostname == hostname
			&& m_port == port
			&& m_sam_socket
			&& (is_open() || m_state == sam_connecting)) return;

		m_hostname = hostname;
		m_port = port;

		if (m_hostname.empty()) return;

		m_state = sam_connecting;

		char tmp[20];
		aux::random_bytes(tmp);
		m_session_id.resize(sizeof(tmp)*2);
		aux::to_hex(tmp, &m_session_id[0]);

		m_sam_socket = std::make_shared<i2p_stream>(m_io_service);
		m_sam_socket->set_proxy(m_hostname, m_port);
		m_sam_socket->set_command(i2p_stream::cmd_create_session);
		m_sam_socket->set_session_id(m_session_id.c_str());
		m_sam_socket->set_session_options(session_options);
		m_sam_socket->set_owned_bridge(m_owner,aux::i2p_owner_purpose::Control);

		ADD_OUTSTANDING_ASYNC("i2p_stream::on_sam_connect");
		m_sam_socket->async_connect(tcp::endpoint(), wrap_allocator(
			[this,s=m_sam_socket](error_code const& ec, Handler hn) {
				on_sam_connect(ec, s, std::move(hn));
			}, std::move(handler)));
	}
	void close(error_code&);

	// TODO: make this a string_view
	char const* session_id() const { return m_session_id.c_str(); }
	std::string const& local_endpoint() const { return m_i2p_local_endpoint; }

	template <typename Handler>
	void async_name_lookup(char const* name, Handler handler,
		aux::i2p_owner_purpose purpose=aux::i2p_owner_purpose::Control,
		std::uint16_t remote=0,std::uint16_t local=0)
	{
		if(m_owner&&(!m_owner->current()||!name||std::strlen(name)>60||m_name_lookup.size()>=16)){
			post(m_io_service,[h=std::move(handler)]()mutable{h(error_code(boost::asio::error::operation_aborted),nullptr);});return;
		}
		if (m_state == sam_idle && m_name_lookup.empty() && is_open())
			do_name_lookup(name,std::move(handler),purpose,remote,local);
		else
			m_name_lookup.push_back(Lookup{std::string(name),std::move(handler),purpose,remote,local});
	}

private:

	template <typename Handler>
	void on_sam_connect(error_code const& ec, std::shared_ptr<i2p_stream>, Handler h)
	{
		COMPLETE_ASYNC("i2p_stream::on_sam_connect");
		m_state = sam_idle;

		if (ec)
		{
			h(ec);
			return;
		}

		do_name_lookup("ME", wrap_allocator(
			[this](error_code const& e, char const* dst, Handler hn) {
				set_local_endpoint(e, dst, std::move(hn));
			}, std::move(h)));
	}

	using name_lookup_handler = std::function<void(error_code const&, char const*)>;

	template <typename Handler>
	void do_name_lookup(std::string const& name, Handler handler,aux::i2p_owner_purpose purpose=aux::i2p_owner_purpose::Control,std::uint16_t remote=0,std::uint16_t local=0)
	{
		TORRENT_ASSERT(m_state == sam_idle);
		m_state = sam_name_lookup;
		m_sam_socket->set_owned_bridge(m_owner,purpose,remote,local);
		m_sam_socket->set_name_lookup(name.c_str());
		m_sam_socket->send_name_lookup(wrap_allocator(
			[this,s=m_sam_socket](error_code const& ec, Handler hn) {
				on_name_lookup(ec, s, std::move(hn));
			}, std::move(handler)));
	}

	template <typename Handler>
	void on_name_lookup(error_code const& ec, std::shared_ptr<i2p_stream>, Handler handler)
	{
		m_state = sam_idle;

		std::string name = m_sam_socket->name_lookup();
		if (!m_name_lookup.empty())
		{
			auto& nl=m_name_lookup.front();
			do_name_lookup(nl.name,std::move(nl.handler),nl.purpose,nl.remote,nl.local);
			m_name_lookup.pop_front();
		}

		if (ec)
		{
			handler(ec, nullptr);
			return;
		}

		handler(ec, name.c_str());
	}


	template <typename Handler>
	void set_local_endpoint(error_code const& ec, char const* dest, Handler h)
	{
		if (!ec && dest != nullptr)
			m_i2p_local_endpoint = dest;
		else
			m_i2p_local_endpoint.clear();

		h(ec);
	}

	// to talk to i2p SAM bridge
	std::shared_ptr<i2p_stream> m_sam_socket;
	std::shared_ptr<aux::i2p_owner_context> m_owner;
	std::string m_hostname;
	int m_port;

	// our i2p endpoint key
	std::string m_i2p_local_endpoint;
	std::string m_session_id;

	struct Lookup {std::string name;name_lookup_handler handler;aux::i2p_owner_purpose purpose;std::uint16_t remote=0,local=0;};
	std::list<Lookup> m_name_lookup;

	enum state_t
	{
		sam_connecting,
		sam_name_lookup,
		sam_idle
	};

	state_t m_state;

	io_context& m_io_service;
};

}

#endif // TORRENT_USE_I2P

#endif
