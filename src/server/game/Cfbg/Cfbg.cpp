/*
 * This file is part of Project SkyFire https://www.projectskyfire.org.
 * See LICENSE.md file for Copyright information
 *
 * Adapted for SkyFire from Legends of Azeroth (GPL-2.0) src/server/game/Cfbg/Cfbg.cpp.
 * Queue and team assignment are rewritten against SkyFire Battleground, BattlegroundQueue, and Group.
 */

#include "Cfbg.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "Chat.h"
#include "Group.h"
#include "Player.h"
#include "World.h"

#include <algorithm>
#include <cstdlib>

namespace
{
void CollectUninvited(BattlegroundQueue::GroupsQueueType const& queue, BattlegroundQueue::QueuedGroupMap& out, uint32& alliance, uint32& horde)
{
    for (BattlegroundQueue::GroupsQueueType::const_iterator itr = queue.begin(); itr != queue.end(); ++itr)
    {
        GroupQueueInfo* ginfo = *itr;
        if (!ginfo || ginfo->IsInvitedToBGInstanceGUID)
            continue;

        uint32 count = uint32(ginfo->Players.size());
        if (ginfo->OriginalTeam == HORDE)
            horde += count;
        else
            alliance += count;

        out.insert(std::make_pair(count, ginfo));
    }
}

bool AlreadySelected(BattlegroundQueue::GroupsQueueType const& pool, GroupQueueInfo* ginfo)
{
    return std::find(pool.begin(), pool.end(), ginfo) != pool.end();
}
}

uint32 BattlegroundQueue::SelectionSide(Battleground const* bg, TeamId pool, uint32 queuedTeam) const
{
    if (!bg || bg->isArena() || !sWorld->GetBoolConfig(WorldBoolConfigs::CONFIG_BATTLEGROUND_CROSSFACTION_ENABLED))
        return queuedTeam;

    return pool == TEAM_HORDE ? HORDE : ALLIANCE;
}

bool BattlegroundQueue::CheckCrossFactionMatch(BattlegroundBracketId bracketId, Battleground* bg, uint32 minPlayers, uint32 maxPlayers)
{
    if (!bg || bg->isArena() || !sWorld->GetBoolConfig(WorldBoolConfigs::CONFIG_BATTLEGROUND_CROSSFACTION_ENABLED))
        return false;

    if (!FillXPlayersToBG(bracketId, bg, true, minPlayers, maxPlayers))
        return false;

    if (sBattlegroundMgr->isTesting() && (m_SelectionPools[TEAM_ALLIANCE].GetPlayerCount() || m_SelectionPools[TEAM_HORDE].GetPlayerCount()))
        return true;

    if (m_SelectionPools[TEAM_ALLIANCE].GetPlayerCount() < minPlayers || m_SelectionPools[TEAM_HORDE].GetPlayerCount() < minPlayers)
    {
        m_SelectionPools[TEAM_ALLIANCE].Init();
        m_SelectionPools[TEAM_HORDE].Init();
        return false;
    }

    return true;
}

