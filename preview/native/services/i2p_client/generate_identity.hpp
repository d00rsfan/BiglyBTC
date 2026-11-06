// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "identity.hpp"
namespace biglybtc::services::i2p {
// Explicit value-only key generation. No file, session, transport or profile
// permission. Original online signing7/Enc0 stream; returned secrets are owned
// and cleared by ClientIdentity. OpenSSL supplies all cryptography/randomness.
Result<ClientIdentity> generateClientIdentity();
}
