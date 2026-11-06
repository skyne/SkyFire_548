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
* SkyFire adaptation: SQL catalog and points balance, SkyFire packet bit-packing, and delivery through Player, mail, or the existing character boost.
*/

#include "BattlePayMgr.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
    BattlePayProductItemsVector const EmptyItems;

    void ApplyLocaleString(StringVector const& data, LocaleConstant locale, std::string& value)
    {
        if (locale == LOCALE_enUS)
            return;

        ObjectMgr::GetLocaleString(data, int32(locale), value);
    }
}

void BattlePayMgr::LoadFromDb()
{
    LoadProductsFromDb();
    LoadProductLocalesFromDb();
    LoadProductItemsFromDb();
    LoadGroupsFromDb();
    LoadGroupLocalesFromDb();
    LoadEntriesFromDb();
    LoadEntryLocalesFromDb();
}

bool BattlePayMgr::IsStoreEnabled() const
{
    return sWorld->GetBoolConfig(WorldBoolConfigs::CONFIG_BATTLE_PAY_STORE_ENABLED);
}

uint32 BattlePayMgr::GetStoreCurrency() const
{
    return sWorld->getIntConfig(WorldIntConfigs::CONFIG_BATTLE_PAY_CURRENCY);
}

uint64 BattlePayMgr::NextPurchaseId()
{
    return m_nextPurchaseId++;
}

BattlePayProduct const* BattlePayMgr::GetProduct(uint32 productId) const
{
    for (BattlePayProduct const& product : m_products)
        if (product.Id == productId)
            return &product;

    return NULL;
}

BattlePayProductItemsVector const& BattlePayMgr::GetItems(uint32 productId) const
{
    std::unordered_map<uint32, BattlePayProductItemsVector>::const_iterator itr = m_productItems.find(productId);
    return itr != m_productItems.end() ? itr->second : EmptyItems;
}

bool BattlePayMgr::HasGroupId(uint32 groupId) const
{
    for (BattlePayGroup const& group : m_groups)
        if (group.Id == groupId)
            return true;

    return false;
}

bool BattlePayMgr::HasEntryId(uint32 entryId) const
{
    for (BattlePayShopEntry const& entry : m_entries)
        if (entry.Id == entryId)
            return true;

    return false;
}

uint64 BattlePayMgr::GetCurrentPrice(BattlePayProduct const* product) const
{
    if (!product)
        return 0;

    uint64 price = product->Price * BATTLE_PAY_CURRENCY_PRECISION;
    if (!product->Discount || product->Discount >= 100)
        return product->Discount >= 100 ? 0 : price;

    return price - (price * product->Discount) / 100;
}

void BattlePayMgr::LoadProductsFromDb()
{
    uint32 oldMSTime = getMSTime();
    m_products.clear();

    QueryResult result = WorldDatabase.Query("SELECT id, title, description, icon, price, discount, displayId, type, choiceType, flags, flagsInfo FROM battle_pay_product ORDER BY id ASC");
    if (!result)
    {
        SF_LOG_INFO("server.loading", ">> Loaded 0 Battle Pay products. Table `battle_pay_product` is empty or missing.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        BattlePayProduct product;
        product.Id = fields[0].GetUInt32();
        product.Title = fields[1].GetString();
        product.Description = fields[2].GetString();
        product.Icon = fields[3].GetUInt32();
        product.Price = fields[4].GetUInt32();
        product.Discount = fields[5].GetUInt32();
        product.DisplayId = fields[6].GetUInt32();
        product.Type = fields[7].GetUInt8();
        product.ChoiceType = fields[8].GetUInt8();
        product.Flags = fields[9].GetUInt32();
        product.FlagsInfo = fields[10].GetUInt32();

        if (GetProduct(product.Id))
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay product id %u already exists, skipped.", product.Id);
            continue;
        }

        if (product.Title.size() > MAX_BATTLE_PAY_PRODUCT_TITLE_SIZE || product.Description.size() > MAX_BATTLE_PAY_PRODUCT_DESCRIPTION_SIZE)
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay product id %u has a title or description that is too long, skipped.", product.Id);
            continue;
        }

        if (product.Discount > 99)
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay product id %u has discount %u (max 99), skipped.", product.Id, product.Discount);
            continue;
        }

        if (product.Type != BATTLE_PAY_PRODUCT_TYPE_ITEM && product.Type != BATTLE_PAY_PRODUCT_TYPE_SERVICE)
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay product id %u has unknown type %u, skipped.", product.Id, product.Type);
            continue;
        }

        if (product.ChoiceType > 3)
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay product id %u has choice type %u, skipped.", product.Id, product.ChoiceType);
            continue;
        }

        m_products.push_back(product);
    } while (result->NextRow());

    SF_LOG_INFO("server.loading", ">> Loaded %u Battle Pay products in %u ms", uint32(m_products.size()), GetMSTimeDiffToNow(oldMSTime));
}

