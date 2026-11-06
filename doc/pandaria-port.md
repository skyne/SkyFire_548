# Pandaria port plan

Staged work to fill the 5.4.8 gaps that other public cores already have
and this tree does not. Each phase is independently testable. Nothing here
is a verbatim copy: those cores are Trinity-based, and SkyFire's session,
packet, and script APIs differ. Read their script for creature entries,
spell ids, and phase order, then rewrite it against SkyFire's `ScriptedAI`
and `InstanceScript`.

Eluna is out of scope. Do not port `LuaEngine`.

## Sources

Both trees are GPL-2.0. When a file is adapted, keep the original copyright
header and note the SkyFire adaptation beside it.

* Primary: <https://github.com/alexkulya/pandaria_5.4.8>
* Same lineage, plus cross-faction battlegrounds:
  <https://github.com/Legends-of-Azeroth/Legends-of-Azeroth-Pandaria-5.4.8>

Legends of Azeroth is the place to look for `src/server/game/Cfbg/`. For
instances, scenarios, vignettes, and the shop, start with alexkulya. The
instance folders match.

## What we already have

* Mogu'shan Vaults, Stormstout Brewery, Temple of the Jade Serpent.
* Valley of the Four Winds and Wandering Isle zone scripts.
* Battle pets, black market, character boost, and challenge-mode packets
  that answer with empty rewards and an empty leaderboard.
* `mod-ahbot` and the playerbots module. Do not re-port those.

Scarlet Monastery in this tree is the classic dungeon (Herod, Doan,
Whitemane). The MoP revamp, Scarlet Halls, and Scholomance are separate
ports, not patches on those files.

## Phase 1 - Shop catalog

Source: `src/server/game/BattlePay/` (`BattlePayMgr`, `ServiceBoost`).

The character screen already gets an empty purchase list and an
"unavailable" product list (`BoostHandler.cpp`). Filling the shop means:

* A catalog (products, groups, display info) and a points balance.
* `CMSG_BATTLE_PAY_START_PURCHASE` (`0x1886`) and
  `CMSG_BATTLE_PAY_CONFIRM_PURCHASE_RESPONSE` (`0x0812`), which are still
  ignored.
* Distribution of the purchased item or service. The character boost path
  already exists and should be one product, not a second implementation.

Ship with the store still able to report unavailable. An empty catalog must
not disconnect the client.

## Phase 2 - Vignettes

Source: `src/server/game/Vignette/` (`Vignette`, `VignetteMgr`).

This is the minimap marker for rares and treasures. It has no dependency on
raids. Timeless Isle scripts in phase 6 need it, so land the manager before
those scripts.

## Phase 3 - Scenarios

Source: `src/server/game/Scenarios/` (`ScenarioMgr`) and
`src/server/scripts/Pandaria/Scenarios/`.

Land the manager first. Then one scenario script per change. A scenario is
a staged 3-player instance with its own criteria tracking, so the manager
has to exist before any of those scripts compile against it.

## Phase 4 - Dungeons, one instance per change

Source folders under `src/server/scripts/Pandaria/` unless noted. Do the
leveling dungeons before the raids. Compare Brewery and Jade Serpent with
the upstream copies and take only the missing encounters. Do not replace a
working SkyFire script with the Trinity one.

1. Mogu'shan Palace (`MogushanPalace`)
2. Shado-pan Monastery (`ShadopanMonastery`)
3. Gate of the Setting Sun (`GateOfTheSettingSun`)
4. Siege of Niuzao Temple (`SiegeOfNiuzaoTemple`)
5. Scarlet Halls (`src/server/scripts/EasternKingdoms/ScarletHalls`)
6. Scarlet Monastery MoP revamp (`EasternKingdoms/ScarletMonastery`)
7. Scholomance MoP (`EasternKingdoms/Scholomance`)

Items 1-4 and 5-7 are the challenge-mode set that this tree does not script
yet, together with Brewery and Jade Serpent which it does.

## Phase 5 - Challenge mode medals

Depends on phase 4 for each map. The packet handlers in
`ChallengeModeHandler.cpp` already reply; they send zero rewards and zero
leaders. The other core records the run inside the dungeon `InstanceScript`
and fills those packets. Add that per dungeon as its script lands, then
fill the reward and leaderboard replies from the stored times.

## Phase 6 - Raids and world content

Use the folder map below. One raid, or one boss wing, per change. Mogu'shan
Vaults already has encounters here (`Pandaria/MoguShanVault/`). Diff it
against `MogushanVault` and fill gaps. Do not start by deleting it.

Release order after the vaults:

1. Heart of Fear (`HeartOfFear`)
2. Terrace of Endless Spring (`TerraceOfEndlessSpring`)
3. Throne of Thunder (`ThroneOfThunder`)
4. Siege of Orgrimmar (`SiegeOfOrgrimmar`)

World content after vignettes (phase 2) exist:

* Timeless Isle (`TimelessIsle`)
* World bosses (`WorldBosses`)
* Remaining zone scripts (`Zones`), beyond the two zone files we have

## Phase 7 - Cross-faction battlegrounds

Source: `src/server/game/Cfbg/` in Legends of Azeroth only (`Cfbg.cpp`,
`Cfbg.h`).

Queue Alliance and Horde into the same battleground while the scoreboard
still has two teams. Put it behind a config flag that defaults off. This
changes matchmaking for every battleground, so it stays last and ships
separately from instance scripts.

## Raid and dungeon reference

Look these up when writing a script. Paths are relative to the repository
root of either source tree.

| Content | Path |
| --- | --- |
| Gate of the Setting Sun | `src/server/scripts/Pandaria/GateOfTheSettingSun` |
| Heart of Fear | `src/server/scripts/Pandaria/HeartOfFear` |
| Mogu'shan Palace | `src/server/scripts/Pandaria/MogushanPalace` |
| Mogu'shan Vaults | `src/server/scripts/Pandaria/MogushanVault` |
| Shado-pan Monastery | `src/server/scripts/Pandaria/ShadopanMonastery` |
| Siege of Niuzao Temple | `src/server/scripts/Pandaria/SiegeOfNiuzaoTemple` |
| Siege of Orgrimmar | `src/server/scripts/Pandaria/SiegeOfOrgrimmar` |
| Stormstout Brewery | `src/server/scripts/Pandaria/StormstoutBrewery` |
| Temple of the Jade Serpent | `src/server/scripts/Pandaria/TempleOfTheJadeSerpent` |
| Terrace of Endless Spring | `src/server/scripts/Pandaria/TerraceOfEndlessSpring` |
| Throne of Thunder | `src/server/scripts/Pandaria/ThroneOfThunder` |
| Scenarios | `src/server/scripts/Pandaria/Scenarios` |
| Timeless Isle | `src/server/scripts/Pandaria/TimelessIsle` |
| Wandering Isle | `src/server/scripts/Pandaria/WanderingIsland` |
| World bosses | `src/server/scripts/Pandaria/WorldBosses` |
| Zone scripts | `src/server/scripts/Pandaria/Zones` |
| Scarlet Halls | `src/server/scripts/EasternKingdoms/ScarletHalls` |
| Scarlet Monastery (MoP) | `src/server/scripts/EasternKingdoms/ScarletMonastery` |
| Scholomance (MoP) | `src/server/scripts/EasternKingdoms/Scholomance` |
| Shop | `src/server/game/BattlePay` |
| Scenario manager | `src/server/game/Scenarios` |
| Vignette manager | `src/server/game/Vignette` |
| Cross-faction battlegrounds | `src/server/game/Cfbg` (Legends of Azeroth) |
