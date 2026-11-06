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
* SkyFire adaptation: shop opcodes call BattlePayMgr; character boost stays on the existing handler.
*/

#include "BattlePayMgr.h"
#include "Log.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

void WorldSession::UpdateBattlePay(uint32 diff)
{
    if (!m_battlePayPurchase.Pending)
        return;

    if (m_battlePayPurchase.Timer > diff)
    {
        m_battlePayPurchase.Timer -= diff;
        return;
    }

    m_battlePayPurchase.Pending = false;
    sBattlePayMgr->AdvancePurchase(this);
}

void WorldSession::BeginBattlePayPurchase(BattlePayPurchase purchase, uint32 delayMs)
{
    purchase.Pending = true;
    purchase.Timer = delayMs;
    m_battlePayPurchase = purchase;
}

void WorldSession::HandleBattlePayGetPurchaseList(WorldPacket& /*recvData*/)
{
    SF_LOG_DEBUG("network", "WORLD: Received CMSG_BATTLE_PAY_GET_PURCHASE_LIST");
    sBattlePayMgr->SendPurchaseList(this);
}

void WorldSession::HandleBattlePayGetProductList(WorldPacket& /*recvData*/)
{
    SF_LOG_DEBUG("network", "WORLD: Received CMSG_BATTLE_PAY_GET_PRODUCT_LIST");

    sBattlePayMgr->SendProductList(this);
    if (sBattlePayMgr->IsStoreEnabled())
        sBattlePayMgr->SendPointsBalance(this);
}

void WorldSession::HandleBattlePayStartPurchase(WorldPacket& recvData)
{
    SF_LOG_DEBUG("network", "WORLD: Received CMSG_BATTLE_PAY_START_PURCHASE");

    uint32 productId = recvData.read<uint32>();
    uint32 clientToken = recvData.read<uint32>();

    ObjectGuid target;
    recvData.ReadGuidMask(target, 2, 3, 5, 1, 4, 7, 0, 6);
    recvData.ReadGuidBytes(target, 5, 3, 7, 1, 4, 0, 6, 2);

    BattlePayPurchase purchase;
    purchase.Target = target;
    purchase.PurchaseId = sBattlePayMgr->NextPurchaseId();
    purchase.ProductId = productId;
    purchase.ClientToken = clientToken;
    purchase.Status = BATTLE_PAY_PURCHASE_STATUS_READY;
    purchase.Result = sBattlePayMgr->GetProduct(productId) ? BATTLE_PAY_RESULT_OK : BATTLE_PAY_RESULT_SHOP_ERROR;

    BeginBattlePayPurchase(purchase, 500);
}

void WorldSession::HandleBattlePayConfirmPurchaseResponse(WorldPacket& recvData)
{
    SF_LOG_DEBUG("network", "WORLD: Received CMSG_BATTLE_PAY_CONFIRM_PURCHASE_RESPONSE");

    recvData.read<uint64>(); // client echo of the quoted price
    uint32 serverToken = recvData.read<uint32>();
    bool confirm = recvData.read<bool>();

    BattlePayPurchase& purchase = m_battlePayPurchase;
    if (!purchase.PurchaseId || purchase.Status != BATTLE_PAY_PURCHASE_STATUS_ALLOWED_TO_BUY)
    {
        recvData.rfinish();
        return;
    }

    if (confirm && serverToken == purchase.ServerToken)
    {
        purchase.Status = BATTLE_PAY_PURCHASE_STATUS_BUYING;
        purchase.Result = BATTLE_PAY_RESULT_OK;
        purchase.Delivered = false;
        BeginBattlePayPurchase(purchase, 8000);
        return;
    }

    purchase.Status = BATTLE_PAY_PURCHASE_STATUS_ALLOWED_TO_BUY;
    purchase.Result = BATTLE_PAY_RESULT_DECLINED;
    purchase.Pending = false;
    sBattlePayMgr->AdvancePurchase(this);
}