void BattlePayMgr::LoadProductLocalesFromDb()
{
    m_productLocales.clear();

    QueryResult result = WorldDatabase.Query("SELECT ID, Locale, Title, Description FROM battle_pay_product_locale");
    if (!result)
    {
        SF_LOG_INFO("server.loading", ">> Loaded 0 Battle Pay product locales. Table `battle_pay_product_locale` is empty or missing.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 id = fields[0].GetUInt32();
        if (!GetProduct(id))
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay product locale %u has no product, skipped.", id);
            continue;
        }

        LocaleConstant locale = GetLocaleByName(fields[1].GetString());
        if (locale == LOCALE_enUS)
            continue;

        BattlePayProductLocale& data = m_productLocales[id];
        ObjectMgr::AddLocaleString(fields[2].GetString(), locale, data.Title);
        ObjectMgr::AddLocaleString(fields[3].GetString(), locale, data.Description);
    } while (result->NextRow());
}

void BattlePayMgr::LoadProductItemsFromDb()
{
    uint32 oldMSTime = getMSTime();
    m_productItems.clear();

    QueryResult result = WorldDatabase.Query("SELECT id, itemId, count, productId FROM battle_pay_product_items ORDER BY id ASC");
    if (!result)
    {
        SF_LOG_INFO("server.loading", ">> Loaded 0 Battle Pay product items. Table `battle_pay_product_items` is empty or missing.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 id = fields[0].GetUInt32();
        uint32 itemId = fields[1].GetUInt32();
        uint32 itemCount = fields[2].GetUInt32();
        uint32 productId = fields[3].GetUInt32();

        if (!GetProduct(productId))
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay product item %u references missing product %u, skipped.", id, productId);
            continue;
        }

        if (itemId && !sObjectMgr->GetItemTemplate(itemId))
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay item %u for product %u is not in item_template, skipped.", itemId, productId);
            continue;
        }

        if (!itemCount)
            itemCount = 1;

        BattlePayProductItem item;
        item.Id = id;
        item.ItemId = itemId;
        item.Count = itemCount;
        m_productItems[productId].push_back(item);
        ++count;
    } while (result->NextRow());

    SF_LOG_INFO("server.loading", ">> Loaded %u Battle Pay product items in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void BattlePayMgr::LoadGroupsFromDb()
{
    uint32 oldMSTime = getMSTime();
    m_groups.clear();

    QueryResult result = WorldDatabase.Query("SELECT id, idx, name, icon, type FROM battle_pay_group ORDER BY idx ASC");
    if (!result)
    {
        SF_LOG_INFO("server.loading", ">> Loaded 0 Battle Pay groups. Table `battle_pay_group` is empty or missing.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        BattlePayGroup group;
        group.Id = fields[0].GetUInt32();
        group.Index = fields[1].GetUInt32();
        group.Name = fields[2].GetString();
        group.Icon = fields[3].GetUInt32();
        group.Type = fields[4].GetUInt8();

        if (HasGroupId(group.Id))
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay group id %u already exists, skipped.", group.Id);
            continue;
        }

        if (group.Name.empty() || group.Name.size() > MAX_BATTLE_PAY_GROUP_NAME_SIZE)
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay group id %u has an empty or too long name, skipped.", group.Id);
            continue;
        }

        if (group.Type >= BATTLE_PAY_GROUP_TYPE_END)
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay group id %u has invalid type %u, skipped.", group.Id, group.Type);
            continue;
        }

        m_groups.push_back(group);
    } while (result->NextRow());

    SF_LOG_INFO("server.loading", ">> Loaded %u Battle Pay groups in %u ms", uint32(m_groups.size()), GetMSTimeDiffToNow(oldMSTime));
}

