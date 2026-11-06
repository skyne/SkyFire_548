# Playerbots: features and configuration

SkyFire 5.4.8 module that logs characters in without a client and drives them with AI. It is a population and party-fill tool: follow, combat, light questing, gear, and dungeon-finder fill. It is not a full AzerothCore strategy engine, and it does not run battlegrounds or arenas on its own.

Copy `conf/playerbots.conf.dist` to `playerbots.conf` next to `worldserver.conf`. The module stays off until `Playerbots.Enable = 1`. Reload with `.playerbots reload`.

Chat orders and GM commands are listed in full in [COMMANDS.md](COMMANDS.md).

## What a bot can do

**Sessions.** A bot is a real character on a socketless session. `.playerbots add <name>` logs an offline character in. Random-pool bots come from accounts whose name starts with `Playerbots.RandomBots.AccountPrefix` (default `RNDBOT`). Characters on those accounts are never mixed with a normal player's alts unless you add the alt yourself. Logging out removes bots that sit on your own account; pool bots stay online.

**Self-bot.** `.playerbots self` attaches the same combat AI to the character you are playing. You keep movement until you whisper yourself `nc +walk`. Walk mode paths to loot, to the current target, and out of ground hazards, then gives the keys back.

**Open world.** Ungrouped random bots wander, grind, loot corpses, repair, sell junk, eat and drink, and accept or turn in nearby quests. Quest travel stays on the current map. There is no portal or flight-graph travel. While following a player, bots do not run off to quests; they copy quests the leader accepts and turn them in when the leader does.

**Groups.** Bots accept invites, follow in role slots (tank, healer, melee, ranged), assist the leader, loot, and answer ready checks. Tanks peel and taunt. Healers use per-spec heals. Orders work as a whisper to one bot or as party or raid chat, with `@tank`, `@dps`, `@heal`, and `@ranged` filters.

**Combat.** All 34 Mists of Pandaria specializations have a priority list, including Monk. Shared behavior covers interrupts, cleanses, defensives, racials, on-use trinkets, crowd control, threat throttle, and stepping out of ground damage. Healers can be told to DPS or to save mana.

**Gear and upkeep.** `.playerbots init` sets spec, talents, glyphs, trainer spells, riding, one mount per unlocked tier, and gear. `maintenance` refreshes that without replacing spec or gear, and enchants and gems from spec stat weights. `autogear` upgrades worn pieces. Bots roll Need only on upgrades and equip bag upgrades on their own. Random bots sell gray junk, and unbound greens when `Playerbots.Vendor.SellGreens` is on.

**Dungeon finder.** Grouped bots set a party role from their spec and accept the role check and proposal. With `Playerbots.RandomBotJoinLfg = 1`, idle random bots join a real player's queue to fill missing roles. Bot-only groups are removed. LFR and random battlegrounds are not automated.

**Cluster.** Each world node only logs in bots saved on a map it owns. `Playerbots.RandomBots.MaxBots` is one cap for the whole cluster: after a bot moves from Elwynn to Outland, Eastern Kingdoms does not log in a replacement. The move logs the bot out and the owning node loads it within about ten seconds, still inside that cap. Account create, wipe, and orphan cleanup run only on the node that owns Eastern Kingdoms (map 0). Dungeon maps that no node owns are refused, and bots will not queue for those dungeons. Bots on a map with no real player stay dormant.

**Chat.** Bots do not talk on their own. Whispers, party chat, and local say or yell that are not orders can be handed to a chat provider. Nothing is built in: `BotChatProvider` is the seam for Ollama, llama.cpp, or anything else. The provider is called on the world thread and must return immediately, then post the reply with `sBotChat->Complete` from its own thread. One bot answers each line. Orders (`follow`, `co`, lines starting with `.` or `!`) stay with the command handler. Chat is off until `Playerbots.Chat.Enable = 1` and `Playerbots.Chat.Provider` names a registered provider.

## What it does not do

- The full AzerothCore strategy set (raid strategies, RTSC, loot lists, `ll`).
- Summoning your own alts as a permanent bot party. `.playerbots add` on your account works until you log out.
- A quest graph across zones, boats, or portals. `nc +quests` is same-map only.
- Standing in battlegrounds or arenas and playing the objective.
- Death Knights and neutral Pandaren in the auto-creator.

## Commands

| Command | Purpose |
| --- | --- |
| `.playerbots status` | Counts, pending logins, pacing. |
| `.playerbots add <name>` / `remove <name>` | Log one character in or out. |
| `.playerbots summon` | Pull grouped bots to you. |
| `.playerbots create` | Create missing `RNDBOT*` accounts and characters. |
| `.playerbots wipe confirm` | Delete every account with the bot prefix. |
| `.playerbots init …` | Re-spec and re-gear. See [COMMANDS.md](COMMANDS.md) for `tank`, spec names, `relevel`, `tele`. |
| `.playerbots self` | Combat AI on your own character. |
| `.playerbots reload` | Re-read this config and the candidate list. |

