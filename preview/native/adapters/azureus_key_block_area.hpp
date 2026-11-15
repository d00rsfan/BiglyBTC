// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "azureus_value_versions.hpp"
namespace biglybtc::storage {
class AzureusKeyBlockArea final {
public:
 const std::optional<std::string>& primary()const noexcept;
 const std::optional<std::string>& pending()const noexcept;
private:
 friend struct AzureusDhtAreaAccess;
 struct Data;explicit AzureusKeyBlockArea(std::shared_ptr<const Data> data):data_(std::move(data)){}
 std::shared_ptr<const Data> data_;
 friend std::expected<AzureusKeyBlockArea,Error> readAzureusKeyBlockArea(const AzureusValueVersions&);
 friend std::expected<AzureusKeyBlockArea,Error> saveAzureusKeyBlockArea(const AzureusValueVersions&,const AzureusKeyBlockArea&,std::string);
};
// Fixed original dht/[netN]/block.dat and block.saving, same acquired profile.
// A save compares this exact captured preimage and requires absent pending data.
std::expected<AzureusKeyBlockArea,Error> readAzureusKeyBlockArea(const AzureusValueVersions&);
std::expected<AzureusKeyBlockArea,Error> saveAzureusKeyBlockArea(const AzureusValueVersions&,const AzureusKeyBlockArea&,std::string encodedMap);
}
