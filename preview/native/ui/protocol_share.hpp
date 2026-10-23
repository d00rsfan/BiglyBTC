#pragma once
#include <cstdint>
#include <limits>
#include <optional>
namespace biglybtc::ui {
// Source formula: protocol*100/(payload+protocol), known zero denominator1.
// Native checked projection: exact rational tenths, round half to even. Avoids
// the original Java signed multiply/add wrap and intermediate float rounding.
// Unknown/negative inputs or an unrepresentable signed sum remain unknown.
inline std::optional<std::uint16_t> protocolShareTenths(std::optional<std::int64_t> payload,
    std::optional<std::int64_t> protocol)noexcept{
  if(!payload||!protocol||*payload<0||*protocol<0||*protocol>std::numeric_limits<std::int64_t>::max()-*payload){return {};}
  const auto sum=*payload+*protocol;const auto divisor=static_cast<std::uint64_t>(sum?sum:1);
  const auto numerator=static_cast<std::uint64_t>(*protocol);std::uint64_t quotient=0,remainder=0;
  // Binary long division of numerator*1000: remainder and numerator are each
  // <=INT64_MAX, so every unsigned double/add fits uint64 without extensions.
  for(unsigned bit=10;bit>0;--bit){quotient*=2;remainder*=2;if(remainder>=divisor){remainder-=divisor;++quotient;}
    if((1000U>>(bit-1))&1U){remainder+=numerator;if(remainder>=divisor){remainder-=divisor;++quotient;}}}
  const auto doubled=remainder*2;if(doubled>divisor||(doubled==divisor&&(quotient&1U))){++quotient;}
  return static_cast<std::uint16_t>(quotient);
}
} // namespace biglybtc::ui