void BattlePayMgr::LoadGroupLocalesFromDb()
{
    m_groupLocales.clear();

    QueryResult result = WorldDatabase.Query("SELECT ID, Locale, Name FROM battle_pay_group_locale");
    if (!result)
    {
        SF_LOG_INFO("server.loading", ">> Loaded 0 Battle Pay group locales. Table `battle_pay_group_locale` is empty or missing.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 id = fields[0].GetUInt32();
        if (!HasGroupId(id))
            continue;

        LocaleConstant locale = GetLocaleByName(fields[1].GetString());
        if (locale == LOCALE_enUS)
            continue;

        ObjectMgr::AddLocaleString(fields[2].GetString(), locale, m_groupLocales[id].Name);
    } while (result->NextRow());
}

void BattlePayMgr::LoadEntriesFromDb()
{
    uint32 oldMSTime = getMSTime();
    m_entries.clear();

    QueryResult result = WorldDatabase.Query("SELECT id, productId, groupId, idx, title, description, icon, displayId, banner, flags FROM battle_pay_entry ORDER BY id ASC");
    if (!result)
    {
        SF_LOG_INFO("server.loading", ">> Loaded 0 Battle Pay entries. Table `battle_pay_entry` is empty or missing.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        BattlePayShopEntry entry;
        entry.Id = fields[0].GetUInt32();
        entry.ProductId = fields[1].GetUInt32();
        entry.GroupId = fields[2].GetUInt32();
        entry.Index = fields[3].GetInt32();
        entry.Title = fields[4].GetString();
        entry.Description = fields[5].GetString();
        entry.Icon = fields[6].GetUInt32();
        entry.DisplayId = fields[7].GetUInt32();
        entry.Banner = fields[8].GetUInt8();
        entry.Flags = fields[9].GetUInt32();

        if (HasEntryId(entry.Id) || !HasGroupId(entry.GroupId) || !GetProduct(entry.ProductId))
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay entry id %u is duplicate or points at a missing group or product, skipped.", entry.Id);
            continue;
        }

        if (entry.Banner >= BATTLE_PAY_BANNER_TYPE_END)
        {
            SF_LOG_ERROR("sql.sql", "Battle Pay entry id %u has invalid banner %u, skipped.", entry.Id, entry.Banner);
            continue;
        }

        m_entries.push_back(entry);
    } while (result->NextRow());

    SF_LOG_INFO("server.loading", ">> Loaded %u Battle Pay entries in %u ms", uint32(m_entries.size()), GetMSTimeDiffToNow(oldMSTime));
}

void BattlePayMgr::LoadEntryLocalesFromDb()
{
    m_entryLocales.clear();

    QueryResult result = WorldDatabase.Query("SELECT ID, Locale, Title, Description FROM battle_pay_entry_locale");
    if (!result)
    {
        SF_LOG_INFO("server.loading", ">> Loaded 0 Battle Pay entry locales. Table `battle_pay_entry_locale` is empty or missing.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 id = fields[0].GetUInt32();
        if (!HasEntryId(id))
            continue;

        LocaleConstant locale = GetLocaleByName(fields[1].GetString());
        if (locale == LOCALE_enUS)
            continue;

        BattlePayShopEntryLocale& data = m_entryLocales[id];
        ObjectMgr::AddLocaleString(fields[2].GetString(), locale, data.Title);
        ObjectMgr::AddLocaleString(fields[3].GetString(), locale, data.Description);
    } while (result->NextRow());
}

uint64 BattlePayMgr::GetPointsBalance(WorldSession* session) const
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BATTLE_PAY_POINTS);
    stmt->setUInt32(0, session->GetAccountId());
    PreparedQueryResult result = LoginDatabase.Query(stmt);
    if (!result)
        return 0;

    return result->Fetch()[0].GetUInt64();
}

bool BattlePayMgr::TakePoints(WorldSession* session, uint64 points) const
{
    if (!points)
        return true;

    uint64 before = GetPointsBalance(session);
    if (before < points)
        return false;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_BATTLE_PAY_POINTS);
    stmt->setUInt64(0, points);
    stmt->setUInt32(1, session->GetAccountId());
    stmt->setUInt64(2, points);
    LoginDatabase.DirectExecute(stmt);
    return GetPointsBalance(session) + points == before;
}

