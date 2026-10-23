// SPDX-License-Identifier: GPL-2.0-or-later
#include "authority.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <bit>
#include <list>
#include <thread>
#include <unordered_map>
namespace biglybtc::services::azureus_auth {
namespace {
template<class T> class Cache {
  struct Entry { std::string key;T value; };
  std::list<Entry> entries_;
  std::unordered_map<std::string,typename std::list<Entry>::iterator> index_;
public:
  std::optional<T> get(const std::string& key) {
    const auto found=index_.find(key);if (found==index_.end()) { return {}; }
    entries_.splice(entries_.begin(),entries_,found->second);return found->second->value;
  }
  void put(std::string key,T value) {
    entries_.push_front({std::move(key),value});
    try { index_.emplace(entries_.front().key,entries_.begin()); }catch (...) { entries_.pop_front();throw; }
    if (entries_.size()>256) { index_.erase(entries_.back().key);entries_.pop_back(); }
  }
  std::size_t size() const { return entries_.size(); }
};
struct Secret { Key bytes{};~Secret() { OPENSSL_cleanse(bytes.data(),bytes.size()); } };
}
struct Authority::Impl {
  const std::thread::id owner=std::this_thread::get_id();
  bool enabled;Secret key;
  Cache<std::int32_t> ips;Cache<Token2> ids;std::uint64_t digests=0;
  explicit Impl(const std::optional<Key>& supplied):enabled(supplied.has_value()) { if (supplied) { key.bytes=*supplied; } }
  bool correctThread() const { return std::this_thread::get_id()==owner; }
  std::expected<Key,Error> digest(std::span<const std::uint8_t> bytes) {
    Secret input;input.bytes=key.bytes;
    for (std::size_t i=0;i<std::min(bytes.size(),input.bytes.size());++i) { input.bytes[i]^=bytes[i]; }
    Secret output;std::size_t length=0;
    if (EVP_Q_digest(nullptr,"MD5",nullptr,input.bytes.data(),input.bytes.size(),output.bytes.data(),&length)!=1 || length!=16) { return std::unexpected(Error::Crypto); }
    ++digests;return output.bytes;
  }
};
Authority::Authority(const std::optional<Key>& key):impl_(std::make_unique<Impl>(key)) {}
Authority::~Authority()=default;
std::expected<std::unique_ptr<Authority>,Error> Authority::create(bool supportsStorage) {
  if (!supportsStorage) { return std::make_unique<Authority>(std::nullopt); }
  Secret key;if (RAND_priv_bytes(key.bytes.data(),static_cast<int>(key.bytes.size()))!=1) { return std::unexpected(Error::Crypto); }
  auto result=std::make_unique<Authority>(std::nullopt);result->impl_->enabled=true;result->impl_->key.bytes=key.bytes;return result;
}
std::expected<Issued<std::int32_t>,Error> Authority::issueIp(const std::optional<Address>& address) {
  if (!impl_->correctThread()) { return std::unexpected(Error::Thread); }
  if (!impl_->enabled) { return Issued<std::int32_t>{0,false}; }
  if (!address || (address->length!=4 && address->length!=16)) { return std::unexpected(Error::Input); }
  const auto bytes=std::span(address->bytes).first(address->length);std::string key(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  if (const auto existing=impl_->ips.get(key)) { return Issued<std::int32_t>{*existing,true}; }
  auto digest=impl_->digest(bytes);if (!digest) { return std::unexpected(digest.error()); }
  std::uint32_t bits=0;for (std::size_t i=0;i<4;++i) { bits=(bits<<8)|(*digest)[i]; }
  OPENSSL_cleanse(digest->data(),digest->size());const auto value=std::bit_cast<std::int32_t>(bits);
  impl_->ips.put(std::move(key),value);return Issued<std::int32_t>{value,false};
}
std::expected<Issued<Token2>,Error> Authority::issueId(std::span<const std::uint8_t> bytes) {
  if (!impl_->correctThread()) { return std::unexpected(Error::Thread); }
  if (!impl_->enabled) { return Issued<Token2>{Token2{},false}; }
  if (bytes.size()>8192) { return std::unexpected(Error::Limit); }
  std::string key;if (!bytes.empty()) { key.assign(reinterpret_cast<const char*>(bytes.data()),bytes.size()); }
  if (const auto existing=impl_->ids.get(key)) { return Issued<Token2>{*existing,true}; }
  auto digest=impl_->digest(bytes);if (!digest) { return std::unexpected(digest.error()); }
  Token2 token{};std::copy_n(digest->begin(),token.size(),token.begin());OPENSSL_cleanse(digest->data(),digest->size());impl_->ids.put(std::move(key),token);return Issued<Token2>{token,false};
}
std::expected<bool,Error> Authority::verifyIp(const std::optional<Address>& address,std::int32_t token) {
  const auto expected=issueIp(address);if (!expected) { return std::unexpected(expected.error()); }return expected->value==token;
}
std::expected<bool,Error> Authority::verifyId(std::span<const std::uint8_t> id,std::optional<std::span<const std::uint8_t>> token) {
  const auto expected=issueId(id);if (!expected) { return std::unexpected(expected.error()); }
  return token && token->size()==expected->value.size() && CRYPTO_memcmp(token->data(),expected->value.data(),expected->value.size())==0;
}
std::expected<Statistics,Error> Authority::statistics() const {
  if (!impl_->correctThread()) { return std::unexpected(Error::Thread); }
  return Statistics{impl_->ips.size(),impl_->ids.size(),impl_->digests};
}
}
