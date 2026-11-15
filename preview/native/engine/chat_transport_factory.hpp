// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include <functional>
#include <chrono>
#include <optional>
#include <string>
namespace libtorrent {struct session_handle;}
namespace biglybtc::services::shared_udp {class Provider;}
namespace biglybtc::services::azureus_udp {enum class Failure;}
namespace biglybtc::app {class GeneralChats;}
namespace biglybtc::engine {
struct OwnedKeyBlockFixtureKey {std::string modulus,exponent;};
class PersistentSession;
class ChatTransportFactory final {
public:
 static std::expected<ChatTransportFactory,storage::Error> owned(
     const libtorrent::session_handle&,const storage::ProfileLease&,unsigned short port,bool standardNetworks=false,std::chrono::milliseconds ownedCachePeriod={},std::chrono::milliseconds ownedSurveyPeriod={},std::optional<OwnedKeyBlockFixtureKey> fixtureKey={});
private:
 using Attach=std::function<std::expected<std::shared_ptr<services::shared_udp::Provider>,services::azureus_udp::Failure>()>;
 ChatTransportFactory(std::shared_ptr<const storage::ProfileLease> lease,Attach attach,bool owned,bool standards=false,std::chrono::milliseconds cachePeriod={},std::chrono::milliseconds surveyPeriod={},std::optional<OwnedKeyBlockFixtureKey> fixtureKey={})
     :lease_(std::move(lease)),attach_(std::move(attach)),owned_(owned),standardNetworks_(standards),ownedCachePeriod_(cachePeriod),ownedSurveyPeriod_(surveyPeriod),ownedKeyBlockFixtureKey_(std::move(fixtureKey)){}
 std::shared_ptr<const storage::ProfileLease> lease_;Attach attach_;bool owned_=false,standardNetworks_=false;
 std::chrono::milliseconds ownedCachePeriod_{},ownedSurveyPeriod_{}; // Explicit owned-loopback fixture timing; never a saved/public override.
 std::optional<OwnedKeyBlockFixtureKey> ownedKeyBlockFixtureKey_; // Owned-loopback factory only; never a saved/public setting.
 friend class PersistentSession;friend class biglybtc::app::GeneralChats;
};
}