void BattlePayMgr::GivePoints(WorldSession* session, uint64 points) const
{
    if (!points)
        return;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_BATTLE_PAY_POINTS_ADD);
    stmt->setUInt64(0, points);
    stmt->setUInt32(1, session->GetAccountId());
    LoginDatabase.DirectExecute(stmt);
}

void BattlePayMgr::SendPointsBalance(WorldSession* session)
{
    double shown = double(GetPointsBalance(session)) / double(BATTLE_PAY_CURRENCY_PRECISION);
    session->SendNotification("Shop balance: %.2f", shown);
}

bool BattlePayMgr::OwnsCharacter(WorldSession* session, uint32 characterGuid) const
{
    if (!characterGuid)
        return false;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ACCOUNT_BY_GUID);
    stmt->setUInt32(0, characterGuid);
    PreparedQueryResult result = CharacterDatabase.Query(stmt);
    if (!result)
        return false;

    return result->Fetch()[0].GetUInt32() == session->GetAccountId();
}

void BattlePayMgr::RegisterPurchase(WorldSession* session, uint32 characterGuid, uint32 productId, uint32 itemId, uint64 price) const
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_BATTLE_PAY_LOG);
    stmt->setUInt32(0, session->GetAccountId());
    stmt->setUInt32(1, characterGuid);
    stmt->setUInt32(2, realmID);
    stmt->setUInt32(3, productId);
    stmt->setUInt32(4, itemId);
    stmt->setUInt64(5, price);
    LoginDatabase.Execute(stmt);
}

bool BattlePayMgr::Deliver(WorldSession* session, BattlePayProduct const* product, uint32 characterGuid) const
{
    if (!product)
        return false;

    if (product->Type == BATTLE_PAY_PRODUCT_TYPE_SERVICE && product->Id == BATTLE_PAY_SERVICE_BOOST)
    {
        if (session->HasBoost())
            return false;

        // The character screen already consumes this flag through HandleBattlePayCharBoost.
        session->SetBoosting(true, true);
        session->SendBattlePayDistributionUpdate(0, CHARACTER_BOOST, CHARACTER_BOOST_ALLOW, CHARACTER_BOOST_TEXT_ID, CHARACTER_BOOST_BONUS_TEXT, CHARACTER_BOOST_BONUS_TEXT2);
        return true;
    }

    if (product->Type != BATTLE_PAY_PRODUCT_TYPE_ITEM)
        return false;

    BattlePayProductItemsVector const& items = GetItems(product->Id);
    if (items.empty() || !OwnsCharacter(session, characterGuid))
        return false;

    Player* player = session->GetPlayer();
    bool giveToPlayer = player && player->GetGUIDLow() == characterGuid;
    std::vector<BattlePayProductItem const*> mailed;

    for (BattlePayProductItem const& item : items)
    {
        if (!item.ItemId || !sObjectMgr->GetItemTemplate(item.ItemId))
            return false;

        if (giveToPlayer && player->AddItem(item.ItemId, item.Count))
            continue;

        mailed.push_back(&item);
    }

    if (mailed.empty())
    {
        if (player)
            player->SaveToDB();
        return true;
    }

    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    MailDraft draft(product->Title, product->Description);
    for (BattlePayProductItem const* item : mailed)
    {
        if (Item* created = Item::CreateItem(item->ItemId, item->Count, giveToPlayer ? player : NULL))
        {
            created->SaveToDB(trans);
            draft.AddItem(created);
        }
        else
            return false;
    }

    draft.SendMailTo(trans, MailReceiver(giveToPlayer ? player : NULL, characterGuid), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_HAS_BODY);
    CharacterDatabase.CommitTransaction(trans);
    return true;
}

void BattlePayMgr::SendUnavailableProductList(WorldSession* session) const
{
    WorldPacket data(SMSG_BATTLE_PAY_GET_PRODUCT_LIST_RESPONSE, 12);
    data.WriteBits(0, 19);
    data.WriteBits(0, 19);
    data.WriteBits(0, 20);
    data.FlushBits();
    data << uint32(BATTLE_PAY_CURRENCY_UNKNOWN);
    data << uint32(BATTLE_PAY_STATUS_UNAVAILABLE);
    session->SendPacket(&data);
}

