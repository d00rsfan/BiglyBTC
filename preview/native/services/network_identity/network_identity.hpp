// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <QString>
#include <QHostAddress>
#include <array>
#include <memory>
#include <optional>
#include <vector>
namespace biglybtc::services::network_identity {
struct Asn {QString number,name,prefix;bool operator==(const Asn&)const=default;};
struct Input {std::array<QString,2> addresses;bool dnsAllowed=false;std::vector<Asn> saved;};
struct Family {QString address,status;std::optional<Asn> asn;bool cached=false;bool operator==(const Family&)const=default;};
struct Snapshot {std::array<Family,2> families;bool current=false;};
bool publicAddress(const QHostAddress&);
QString originQuery(const QHostAddress&);
std::optional<Asn> parseOrigin(const QHostAddress&,QString);
std::optional<QString> parseName(QString expectedNumber,QString);
bool matches(const Asn&,const QHostAddress&);
std::vector<Asn> saved(const core::Document*);
}
