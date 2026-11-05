// SPDX-License-Identifier: GPL-2.0-or-later
#include "unique32.hpp"
#include "detail/aes_rounds.hpp"
#include "detail/swap_or_not.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <array>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace biglybtc::services::protocol_ids {
namespace {
struct ContextDeleter {void operator()(EVP_CIPHER_CTX* p)const noexcept{EVP_CIPHER_CTX_free(p);}};
using Context=std::unique_ptr<EVP_CIPHER_CTX,ContextDeleter>;
struct Secret {
  detail::AesRounds::Key bytes{};
  ~Secret(){OPENSSL_cleanse(bytes.data(),bytes.size());}
};
// Exact PRF input encoding, all unsigned and big endian:
// 0..3="U32S",4=version1,5=purpose(1=pivot,2=pair bit),6=width,
// 7=0,8..9=round,10..13=canonical pair maximum (zero for pivot),14..15=0.
// The two purpose domains cannot collide, including pair maximum==0.
std::array<unsigned char,16> input(unsigned purpose,unsigned width,unsigned round,
    std::uint32_t pair) noexcept {
  return {'U','3','2','S',1,static_cast<unsigned char>(purpose),
      static_cast<unsigned char>(width),0,
      static_cast<unsigned char>(round>>8),static_cast<unsigned char>(round),
      static_cast<unsigned char>(pair>>24),static_cast<unsigned char>(pair>>16),
      static_cast<unsigned char>(pair>>8),static_cast<unsigned char>(pair),0,0};
}
}
struct detail::AesRounds::Impl {
  Context ctx;
  const unsigned width;
  std::array<std::uint32_t,Rounds> pivots{};
  bool failed=false;
  explicit Impl(unsigned bits):ctx(EVP_CIPHER_CTX_new()),width(bits){}
  ~Impl(){OPENSSL_cleanse(pivots.data(),sizeof(pivots));}
  std::expected<std::uint32_t,Error> block(unsigned purpose,unsigned round,std::uint32_t pair) noexcept {
    if(failed){return std::unexpected(Error::Failed);}
    auto in=input(purpose,width,round,pair);
    // EVP streaming API permits input length+blocksize output capacity. Only
    // one exact AES block must be returned with ECB padding disabled.
    std::array<unsigned char,32> out{};int length=0;
    const bool ok=EVP_EncryptUpdate(ctx.get(),out.data(),&length,in.data(),static_cast<int>(in.size()))==1&&length==16;
    const auto value=(std::uint32_t(out[0])<<24)|(std::uint32_t(out[1])<<16)|
        (std::uint32_t(out[2])<<8)|std::uint32_t(out[3]);
    const auto bit=std::uint32_t(out[0]&1u);
    OPENSSL_cleanse(in.data(),in.size());OPENSSL_cleanse(out.data(),out.size());
    if(!ok){failed=true;return std::unexpected(Error::Crypto);}
    return purpose==1?value:bit;
  }
};
detail::AesRounds::AesRounds(std::unique_ptr<Impl> impl)noexcept:impl_(std::move(impl)){}
detail::AesRounds::~AesRounds()=default;
detail::AesRounds::AesRounds(AesRounds&&)noexcept=default;
detail::AesRounds& detail::AesRounds::operator=(AesRounds&&)noexcept=default;
std::expected<detail::AesRounds,Error> detail::AesRounds::create(const Key& key,unsigned width) noexcept {
  if(width==0||width>32){return std::unexpected(Error::Failed);}
  try{
    auto impl=std::make_unique<Impl>(width);
    if(!impl->ctx){return std::unexpected(Error::Resource);}
    if(EVP_EncryptInit_ex2(impl->ctx.get(),EVP_aes_256_ecb(),key.data(),nullptr,nullptr)!=1||
        EVP_CIPHER_CTX_set_padding(impl->ctx.get(),0)!=1){return std::unexpected(Error::Crypto);}
    for(unsigned round=0;round<Rounds;++round){
      const auto pivot=impl->block(1,round,0);if(!pivot){return std::unexpected(pivot.error());}
      impl->pivots[round]=*pivot&mask(width);
    }
    return AesRounds(std::move(impl));
  }catch(const std::bad_alloc&){return std::unexpected(Error::Resource);}
}
std::expected<std::uint32_t,Error> detail::AesRounds::apply(std::uint32_t value,bool inverse) noexcept {
  if(!impl_){return std::unexpected(Error::Closed);}
  return detail::transform(value,impl_->width,impl_->pivots,
      [this](std::size_t round,std::uint32_t pair)->std::expected<bool,Error>{
        auto bit=impl_->block(2,static_cast<unsigned>(round),pair);
        if(!bit){return std::unexpected(bit.error());}return *bit!=0;
      },inverse);
}

struct Unique32::Impl {
  const std::thread::id owner=std::this_thread::get_id();
  const Zero zero;
  detail::Sequence sequence;
  std::optional<detail::AesRounds> rounds;
  explicit Impl(Zero selected):zero(selected){}
  std::expected<std::uint32_t,Error> evaluate(std::uint32_t value) noexcept {
    if(!rounds){
      Secret key;
      if(RAND_priv_bytes(key.bytes.data(),static_cast<int>(key.bytes.size()))!=1){return std::unexpected(Error::Crypto);}
      auto prepared=detail::AesRounds::create(key.bytes);if(!prepared){return std::unexpected(prepared.error());}
      rounds.emplace(std::move(*prepared));
    }
    return rounds->apply(value);
  }
};
Unique32::Unique32(std::unique_ptr<Impl> impl)noexcept:impl_(std::move(impl)){}
Unique32::~Unique32()=default;
Unique32::Unique32(Unique32&&)noexcept=default;
Unique32& Unique32::operator=(Unique32&&)noexcept=default;
std::expected<Unique32,Error> Unique32::create(Zero zero) noexcept {
  if(zero!=Zero::Allow&&zero!=Zero::Skip){return std::unexpected(Error::Failed);}
  try{return Unique32(std::make_unique<Impl>(zero));}
  catch(const std::bad_alloc&){return std::unexpected(Error::Resource);}
}
std::expected<std::uint32_t,Error> Unique32::next() noexcept {
  if(!impl_){return std::unexpected(Error::Closed);}
  if(impl_->owner!=std::this_thread::get_id()){return std::unexpected(Error::Thread);}
  return impl_->sequence.next(impl_->zero,[this](std::uint32_t value){return impl_->evaluate(value);});
}
std::expected<Statistics,Error> Unique32::statistics() const noexcept {
  if(!impl_){return std::unexpected(Error::Closed);}
  if(impl_->owner!=std::this_thread::get_id()){return std::unexpected(Error::Thread);}
  return Statistics{impl_->sequence.consumed(),impl_->rounds.has_value(),impl_->sequence.failed()};
}
} // namespace biglybtc::services::protocol_ids
