// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "session.hpp"
#include "../azureus_control/lookup.hpp"
#include <deque>
#include <biglybtc/azureus_diversification.hpp>
#include <set>
namespace biglybtc::services::azureus_nat {
enum class DirectoryError {Input,Limit,Source,Closed,Cancelled};
using DirectoryOutcome=std::expected<std::optional<RoutedContact>,DirectoryError>;
using DirectoryCallback=std::move_only_function<void(DirectoryOutcome)>;
struct DirectoryFacts {std::size_t active=0,cached=0;std::uint64_t reads=0,hits=0,publications=0,stores=0;bool closed=false;};
// One existing lane/source epoch. DHT values are untrusted relay hints; this
// class never grants DATA routes or private identity/payload authority.
class Directory final:public QObject {
public:
 using Remember=std::function<void(core::AzureusRemoteDiversification,std::function<void(bool)>)>;
 bool restoreDiversifications(std::span<const core::AzureusRemoteDiversification>,std::int64_t wallNow);
 static Result<std::unique_ptr<Directory>> open(transport::OwnedPingTransport&,
     nodes::Contact local,std::shared_ptr<azureus_db_runtime::Owner>,std::uint64_t,
     SessionHooks,bool owned,std::function<Milliseconds()> cacheClock={},Remember remember={},std::shared_ptr<core::AzureusDiversificationLedger> ledger={});
 ~Directory()override;
 Result<std::uint64_t> read(RoutedContact,QObject*,DirectoryCallback,std::span<const nodes::Contact> seeds={});
 // Caller reserves the durable original version under its actual IO broker.
 // Publication/removal can only address this lane's own local NAT key.
 bool publish(RoutedContact relay,std::int32_t reservedVersion,std::span<const nodes::Contact> seeds={});
 bool withdraw(std::int32_t reservedVersion,std::span<const nodes::Contact> seeds={});
 bool cancel(std::uint64_t);
 DirectoryFacts facts()const;
 void close();
private:
 using Clock=std::chrono::steady_clock;
 struct Waiter {std::uint64_t id=0;QPointer<QObject> receiver;DirectoryCallback callback;};
 struct Work {
  std::uint64_t id=0;nodes::Contact subject;nodes::NodeId key{};unsigned depth=0;
  std::set<nodes::NodeId> visited;std::deque<std::pair<nodes::NodeId,unsigned>> pendingKeys;
  Clock::time_point end,lookupEnd;Milliseconds completed=0;bool finished=false,lookupDone=false,publishing=false,persisting=false;
  std::optional<RoutedContact> result;std::optional<azureus_values::Value> publication;
  azureus_control::LookupController control;std::vector<Waiter> waiters;
  std::map<std::uint64_t,transport::Ticket> tickets;
  std::map<std::uint64_t,std::int32_t> tokens;
  std::vector<transport::Ticket> stores;
 };
 Directory(transport::OwnedPingTransport&,nodes::Contact,std::shared_ptr<azureus_db_runtime::Owner>,std::uint64_t,SessionHooks,bool,std::function<Milliseconds()>,Remember,std::shared_ptr<core::AzureusDiversificationLedger>);
 bool guard(Clock::time_point,std::move_only_function<void()>);
 bool valid(const nodes::Contact&)const;
 void begin(const std::shared_ptr<Work>&,std::vector<nodes::Contact>);
 void pump(const std::shared_ptr<Work>&);
 void valueReply(const std::shared_ptr<Work>&,std::uint64_t,transport::FindValueOutcome);
 void nodeReply(const std::shared_ptr<Work>&,std::uint64_t,transport::FindNodeOutcome);
 void publishStores(const std::shared_ptr<Work>&);
 void diversify(const std::shared_ptr<Work>&,std::int8_t);
 void nextKey(const std::shared_ptr<Work>&);
 bool publication(std::optional<RoutedContact>,std::int32_t,std::span<const nodes::Contact>);
 void finish(const std::shared_ptr<Work>&,std::optional<DirectoryError> error={});
 void shutdown(DirectoryError);
 void tick();
 Milliseconds cacheNow()const;
 static Milliseconds now();
 static nodes::NodeId key(const nodes::Contact&);
 QPointer<transport::OwnedPingTransport> lane_;nodes::Contact local_;
 std::shared_ptr<azureus_db_runtime::Owner> database_;std::uint64_t epoch_=0,serial_=0;
 SessionHooks hooks_;bool owned_=false,closed_=false;DirectoryError closedReason_=DirectoryError::Closed;std::function<Milliseconds()> cacheClock_;
 QTimer timer_;nodes::NodeCache cache_;std::map<std::string,std::shared_ptr<Work>> cacheEntries_;
 Remember remember_;
 std::vector<std::shared_ptr<Work>> works_;std::shared_ptr<Work> publication_;
 std::shared_ptr<core::AzureusDiversificationLedger> diversifications_;
 std::uint64_t reads_=0,hits_=0,publications_=0,stores_=0;
};
}
