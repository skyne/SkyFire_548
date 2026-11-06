/*
* This file is part of the Pandaria 5.4.8 Project. See THANKS file for Copyright information
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*
* SkyFire adaptation: catalog and purchase flow retargeted onto SkyFire packets, SQL, and the existing character boost.
*/

#ifndef SKYFIRE_BATTLE_PAY_MGR_H
#define SKYFIRE_BATTLE_PAY_MGR_H

#include "Common.h"
#include "Platform/Singleton.h"
#include <string>
#include <unordered_map>
#include <vector>

class WorldSession;

#define MAX_BATTLE_PAY_PRODUCT_TITLE_SIZE 55
#define MAX_BATTLE_PAY_PRODUCT_DESCRIPTION_SIZE 500
#define MAX_BATTLE_PAY_GROUP_NAME_SIZE 32
#define BATTLE_PAY_CURRENCY_PRECISION 10000

enum BattlePayCurrency
{
    BATTLE_PAY_CURRENCY_UNKNOWN = 0,
    BATTLE_PAY_CURRENCY_USD     = 1,
    BATTLE_PAY_CURRENCY_GBP     = 2,
    BATTLE_PAY_CURRENCY_KRW     = 3,
    BATTLE_PAY_CURRENCY_EUR     = 4,
    BATTLE_PAY_CURRENCY_RUB     = 5,
    BATTLE_PAY_CURRENCY_ARS     = 8,
    BATTLE_PAY_CURRENCY_CLP     = 9,
    BATTLE_PAY_CURRENCY_MXN     = 10,
    BATTLE_PAY_CURRENCY_BRL     = 11,
    BATTLE_PAY_CURRENCY_AUD     = 12,
    BATTLE_PAY_CURRENCY_CPT     = 14,
    BATTLE_PAY_CURRENCY_TPT     = 15,
    BATTLE_PAY_CURRENCY_BETA    = 16
};

enum BattlePayGroupType
{
    BATTLE_PAY_GROUP_TYPE_GRID   = 0,
    BATTLE_PAY_GROUP_TYPE_SINGLE = 1,
    BATTLE_PAY_GROUP_TYPE_END
};

enum BattlePayBannerType
{
    BATTLE_PAY_BANNER_TYPE_FEATURED = 0,
    BATTLE_PAY_BANNER_TYPE_DISCOUNT = 1,
    BATTLE_PAY_BANNER_TYPE_NEW      = 2,
    BATTLE_PAY_BANNER_TYPE_CUSTOM   = 3,
    BATTLE_PAY_BANNER_TYPE_END
};

enum BattlePayProductType
{
    BATTLE_PAY_PRODUCT_TYPE_ITEM    = 0,
    BATTLE_PAY_PRODUCT_TYPE_SERVICE = 2
};

enum BattlePayProductFlags
{
    BATTLE_PAY_PRODUCT_FLAG_SHOW_IN_CHARACTERS_SCREEN = 0x001,
    BATTLE_PAY_PRODUCT_FLAG_UNK2                      = 0x002,
    BATTLE_PAY_PRODUCT_FLAG_UNK3                      = 0x004,
    BATTLE_PAY_PRODUCT_FLAG_UNK4                      = 0x008,
    BATTLE_PAY_PRODUCT_FLAG_UNK6                      = 0x020,
    BATTLE_PAY_PRODUCT_FLAG_UNK7                      = 0x040,
    BATTLE_PAY_PRODUCT_FLAG_UNK8                      = 0x080,
    BATTLE_PAY_PRODUCT_FLAG_UNK9                      = 0x100,
    BATTLE_PAY_PRODUCT_FLAG_UNK10                     = 0x200,

    BATTLE_PAY_PRODUCT_FLAG_SEND_MAIL = BATTLE_PAY_PRODUCT_FLAG_SHOW_IN_CHARACTERS_SCREEN | BATTLE_PAY_PRODUCT_FLAG_UNK2 |
                                        BATTLE_PAY_PRODUCT_FLAG_UNK3 | BATTLE_PAY_PRODUCT_FLAG_UNK4 | BATTLE_PAY_PRODUCT_FLAG_UNK6,
    BATTLE_PAY_PRODUCT_DISABLES       = BATTLE_PAY_PRODUCT_FLAG_SHOW_IN_CHARACTERS_SCREEN | BATTLE_PAY_PRODUCT_FLAG_UNK3
};

enum BattlePayStatus
{
    BATTLE_PAY_STATUS_OK          = 0,
    BATTLE_PAY_STATUS_UNAVAILABLE = 1
};

