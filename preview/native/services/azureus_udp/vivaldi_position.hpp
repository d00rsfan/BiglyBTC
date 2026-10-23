// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) Azureus Software, Inc.
// Source algorithm: Azureus Software, Inc. VivaldiPositionImpl/HeightCoordinatesImpl.
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace biglybtc::services::azureus_udp {
struct VivaldiCoordinates {
  float x=0,y=0,h=0;
  bool operator==(const VivaldiCoordinates&) const=default;
};
struct VivaldiValue {
  VivaldiCoordinates coordinates{};float error=10;
  bool operator==(const VivaldiValue&) const=default;
};
inline float vivaldiFloat(float value) noexcept {volatile float rounded=value;return rounded;}
inline bool validVivaldiCoordinates(VivaldiCoordinates value) noexcept {
  return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.h)
      &&std::abs(value.x)<=30000 && std::abs(value.y)<=30000 && std::abs(value.h)<=30000;
}
inline bool validVivaldiValue(VivaldiValue value) noexcept {return validVivaldiCoordinates(value.coordinates)&&std::isfinite(value.error);}
inline float vivaldiMeasure(VivaldiCoordinates value) noexcept {
  return vivaldiFloat(static_cast<float>(std::sqrt(static_cast<double>(vivaldiFloat(vivaldiFloat(value.x*value.x)+vivaldiFloat(value.y*value.y))))+static_cast<double>(value.h)));
}
inline VivaldiCoordinates vivaldiAdd(VivaldiCoordinates a,VivaldiCoordinates b) noexcept {return {vivaldiFloat(a.x+b.x),vivaldiFloat(a.y+b.y),std::abs(vivaldiFloat(a.h+b.h))};}
inline VivaldiCoordinates vivaldiSubtract(VivaldiCoordinates a,VivaldiCoordinates b) noexcept {return {vivaldiFloat(a.x-b.x),vivaldiFloat(a.y-b.y),std::abs(vivaldiFloat(a.h+b.h))};}
inline VivaldiCoordinates vivaldiScale(VivaldiCoordinates a,float scale) noexcept {return {vivaldiFloat(scale*a.x),vivaldiFloat(scale*a.y),vivaldiFloat(scale*a.h)};}
// Fixed-state V1 owner. RNG supplies float-narrowed [0,1] values, corresponding to original
// Math.random. Eight zero-vector retries bound the original recursive unity
// case; exhaustion rejects this optional observation without changing state.
class VivaldiPosition final {
public:
  const VivaldiValue& value()const noexcept{return value_;}
  std::uint64_t observations()const noexcept{return observations_;}
  std::uint64_t convergences()const noexcept{return convergences_;}
  template<class Random> bool observe(float rtt,VivaldiValue remote,Random&& random) noexcept {
    if(!std::isfinite(rtt)||rtt<=0||rtt>300000||!validVivaldiValue(remote)||vivaldiFloat(value_.error+remote.error)==0){return false;}
    auto next=*this;try{if(!next.update(rtt,remote,random)){return false;}}catch(...){return false;}
    if(next.observations_!=std::numeric_limits<std::uint64_t>::max()){++next.observations_;}*this=next;return true;
  }
  std::array<std::uint32_t,4> bits()const noexcept{
    return {std::bit_cast<std::uint32_t>(value_.coordinates.x),std::bit_cast<std::uint32_t>(value_.coordinates.y),std::bit_cast<std::uint32_t>(value_.coordinates.h),std::bit_cast<std::uint32_t>(value_.error)};
  }
private:
  template<class Random> bool update(float rtt,VivaldiValue remote,Random& random) {
    const auto draw=[&]()->std::optional<float>{const float value=random();return std::isfinite(value)&&value>=0&&value<=1?std::optional{value}:std::nullopt;};
    const float w=vivaldiFloat(value_.error/vivaldiFloat(remote.error+value_.error));
    const float realError=vivaldiFloat(rtt-vivaldiMeasure(vivaldiSubtract(value_.coordinates,remote.coordinates)));
    const float relativeError=vivaldiFloat(std::abs(realError)/rtt);
    const float newError=vivaldiFloat(vivaldiFloat(vivaldiFloat(relativeError*0.5f)*w)+vivaldiFloat(value_.error*vivaldiFloat(1-vivaldiFloat(0.5f*w))));
    const float scale=vivaldiFloat(vivaldiFloat(0.25f*w)*realError);
    auto a=draw(),b=draw(),c=draw();if(!a||!b||!c){return false;}
    const VivaldiCoordinates noise{vivaldiFloat(*a/10),vivaldiFloat(*b/10),vivaldiFloat(*c/10)};
    auto direction=vivaldiSubtract(value_.coordinates,vivaldiAdd(remote.coordinates,noise));
    float measure=vivaldiMeasure(direction);
    for(unsigned retry=0;measure==0 && retry<8;++retry){a=draw();b=draw();c=draw();if(!a||!b||!c){return false;}direction={*a,*b,*c};measure=vivaldiMeasure(direction);}
    if(measure==0){return false;}
    const auto updated=vivaldiAdd(value_.coordinates,vivaldiScale(vivaldiScale(direction,vivaldiFloat(1/measure)),scale));
    if(std::isfinite(newError)&&validVivaldiCoordinates(updated)){value_={updated,newError>0.1f?newError:0.1f};}
    else{value_={};}
    if(remote.coordinates.x!=0||remote.coordinates.y!=0){++convergeUpdates_;}
    if(convergeUpdates_>5){convergeUpdates_=0;if(convergences_!=std::numeric_limits<std::uint64_t>::max()){++convergences_;}if(!update(10,{{},50},random)){return false;}}
    return true;
  }
  VivaldiValue value_{};unsigned convergeUpdates_=0;
  std::uint64_t observations_=0,convergences_=0;
};
} // namespace biglybtc::services::azureus_udp