bool BattlegroundQueue::FillXPlayersToBG(BattlegroundBracketId bracketId, Battleground* bg, bool start, uint32 minPlayers, uint32 maxPlayers)
{
    if (!bg || bg->isArena() || !sWorld->GetBoolConfig(WorldBoolConfigs::CONFIG_BATTLEGROUND_CROSSFACTION_ENABLED))
        return false;

    uint8 const bracket = uint8(bracketId);
    uint32 queuedPeople = 0;
    for (uint8 queueType = BG_QUEUE_PREMADE_ALLIANCE; queueType < BG_QUEUE_GROUP_TYPES_COUNT; ++queueType)
    {
        for (GroupsQueueType::const_iterator itr = m_QueuedGroups[bracket][queueType].begin(); itr != m_QueuedGroups[bracket][queueType].end(); ++itr)
            if (!(*itr)->IsInvitedToBGInstanceGUID)
                queuedPeople += uint32((*itr)->Players.size());
    }

    // Same gate as the upstream matcher: a new battleground needs enough players to fill both teams,
    // unless this is only topping up a battleground that has already started, or the server is testing.
    if (!(sBattlegroundMgr->isTesting() || queuedPeople >= minPlayers * 2 || !start))
        return false;

    int32 aliFree = start ? int32(maxPlayers) : bg->GetFreeSlotsForTeam(ALLIANCE);
    int32 hordeFree = start ? int32(maxPlayers) : bg->GetFreeSlotsForTeam(HORDE);
    if (aliFree < 0)
        aliFree = 0;
    if (hordeFree < 0)
        hordeFree = 0;

    m_SelectionPools[TEAM_ALLIANCE].Init();
    m_SelectionPools[TEAM_HORDE].Init();

    int32 valiFree = aliFree;
    int32 vhordeFree = hordeFree;

    QueuedGroupMap queuedGroups;
    uint32 queuedAlliance = 0;
    uint32 queuedHorde = 0;
    for (uint8 queueType = BG_QUEUE_PREMADE_ALLIANCE; queueType < BG_QUEUE_GROUP_TYPES_COUNT; ++queueType)
        CollectUninvited(m_QueuedGroups[bracket][queueType], queuedGroups, queuedAlliance, queuedHorde);

    // Keep each premade on its own faction until both scoreboard teams are even, then spill into the short side.
    if (start)
    {
        int32 smallest = std::min(std::min(aliFree, int32(queuedAlliance)), std::min(hordeFree, int32(queuedHorde)));
        QueuedGroupMap allianceGroups;
        QueuedGroupMap hordeGroups;
        for (QueuedGroupMap::const_iterator itr = queuedGroups.begin(); itr != queuedGroups.end(); ++itr)
        {
            if (itr->second->OriginalTeam == HORDE)
                hordeGroups.insert(*itr);
            else
                allianceGroups.insert(*itr);
        }

        valiFree -= PreAddPlayers(allianceGroups, smallest, uint32(aliFree));
        vhordeFree -= PreAddPlayers(hordeGroups, smallest, uint32(hordeFree));
    }

    for (QueuedGroupMap::reverse_iterator itr = queuedGroups.rbegin(); itr != queuedGroups.rend(); ++itr)
    {
        GroupQueueInfo* ginfo = itr->second;
        if (!ginfo || ginfo->IsInvitedToBGInstanceGUID)
            continue;

        if (AlreadySelected(m_SelectionPools[TEAM_ALLIANCE].SelectedGroups, ginfo) ||
            AlreadySelected(m_SelectionPools[TEAM_HORDE].SelectedGroups, ginfo))
            continue;

        // A new match stops once both teams can start. A running match keeps filling free slots.
        if (start &&
            m_SelectionPools[TEAM_ALLIANCE].GetPlayerCount() >= minPlayers &&
            m_SelectionPools[TEAM_HORDE].GetPlayerCount() >= minPlayers)
            break;

        int32 diff = std::abs(int32(valiFree - vhordeFree));
        bool moreAlliance = valiFree < vhordeFree;
        uint32 side = ginfo->OriginalTeam ? ginfo->OriginalTeam : ginfo->Team;
        if (diff > 0)
            side = moreAlliance ? HORDE : ALLIANCE;

        bool alliance = side == ALLIANCE;
        TeamId pool = alliance ? TEAM_ALLIANCE : TEAM_HORDE;
        int32 const room = alliance ? aliFree : hordeFree;
        uint32 before = m_SelectionPools[pool].GetPlayerCount();
        m_SelectionPools[pool].AddGroup(ginfo, uint32(room));
        uint32 added = m_SelectionPools[pool].GetPlayerCount() - before;
        if (!added)
            continue;

        if (alliance)
            valiFree -= int32(added);
        else
            vhordeFree -= int32(added);
    }

    return true;
}

int32 BattlegroundQueue::PreAddPlayers(QueuedGroupMap const& groups, int32 maxAdd, uint32 maxInTeam)
{
    int32 left = maxAdd;
    int32 added = 0;
    if (left <= 0)
        return 0;

    for (QueuedGroupMap::const_reverse_iterator itr = groups.rbegin(); itr != groups.rend(); ++itr)
    {
        GroupQueueInfo* ginfo = itr->second;
        int32 playerSize = int32(itr->first);
        if (!ginfo || playerSize <= 0 || playerSize > left)
            continue;

        bool alliance = ginfo->OriginalTeam != HORDE;
        TeamId pool = alliance ? TEAM_ALLIANCE : TEAM_HORDE;
        uint32 before = m_SelectionPools[pool].GetPlayerCount();
        m_SelectionPools[pool].AddGroup(ginfo, maxInTeam);
        uint32 justAdded = m_SelectionPools[pool].GetPlayerCount() - before;
        if (!justAdded)
            continue;

        left -= int32(justAdded);
        added += int32(justAdded);
    }

    return added;
}

void Cfbg::FitPlayerInTeam(Player* player, Battleground* bg, bool apply)
{
    if (!player)
        return;

    if (!apply)
    {
        uint32 nativeTeam = Player::TeamForRace(player->getRace());
        if (player->GetTeam() != nativeTeam)
            player->setFactionForRace(player->getRace());
        return;
    }

    if (!bg || bg->isArena() || !sWorld->GetBoolConfig(WorldBoolConfigs::CONFIG_BATTLEGROUND_CROSSFACTION_ENABLED))
        return;

    uint32 bgTeam = player->GetBGTeam();
    if (bgTeam != ALLIANCE && bgTeam != HORDE)
        return;

    if (player->GetTeam() == bgTeam)
        return;

    // Faction template follows the scoreboard team. Race and display are left alone and restored on leave.
    player->setFactionForRace(bgTeam == HORDE ? RACE_BLOODELF : RACE_HUMAN);

    if (WorldSession* session = player->GetSession())
        ChatHandler(session).PSendSysMessage("You are playing for the %s in this battleground.", bgTeam == ALLIANCE ? "Alliance" : "Horde");
}