Useful whispers and party lines: `follow`, `stay`, `attack`, `tank attack`, `pull`, `grind`, `passive`, `quests`, `sell`, `mount`, `maintenance`, `autogear`, `co ?`, `nc +food`. The bot whispers its `co` / `nc` state back.

## Configuration

Keys below match `conf/playerbots.conf.dist`. Defaults are the file defaults, which leave the module off.

### Master switch and pool

| Key | Default | Effect |
| --- | --- | --- |
| `Playerbots.Enable` | `0` | Master switch. |
| `Playerbots.RandomBots.Enable` | `0` | Keep a pool of prefix-account characters online. |
| `Playerbots.RandomBots.MaxBots` | `0` | Online cap for the whole cluster, shared by every world node. |
| `Playerbots.RandomBots.AccountPrefix` | `RNDBOT` | Only these accounts are pool candidates. |
| `Playerbots.DeleteRandomBotAccounts` | `0` | `1` wipes prefix accounts on the next boot, then skips auto-create for that boot. Set back to `0`. |
| `Playerbots.DeleteOrphanCharacters` | `1` | Delete characters whose account is gone. Skips account 0. Cluster: Eastern Kingdoms node only. |
| `Playerbots.RandomBots.LoginInterval` | `250` | Milliseconds between starting logins. |
| `Playerbots.RandomBots.MaxPendingLogins` | `8` | Logins waiting on the database at once (1–64). |
| `Playerbots.RandomBots.LoginsPerTick` | `1` | Characters actually entered into the world per tick (1–10). Leave at 1. |

### Dungeon finder fill

| Key | Default | Effect |
| --- | --- | --- |
| `Playerbots.RandomBotJoinLfg` | `0` | Idle random bots fill a real player's dungeon-finder queue. |
| `Playerbots.RandomBotJoinLfg.MaxBots` | `10` | Cap on bots used as fillers. Still limited by open 5-man slots. |
| `Playerbots.RandomBotJoinLfg.LevelRange` | `5` | Maximum level gap between bot and player. |

### Creating characters

Incremental: existing `RNDBOT1`… accounts and their characters are kept. Death Knights and neutral Pandaren are skipped. On a cluster this runs only on the Eastern Kingdoms node.

| Key | Default | Effect |
| --- | --- | --- |
| `Playerbots.AutoCreate.OnStartup` | `0` | Create missing accounts and characters at boot. |
| `Playerbots.AutoCreate.AccountCount` | `0` | How many `RNDBOT*` accounts to ensure. Not the online cap. |
| `Playerbots.AutoCreate.AccountPassword` | `password` | Password for new bot accounts. |
| `Playerbots.AutoCreate.CharactersPerAccount` | `1` | Characters per account. Realm cap is 11. |
| `Playerbots.AutoCreate.AlliancePct` | `50` | Percent Alliance. The rest are Horde. |
| `Playerbots.AutoCreate.TankPct` | `20` | Percent tanks. |
| `Playerbots.AutoCreate.HealerPct` | `20` | Percent healers. The rest are DPS. |
| `Playerbots.AutoCreate.Level` | `1` | Fallback when min/max are omitted. |
| `Playerbots.AutoCreate.MinLevel` | `1` | Low end of the create and `relevel` band. |
| `Playerbots.AutoCreate.MaxLevel` | `1` | High end. Equal to min forces one level. |
| `Playerbots.AutoCreate.InitOnCreate` | `0` | Ignored by create. Gear runs on first login. |
| `Playerbots.AutoCreate.TeleportOnInit` | `1` | First gear pass scatters the bot by level and faction. Levels 1–5 stay at the race start. |
| `Playerbots.Init.PerTick` | `5` | Bots geared per world tick for `.playerbots init all` (1–50). |

### Gear, enchants, loot

