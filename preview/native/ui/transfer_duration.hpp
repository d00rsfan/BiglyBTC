// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QLocale>
#include <array>
#include <cmath>
namespace biglybtc::ui {
struct TransferDisplaySettings {
  bool siLabels=false,forceBinary=true,rateBits=false,stopAtMegabytes=false;
};
inline QString transferBytes(qint64 value,const TransferDisplaySettings& settings={},bool rate=false){
  const std::array<const char*,5> binaryBytes{"B","KiB","MiB","GiB","TiB"},decimalBytes{"B","kB","MB","GB","TB"};
  const std::array<const char*,5> binaryBits{"bit","Kibit","Mibit","Gibit","Tibit"},decimalBits{"bit","kbit","Mbit","Gbit","Tbit"};
  const auto& labels=rate&&settings.rateBits?(settings.siLabels?binaryBits:decimalBits):(settings.siLabels?binaryBytes:decimalBytes);
  const long double divisor=settings.forceBinary||settings.siLabels?1024:1000;
  long double amount=value;if(rate&&settings.rateBits){amount*=8;}
  const auto maximum=settings.stopAtMegabytes?2:4;int unit=0;
  while(std::fabs(amount)>=divisor&&unit<maximum){amount/=divisor;++unit;}
  constexpr std::array<int,5> precision{0,1,2,2,3};
  if(!rate){const auto scale=std::pow(10.0L,precision[static_cast<std::size_t>(unit)]);amount=std::trunc(amount*scale)/scale;}
  const auto text=QLocale().toString(static_cast<double>(amount),'f',precision[static_cast<std::size_t>(unit)]);
  return text+' '+QString::fromLatin1(labels[static_cast<std::size_t>(unit)])+(rate?QStringLiteral("/s"):QString{});
}
inline QString transferUptime(qint64 seconds) {
  if(seconds<0){return QObject::tr("%1 seconds").arg(seconds);}
  // Original TimeFormatter uses a duration year of365 days. Uptime sums
  // separate sessions; it has no calendar month or anniversary date.
  constexpr qint64 year=365LL*86400;
  const auto years=seconds/year;seconds%=year;
  const auto rest=QObject::tr("%1d %2h %3m %4s").arg(seconds/86400).arg((seconds%86400)/3600).arg((seconds%3600)/60).arg(seconds%60);
  return years?QObject::tr("%1y %2").arg(years).arg(rest):rest;
}
}
