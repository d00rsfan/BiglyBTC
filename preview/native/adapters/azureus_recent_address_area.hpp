// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "azureus_key_block_area.hpp"
namespace biglybtc::storage {
class AzureusRecentAddressArea final {
public:
 const std::optional<std::string>& primary()const noexcept{return captured_.primary();}
 const std::optional<std::string>& pending()const noexcept{return captured_.pending();}
private:
 explicit AzureusRecentAddressArea(AzureusKeyBlockArea captured):captured_(std::move(captured)){}
 AzureusKeyBlockArea captured_;
 friend std::expected<AzureusRecentAddressArea,Error> readAzureusRecentAddressArea(const AzureusValueVersions&);
 friend std::expected<AzureusRecentAddressArea,Error> saveAzureusRecentAddressArea(const AzureusValueVersions&,const AzureusRecentAddressArea&,std::string);
};
// Separate fixed-file role; cannot be substituted for a block-area receipt.
std::expected<AzureusRecentAddressArea,Error> readAzureusRecentAddressArea(const AzureusValueVersions&);
std::expected<AzureusRecentAddressArea,Error> saveAzureusRecentAddressArea(const AzureusValueVersions&,const AzureusRecentAddressArea&,std::string encodedMap);
}