void BattlePayMgr::SendPurchaseList(WorldSession* session)
{
    WorldPacket data(SMSG_BATTLE_PAY_GET_PURCHASE_LIST_RESPONSE, 8);
    data.WriteBits(0, 19);
    data.FlushBits();
    data << uint32(0);
    session->SendPacket(&data);
}

void BattlePayMgr::SendProductList(WorldSession* session)
{
    if (!IsStoreEnabled())
    {
        SendUnavailableProductList(session);
        return;
    }

    WorldPacket data(SMSG_BATTLE_PAY_GET_PRODUCT_LIST_RESPONSE);
    data.WriteBits(m_entries.size(), 19);
    data.WriteBits(m_products.size(), 19);

    for (BattlePayProduct const& product : m_products)
    {
        BattlePayProductItemsVector const& items = GetItems(product.Id);
        std::string title = product.Title;
        std::string description = product.Description;
        std::unordered_map<uint32, BattlePayProductLocale>::const_iterator locale = m_productLocales.find(product.Id);
        if (locale != m_productLocales.end())
        {
            ApplyLocaleString(locale->second.Title, session->GetSessionDbLocaleIndex(), title);
            ApplyLocaleString(locale->second.Description, session->GetSessionDbLocaleIndex(), description);
        }

        data.WriteBits(product.ChoiceType, 2);
        data.WriteBits(items.size(), 20);
        for (BattlePayProductItem const& item : items)
        {
            bool alreadyOwned = false;
            if (product.Type == BATTLE_PAY_PRODUCT_TYPE_SERVICE && product.Id == BATTLE_PAY_SERVICE_BOOST)
                alreadyOwned = session->HasBoost();

            data.WriteBit(false);
            data.WriteBit(alreadyOwned);
            data.WriteBit(false); // hasItemInfo
            data.WriteBit(false); // hasBattlePetResult

            (void)item;
        }

        data.WriteBit(!title.empty());
        if (!title.empty())
        {
            data.WriteBit(product.DisplayId != 0);
            data.WriteBits(title.size(), 10);
            data.WriteBits(description.size(), 13);
            data.WriteBits(0, 10);
            data.WriteBit(product.FlagsInfo != 0);
            data.WriteBit(false);
            data.WriteBit(product.Icon != 0);
        }
    }

    data.WriteBits(m_groups.size(), 20);

    for (BattlePayShopEntry const& entry : m_entries)
    {
        std::string title = entry.Title;
        std::string description = entry.Description;
        std::unordered_map<uint32, BattlePayShopEntryLocale>::const_iterator locale = m_entryLocales.find(entry.Id);
        if (locale != m_entryLocales.end())
        {
            ApplyLocaleString(locale->second.Title, session->GetSessionDbLocaleIndex(), title);
            ApplyLocaleString(locale->second.Description, session->GetSessionDbLocaleIndex(), description);
        }

        data.WriteBit(!title.empty());
        if (!title.empty())
        {
            data.WriteBits(title.size(), 10);
            data.WriteBit(false);
            data.WriteBits(0, 10);
            data.WriteBits(description.size(), 13);
            data.WriteBit(entry.DisplayId != 0);
            data.WriteBit(entry.Icon != 0);
            data.WriteBit(false);
        }
    }

    for (BattlePayGroup const& group : m_groups)
    {
        std::string name = group.Name;
        std::unordered_map<uint32, BattlePayGroupLocale>::const_iterator locale = m_groupLocales.find(group.Id);
        if (locale != m_groupLocales.end())
            ApplyLocaleString(locale->second.Name, session->GetSessionDbLocaleIndex(), name);

        data.WriteBits(name.size(), 8);
    }

    data.FlushBits();

    for (BattlePayGroup const& group : m_groups)
    {
        std::string name = group.Name;
        std::unordered_map<uint32, BattlePayGroupLocale>::const_iterator locale = m_groupLocales.find(group.Id);
        if (locale != m_groupLocales.end())
            ApplyLocaleString(locale->second.Name, session->GetSessionDbLocaleIndex(), name);

        data << uint32(group.Index);
        data.WriteString(name);
        data << uint8(group.Type);
        data << uint32(group.Icon);
        data << uint32(group.Id);
    }

    for (BattlePayProduct const& product : m_products)
    {
        BattlePayProductItemsVector const& items = GetItems(product.Id);
        std::string title = product.Title;
        std::string description = product.Description;
        std::unordered_map<uint32, BattlePayProductLocale>::const_iterator locale = m_productLocales.find(product.Id);
        if (locale != m_productLocales.end())
        {
            ApplyLocaleString(locale->second.Title, session->GetSessionDbLocaleIndex(), title);
            ApplyLocaleString(locale->second.Description, session->GetSessionDbLocaleIndex(), description);
        }

        data << uint8(product.Type);
        for (BattlePayProductItem const& item : items)
        {
            data << uint32(item.ItemId);
            data << uint32(item.Count);
            data << uint32(item.Id);
        }

        if (!title.empty())
        {
            if (product.DisplayId)
                data << uint32(product.DisplayId);
            if (product.Icon)
                data << uint32(product.Icon);

            data.WriteString("");
            data.WriteString(title);
            if (product.FlagsInfo)
                data << uint32(product.FlagsInfo);
            data.WriteString(description);
        }

        data << uint32(product.Id);
        data << uint32(product.Flags);
        data << uint64(product.Price * BATTLE_PAY_CURRENCY_PRECISION);
        data << uint64(GetCurrentPrice(&product));
    }

    for (BattlePayShopEntry const& entry : m_entries)
    {
        std::string title = entry.Title;
        std::string description = entry.Description;
        std::unordered_map<uint32, BattlePayShopEntryLocale>::const_iterator locale = m_entryLocales.find(entry.Id);
        if (locale != m_entryLocales.end())
        {
            ApplyLocaleString(locale->second.Title, session->GetSessionDbLocaleIndex(), title);
            ApplyLocaleString(locale->second.Description, session->GetSessionDbLocaleIndex(), description);
        }

        if (!title.empty())
        {
            data.WriteString("");
            if (entry.Icon)
                data << uint32(entry.Icon);
            if (entry.DisplayId)
                data << uint32(entry.DisplayId);
            data.WriteString(description);
            data.WriteString(title);
        }

        data << uint32(entry.Flags);
        data << uint8(entry.Banner);
        data << uint32(entry.Id);
        data << int32(entry.Index);
        data << uint32(entry.GroupId);
        data << uint32(entry.ProductId);
    }

    data << uint32(GetStoreCurrency());
    data << uint32(BATTLE_PAY_STATUS_OK);
    session->SendPacket(&data);
}

