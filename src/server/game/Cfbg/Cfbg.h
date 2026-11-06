/*
 * This file is part of Project SkyFire https://www.projectskyfire.org.
 * See LICENSE.md file for Copyright information
 *
 * Adapted for SkyFire from Legends of Azeroth (GPL-2.0) src/server/game/Cfbg/Cfbg.cpp.
 */

#ifndef SF_CFBG_H
#define SF_CFBG_H

class Battleground;
class Player;

namespace Cfbg
{
    // Aligns faction with the scoreboard team while a player is inside a cross-faction battleground.
    // apply == false restores the faction that matches the player's race.
    void FitPlayerInTeam(Player* player, Battleground* bg, bool apply);
}

#endif
