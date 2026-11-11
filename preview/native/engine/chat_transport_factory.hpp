// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include <functional>
namespace libtorrent {struct session_handle;}
namespace biglybtc::services::shared_udp {class Provider;}
namespace biglybtc::services::azureus_udp {enum class Failure;}
namespace biglybtc::app {class GeneralChats;}
namespace biglybtc::engine {
class PersistentSession;
class ChatTransportFactory final {
public:
 static std::expected<ChatTransportFactory,storage::Error> owned(
     const libtorrent::session_handle&,const storage::ProfileLease&,unsigned short port,bool standardNetworks=false);
private:
 using Attach=std::function<std::expected<std::shared_ptr<services::shared_udp::Provider>,services::azureus_udp::Failure>()>;
 ChatTransportFactory(std::shared_ptr<const storage::ProfileLease> lease,Attach attach,bool owned,bool standards=false)
     :lease_(std::move(lease)),attach_(std::move(attach)),owned_(owned),standardNetworks_(standards){}
 std::shared_ptr<const storage::ProfileLease> lease_;Attach attach_;bool owned_=false,standardNetworks_=false;
 friend class PersistentSession;friend class biglybtc::app::GeneralChats;
};
}
