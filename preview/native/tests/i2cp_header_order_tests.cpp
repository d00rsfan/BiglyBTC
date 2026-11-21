// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/msgsync/session.hpp"
#include "../services/i2p_provider/bridge.hpp"
#include "../services/i2p_dht/actor.hpp"
#include "../services/i2cp_leaseset/leaseset.hpp"
#include "../services/i2cp_channel/channel.hpp"
#include "../services/i2p_provider/owner.hpp"
#include <type_traits>
#include <iostream>
namespace i=biglybtc::services::i2p;
static_assert(std::is_same_v<i::i2cp::SessionOptions,std::vector<std::pair<std::string,std::string>>>);
static_assert(std::is_same_v<decltype(i::provider::OwnedTorrent::Options::session),i::i2cp::SessionOptions>);
void checkReverseI2cpHeaderOrder();
int main(){checkReverseI2cpHeaderOrder();std::cout<<"PASS_I2CP_CHAT_HEADER_ORDERS 2 translation units\n";}