void BattlePayMgr::SendPurchaseUpdate(WorldSession* session) const
{
    BattlePayPurchase const& purchase = session->GetBattlePayPurchase();
    std::string wallet;
    if (purchase.Status == BATTLE_PAY_PURCHASE_STATUS_ALLOWED_TO_BUY || purchase.Status == BATTLE_PAY_PURCHASE_STATUS_BUYING || purchase.Status == BATTLE_PAY_PURCHASE_STATUS_BUYED)
        wallet = "Account";

    WorldPacket data(SMSG_BATTLE_PAY_PURCHASE_UPDATE);
    data.WriteBits(1, 19);
    data.WriteBits(wallet.size(), 8);
    data.FlushBits();
    data << uint32(purchase.Result);
    data << uint64(purchase.PurchaseId);
    data << uint32(purchase.ProductId);
    data << uint32(purchase.Status);
    data.WriteString(wallet);
    session->SendPacket(&data);
}

void BattlePayMgr::AdvancePurchase(WorldSession* session)
{
    BattlePayPurchase& purchase = session->GetBattlePayPurchase();
    BattlePayProduct const* product = GetProduct(purchase.ProductId);

    if (purchase.Status == BATTLE_PAY_PURCHASE_STATUS_READY && purchase.Result == BATTLE_PAY_RESULT_OK)
    {
        WorldPacket start(SMSG_BATTLE_PAY_START_PURCHASE_RESPONSE);
        start << uint64(purchase.PurchaseId);
        start << uint32(1);
        start << uint32(purchase.ClientToken);
        session->SendPacket(&start);
        SendPurchaseUpdate(session);

        if (!IsStoreEnabled() || !product)
        {
            purchase.Status = BATTLE_PAY_PURCHASE_STATUS_READY;
            purchase.Result = !IsStoreEnabled() ? BATTLE_PAY_RESULT_UNAVAILABLE : BATTLE_PAY_RESULT_SHOP_ERROR;
            session->BeginBattlePayPurchase(purchase, 500);
            return;
        }

        purchase.Status = BATTLE_PAY_PURCHASE_STATUS_ALLOWED_TO_BUY;
        purchase.Result = BATTLE_PAY_RESULT_OK;
        session->BeginBattlePayPurchase(purchase, 500);
        return;
    }

    if (purchase.Status == BATTLE_PAY_PURCHASE_STATUS_READY)
    {
        WorldPacket start(SMSG_BATTLE_PAY_START_PURCHASE_RESPONSE);
        start << uint64(purchase.PurchaseId);
        start << uint32(purchase.Result ? purchase.Result : uint32(BATTLE_PAY_RESULT_SHOP_ERROR));
        start << uint32(purchase.ClientToken);
        session->SendPacket(&start);
        SendPurchaseUpdate(session);
        return;
    }

    if (purchase.Status == BATTLE_PAY_PURCHASE_STATUS_ALLOWED_TO_BUY && purchase.Result == BATTLE_PAY_RESULT_OK)
    {
        SendPurchaseUpdate(session);

        bool valid = product && product->Flags != BATTLE_PAY_PRODUCT_DISABLES;
        uint32 characterGuid = GUID_LOPART(uint64(purchase.Target));
        if (valid && product->Type == BATTLE_PAY_PRODUCT_TYPE_SERVICE && product->Id == BATTLE_PAY_SERVICE_BOOST && session->HasBoost())
            valid = false;
        if (valid && product->Type == BATTLE_PAY_PRODUCT_TYPE_ITEM && (!characterGuid || GetItems(product->Id).empty() || !OwnsCharacter(session, characterGuid)))
            valid = false;

        uint64 price = GetCurrentPrice(product);
        if (valid && GetPointsBalance(session) < price)
            valid = false;

        purchase.ServerToken = uint32(irand(1, 999999));
        WorldPacket confirm(SMSG_BATTLE_PAY_CONFIRM_PURCHASE);
        confirm << uint32(purchase.ServerToken);
        confirm << uint64(price);
        confirm << uint64(purchase.PurchaseId);
        session->SendPacket(&confirm);

        if (!valid)
        {
            purchase.Result = BATTLE_PAY_RESULT_SHOP_ERROR;
            session->BeginBattlePayPurchase(purchase, 200);
        }
        return;
    }

    if (purchase.Status == BATTLE_PAY_PURCHASE_STATUS_ALLOWED_TO_BUY)
    {
        SendPurchaseUpdate(session);
        return;
    }

    if (purchase.Status == BATTLE_PAY_PURCHASE_STATUS_BUYING && !purchase.Delivered)
    {
        SendPurchaseUpdate(session);

        WorldPacket ended(SMSG_BATTLE_PAY_DELIVERY_ENDED);
        BattlePayProductItemsVector const& items = product ? GetItems(product->Id) : EmptyItems;
        ended.WriteBits(items.size(), 22);
        ended.FlushBits();
        for (BattlePayProductItem const& item : items)
            ended << uint32(item.ItemId);
        ended << uint64(0);
        session->SendPacket(&ended);

        purchase.Delivered = true;
        session->BeginBattlePayPurchase(purchase, 1000);
        return;
    }

    if (purchase.Status == BATTLE_PAY_PURCHASE_STATUS_BUYING && purchase.Delivered)
    {
        uint32 characterGuid = GUID_LOPART(uint64(purchase.Target));
        uint64 price = GetCurrentPrice(product);
        bool affordable = product && GetPointsBalance(session) >= price && TakePoints(session, price);
        bool delivered = affordable && Deliver(session, product, characterGuid);
        if (affordable && !delivered)
            GivePoints(session, price);

        if (!delivered)
        {
            purchase.Status = BATTLE_PAY_PURCHASE_STATUS_ALLOWED_TO_BUY;
            purchase.Result = BATTLE_PAY_RESULT_SHOP_ERROR;
            SendPurchaseUpdate(session);
            return;
        }

        uint32 loggedItem = 0;
        BattlePayProductItemsVector const& items = GetItems(product->Id);
        if (!items.empty())
            loggedItem = items.front().ItemId;

        RegisterPurchase(session, characterGuid, product->Id, loggedItem, price);
        purchase.Status = BATTLE_PAY_PURCHASE_STATUS_BUYED;
        purchase.Result = BATTLE_PAY_RESULT_OK;
        SendPurchaseUpdate(session);
        SendPointsBalance(session);
    }
}