enum BattlePayPurchaseStatus
{
    BATTLE_PAY_PURCHASE_STATUS_READY          = 2,
    BATTLE_PAY_PURCHASE_STATUS_ALLOWED_TO_BUY = 3,
    BATTLE_PAY_PURCHASE_STATUS_BUYING         = 5,
    BATTLE_PAY_PURCHASE_STATUS_BUYED          = 6
};

enum BattlePayResultCode
{
    BATTLE_PAY_RESULT_OK          = 0,
    BATTLE_PAY_RESULT_SHOP_ERROR  = 3,
    BATTLE_PAY_RESULT_DECLINED    = 4,
    BATTLE_PAY_RESULT_UNAVAILABLE = 25
};

enum BattlePayServices
{
    BATTLE_PAY_SERVICE_BOOST = 83
};

struct BattlePayProduct
{
    uint32 Id;
    std::string Title;
    std::string Description;
    uint32 Icon;
    uint64 Price;
    uint32 Discount;
    uint32 DisplayId;
    uint8 Type;
    uint8 ChoiceType;
    uint32 Flags;
    uint32 FlagsInfo;
};

struct BattlePayProductLocale
{
    std::vector<std::string> Title;
    std::vector<std::string> Description;
};

struct BattlePayProductItem
{
    uint32 Id;
    uint32 ItemId;
    uint32 Count;
};

struct BattlePayGroup
{
    uint32 Id;
    uint32 Index;
    std::string Name;
    uint32 Icon;
    uint8 Type;
};

struct BattlePayGroupLocale
{
    std::vector<std::string> Name;
};

struct BattlePayShopEntry
{
    uint32 Id;
    uint32 ProductId;
    uint32 GroupId;
    int32 Index;
    std::string Title;
    std::string Description;
    uint32 Icon;
    uint32 DisplayId;
    uint8 Banner;
    uint32 Flags;
};

struct BattlePayShopEntryLocale
{
    std::vector<std::string> Title;
    std::vector<std::string> Description;
};

typedef std::vector<BattlePayProductItem> BattlePayProductItemsVector;

class BattlePayMgr
{
    friend class Skyfire::Singleton<BattlePayMgr, Skyfire::NullMutex>;

    BattlePayMgr() : m_nextPurchaseId(1) { }
    ~BattlePayMgr() { }

    uint64 m_nextPurchaseId;

public:
    void LoadFromDb();
    bool IsStoreEnabled() const;
    uint32 GetStoreCurrency() const;
    uint64 NextPurchaseId();

    void SendProductList(WorldSession* session);
    void SendPurchaseList(WorldSession* session);
    void SendPointsBalance(WorldSession* session);
    void AdvancePurchase(WorldSession* session);

    BattlePayProduct const* GetProduct(uint32 productId) const;
    uint64 GetCurrentPrice(BattlePayProduct const* product) const;

private:
    std::vector<BattlePayProduct> m_products;
    std::unordered_map<uint32, BattlePayProductItemsVector> m_productItems;
    std::vector<BattlePayGroup> m_groups;
    std::vector<BattlePayShopEntry> m_entries;
    std::unordered_map<uint32, BattlePayProductLocale> m_productLocales;
    std::unordered_map<uint32, BattlePayGroupLocale> m_groupLocales;
    std::unordered_map<uint32, BattlePayShopEntryLocale> m_entryLocales;

    void LoadProductsFromDb();
    void LoadProductLocalesFromDb();
    void LoadProductItemsFromDb();
    void LoadGroupsFromDb();
    void LoadGroupLocalesFromDb();
    void LoadEntriesFromDb();
    void LoadEntryLocalesFromDb();

    BattlePayProductItemsVector const& GetItems(uint32 productId) const;
    bool HasGroupId(uint32 groupId) const;
    bool HasEntryId(uint32 entryId) const;
    uint64 GetPointsBalance(WorldSession* session) const;
    bool TakePoints(WorldSession* session, uint64 points) const;
    void GivePoints(WorldSession* session, uint64 points) const;
    bool OwnsCharacter(WorldSession* session, uint32 characterGuid) const;
    bool Deliver(WorldSession* session, BattlePayProduct const* product, uint32 characterGuid) const;
    void RegisterPurchase(WorldSession* session, uint32 characterGuid, uint32 productId, uint32 itemId, uint64 price) const;
    void SendPurchaseUpdate(WorldSession* session) const;
    void SendUnavailableProductList(WorldSession* session) const;
};

#define sBattlePayMgr Skyfire::Singleton<BattlePayMgr, Skyfire::NullMutex>::instance()

#endif