| Key | Default | Effect |
| --- | --- | --- |
| `Playerbots.Gear.MinQuality` | `2` | Lowest quality when gearing (2 uncommon, 3 rare, 4 epic, 5 legendary). |
| `Playerbots.Gear.MaxQuality` | `4` | Highest quality for init. |
| `Playerbots.AutoGear.Command` | `1` | Allow the `autogear` order. |
| `Playerbots.AutoGear.AltBots` | `1` | Allow `autogear` on a real player's character. |
| `Playerbots.AutoGear.QualityLimit` | `4` | Quality ceiling for `autogear`. |
| `Playerbots.AutoGear.ItemLevelLimit` | `0` | Item-level ceiling. `0` means none. |
| `Playerbots.AutoGear.StatWeights` | `1` | `1` scores by spec stat weights. `0` scores by item level. |
| `Playerbots.Maintenance.Command` | `1` | Allow the `maintenance` order. |
| `Playerbots.Maintenance.Enchants` | `1` | Enchant worn gear. Replaced only by a better enchant. |
| `Playerbots.Maintenance.Gems` | `1` | Fill empty sockets. Existing gems stay. |
| `Playerbots.Maintenance.EnchantBudgetShare` | `1.0` | How strong an enchant may be relative to the item's level. |
| `Playerbots.Maintenance.StatRankStep` | `0.05` | Weight lost per step down the spec's stat list. |
| `Playerbots.Maintenance.BagEntry` | `82446` | Bag placed in empty bag slots (Royal Satchel). `0` skips bags. |
| `Playerbots.AltMaintenance.Spells` | `1` | Spells on alt bots. |
| `Playerbots.AltMaintenance.Talents` | `1` | Recommended talents on alt bots. |
| `Playerbots.AltMaintenance.Glyphs` | `1` | Glyphs on alt bots. |
| `Playerbots.AltMaintenance.Mounts` | `1` | Riding and mounts on alt bots. |
| `Playerbots.AltMaintenance.Pet` | `1` | Hunter pets on alt bots. |
| `Playerbots.AltMaintenance.Bags` | `1` | Bags on alt bots. |
| `Playerbots.AltMaintenance.Enchants` | `1` | Enchants on alt bots, and only on empty slots. |
| `Playerbots.AltMaintenance.Gems` | `1` | Gems on alt bots. |
| `Playerbots.Loot.NeedUpgradePercent` | `2` | Need only if the piece is at least this much better. |
| `Playerbots.Loot.CapBoost` | `1.15` | Extra weight for stats the bot still needs to cap. |
| `Playerbots.AutoEquip.Enable` | `1` | Equip bag upgrades automatically. |
| `Playerbots.AutoEquip.Alts` | `0` | Also do that for real players' characters. |
| `Playerbots.AutoEquip.Interval` | `60` | Seconds between out-of-combat bag checks. |

### Combat and world behavior

| Key | Default | Effect |
| --- | --- | --- |
| `Playerbots.Rest.HealthPct` | `50` | Sit to eat below this health when `nc +food` is on. |
| `Playerbots.Rest.ManaPct` | `50` | Sit to drink below this mana. |
| `Playerbots.Rest.AlmostFullHealth` | `85` | Party waits to pull until allies are this healthy. |
| `Playerbots.Rest.MediumMana` | `40` | Party waits until mana is at least this. |
| `Playerbots.SaveMana.Threshold` | `60` | With `co +save mana`, skip expensive heals below this. |
| `Playerbots.Threat.ThrottlePct` | `80` | With `co +threat`, stop damage at this share of the tank's threat. |
| `Playerbots.Flee.Enabled` | `1` | Ranged bots step back when an enemy is too close. |
| `Playerbots.Flee.Distance` | `20` | Search radius for a flee point. |
| `Playerbots.WaitForAttack.Seconds` | `5` | DPS wait this long after combat starts. Fight-back is immediate. |
| `Playerbots.Vendor.SellGreens` | `1` | Also vendor unbound uncommon and better junk. |
| `Playerbots.Vendor.HubMaxDistance` | `500` | How far to walk on the same map for a vendor. |
| `Playerbots.Quest.AutoPickReward` | `1` | Pick the best reward. `0` always takes the first choice. |

### Performance

| Key | Default | Effect |
| --- | --- | --- |
| `Playerbots.Activity.Enable` | `1` | Bots far from players tick slowly or not at all. |
| `Playerbots.Activity.ActiveRange` | `0` | Yards from a player for a full tick. `0` uses map visibility. |
| `Playerbots.Activity.SlowInterval` | `5000` | Heartbeat for bots on the same map but out of range. They still take invites. |

Bots grouped with a player, or inside an instance that already has a player, stay on a full tick.

### Chat provider

Off by default. See the comment at the top of `src/BotChat.h` for how to register a backend.

| Key | Default | Effect |
| --- | --- | --- |
| `Playerbots.Chat.Enable` | `0` | Offer non-order chat to the provider. |
| `Playerbots.Chat.Provider` | `none` | Factory name. Unknown names log an error and stay quiet. |
| `Playerbots.Chat.Range` | `25` | `/say` hear range in yards. `/yell` is four times that. |
| `Playerbots.Chat.Cooldown` | `8` | Seconds before that bot is offered another line. |
| `Playerbots.Chat.Memory` | `8` | Recent lines included with the request. |
| `Playerbots.Chat.MaxReplyLength` | `200` | Spoken reply is cut here. |

## A small populated world

1. Set `Playerbots.Enable = 1` and `Playerbots.RandomBots.Enable = 1`.
2. Set `AutoCreate.AccountCount` to the number of accounts, `CharactersPerAccount`, faction and role percents, and `MinLevel` / `MaxLevel`.
3. Set `RandomBots.MaxBots` to how many should be online across the cluster.
4. On the Eastern Kingdoms node, `.playerbots create` (or `AutoCreate.OnStartup = 1` and restart).
5. Invite bots, or set `RandomBotJoinLfg = 1` and queue for a dungeon this node owns.
