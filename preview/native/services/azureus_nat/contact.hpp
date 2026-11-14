// SPDX-License-Identifier: GPL-2.0-or-later
// Original external/transport role split: Copyright (C) Azureus Software, Inc.
#pragma once
#include "wire.hpp"
namespace biglybtc::services::azureus_nat {
class RoutedContact final {
public:
 static Result<RoutedContact> observed(nodes::Contact announced,udp::Address actual,nodes::NodeCache&);
 const nodes::Contact& external()const noexcept{return external_;}
 const udp::Address& transport()const noexcept{return transport_;}
 Result<RoutedContact> withTransport(udp::Address actual)const;
 nodes::Snapshot exportSnapshot()const{return {true,external_.protocol,external_.address};}
private:
 RoutedContact(nodes::Contact external,udp::Address actual):external_(std::move(external)),transport_(std::move(actual)){}
 nodes::Contact external_;udp::Address transport_;
};
}
