/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "BotChat.h"
#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "SharedDefines.h"
#include "Timer.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace
{
    std::mutex g_factoryLock;
    std::unordered_map<std::string, BotChatFactory> g_factories;

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        return value;
    }

    std::string CleanReply(std::string text, uint32 maxLength)
    {
        for (char& ch : text)
        {
            if (ch == '\r' || ch == '\n' || ch == '\t' || ch == '|')
                ch = ' ';
        }
        while (!text.empty() && text.front() == ' ')
            text.erase(text.begin());
        while (!text.empty() && text.back() == ' ')
            text.pop_back();
        if (maxLength && text.size() > maxLength)
            text.resize(maxLength);
        return text;
    }
}

void RegisterBotChatProvider(char const* name, BotChatFactory factory)
{
    if (!name || !*name || !factory)
        return;
    std::lock_guard<std::mutex> guard(g_factoryLock);
    g_factories[Lower(name)] = std::move(factory);
}

BotChat* BotChat::instance()
{
    static BotChat chat;
    return &chat;
}

void BotChat::LoadConfig()
{
    _enabled = sConfigMgr->GetBoolDefault("Playerbots.Chat.Enable", false);
    _providerName = sConfigMgr->GetStringDefault("Playerbots.Chat.Provider", "none");
    _range = uint32(sConfigMgr->GetIntDefault("Playerbots.Chat.Range", 25));
    _cooldownMs = uint32(sConfigMgr->GetIntDefault("Playerbots.Chat.Cooldown", 8)) * 1000;
    _memoryLines = uint32(sConfigMgr->GetIntDefault("Playerbots.Chat.Memory", 8));
    _maxReplyLength = uint32(sConfigMgr->GetIntDefault("Playerbots.Chat.MaxReplyLength", 200));

    if (_range < 5)
        _range = 5;
    if (_range > 100)
        _range = 100;
    if (_cooldownMs < 1000)
        _cooldownMs = 1000;
    if (_memoryLines > 32)
        _memoryLines = 32;
    if (_maxReplyLength < 20)
        _maxReplyLength = 20;
    if (_maxReplyLength > 255)
        _maxReplyLength = 255;

    _provider.reset();
    std::string const key = Lower(_providerName);
    if (_enabled && key != "none" && !key.empty())
    {
        BotChatFactory factory;
        {
            std::lock_guard<std::mutex> guard(g_factoryLock);
            auto it = g_factories.find(key);
            if (it != g_factories.end())
                factory = it->second;
        }
        if (!factory)
            SF_LOG_ERROR("modules",
                "[mod-playerbots] Chat provider '%s' is not registered. Bots will stay quiet.",
                _providerName.c_str());
        else
        {
            _provider = factory();
            SF_LOG_INFO("modules", "[mod-playerbots] Chat provider '%s' enabled.",
                _provider ? _provider->Name() : _providerName.c_str());
        }
    }
}

void BotChat::Update()
{
    std::vector<QueuedReply> ready;
    uint32 const now = getMSTime();
    {
        std::lock_guard<std::mutex> guard(_lock);
        ready.swap(_replies);
        for (auto it = _pending.begin(); it != _pending.end();)
        {
            if (getMSTimeDiff(it->second.startedMs, now) > 30000)
                it = _pending.erase(it);
            else
                ++it;
        }
    }

    for (QueuedReply const& reply : ready)
        Deliver(reply.botGuid, reply.speakerGuid, reply.channel, reply.text);
}

bool BotChat::AcceptLine(std::string const& text) const
{
    if (_delivering || !_enabled || !_provider)
        return false;
    if (text.empty() || text.size() > 255)
        return false;
    if (text[0] == '.' || text[0] == '!')
        return false;
    return true;
}

void BotChat::Remember(uint64 botGuid, std::string const& speakerName, std::string const& text)
{
    if (!_memoryLines)
        return;
    std::string line = speakerName.empty() ? text : speakerName + ": " + text;
    std::lock_guard<std::mutex> guard(_lock);
    std::vector<std::string>& memory = _memory[botGuid];
    memory.push_back(std::move(line));
    while (memory.size() > _memoryLines)
        memory.erase(memory.begin());
}

bool BotChat::OfferTo(Player* speaker, Player* bot, BotChatChannel channel, std::string const& text)
{
    if (!AcceptLine(text) || !speaker || !bot || speaker == bot)
        return false;
    if (!bot->IsInWorld() || !sPlayerbotMgr->HasBotAI(bot->GetGUID()))
        return false;

    uint32 const now = getMSTime();
    BotChatRequest request;
    {
        std::lock_guard<std::mutex> guard(_lock);
        if (_pending.find(bot->GetGUID()) != _pending.end())
            return false;
        auto cooled = _cooldownSince.find(bot->GetGUID());
        if (cooled != _cooldownSince.end() && getMSTimeDiff(cooled->second, now) < _cooldownMs)
            return false;

        _pending[bot->GetGUID()].startedMs = now;
        _cooldownSince[bot->GetGUID()] = now;
        auto memory = _memory.find(bot->GetGUID());
        if (memory != _memory.end())
            request.memory = memory->second;
    }

    request.speakerGuid = speaker->GetGUID();
    request.speakerName = speaker->GetName();
    request.botGuid = bot->GetGUID();
    request.botName = bot->GetName();
    request.botRace = bot->getRace();
    request.botClass = bot->getClass();
    request.botGender = bot->getGender();
    request.botLevel = bot->getLevel();
    request.mapId = bot->GetMapId();
    request.zoneId = bot->GetZoneId();
    request.channel = channel;
    request.text = text;

    Remember(bot->GetGUID(), speaker->GetName(), text);
    _provider->Submit(request);
    return true;
}

void BotChat::OfferWhisper(Player* speaker, Player* bot, std::string const& text)
{
    OfferTo(speaker, bot, BotChatChannel::Whisper, text);
}

void BotChat::OfferGroup(Player* speaker, Group* group, std::string const& text, bool raid, std::string const& roleFilter)
{
    if (!AcceptLine(text) || !speaker || !group)
        return;

    Player* best = nullptr;
    float bestDist = 0.0f;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || member == speaker || !member->IsInWorld())
            continue;
        if (!sPlayerbotMgr->HasBotAI(member->GetGUID()))
            continue;
        if (!roleFilter.empty())
        {
            PlayerbotAI* ai = sPlayerbotMgr->GetBotAI(member);
            if (!ai || !ai->MatchesRoleFilter(roleFilter))
                continue;
        }
        float const dist = speaker->GetDistance(member);
        if (!best || dist < bestDist)
        {
            best = member;
            bestDist = dist;
        }
    }
    if (!best)
        return;
    OfferTo(speaker, best, raid ? BotChatChannel::Raid : BotChatChannel::Party, text);
}

void BotChat::OfferSay(Player* speaker, std::string const& text, bool yell)
{
    if (!AcceptLine(text) || !speaker || !speaker->IsInWorld())
        return;

    float const range = yell ? float(_range) * 4.0f : float(_range);
    Player* best = nullptr;
    float bestDist = 0.0f;
    {
        SF_SHARED_GUARD readGuard(*HashMapHolder<Player>::GetLock());
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* bot = pair.second;
            if (!bot || bot == speaker || !bot->IsInWorld() || bot->GetMap() != speaker->GetMap())
                continue;
            if (!sPlayerbotMgr->HasBotAI(bot->GetGUID()))
                continue;
            float const dist = speaker->GetDistance(bot);
            if (dist > range)
                continue;
            if (!best || dist < bestDist)
            {
                best = bot;
                bestDist = dist;
            }
        }
    }
    if (!best)
        return;
    OfferTo(speaker, best, yell ? BotChatChannel::Yell : BotChatChannel::Say, text);
}

void BotChat::Complete(uint64 botGuid, uint64 speakerGuid, BotChatChannel channel, std::string text)
{
    std::lock_guard<std::mutex> guard(_lock);
    _pending.erase(botGuid);
    if (text.empty())
        return;
    QueuedReply reply;
    reply.botGuid = botGuid;
    reply.speakerGuid = speakerGuid;
    reply.channel = channel;
    reply.text = std::move(text);
    _replies.push_back(std::move(reply));
}

void BotChat::Deliver(uint64 botGuid, uint64 speakerGuid, BotChatChannel channel, std::string text)
{
    text = CleanReply(std::move(text), _maxReplyLength);
    if (text.empty())
        return;

    Player* bot = ObjectAccessor::FindPlayer(botGuid);
    if (!bot || !bot->IsInWorld())
        return;

    struct Delivering
    {
        bool& flag;
        explicit Delivering(bool& flag) : flag(flag) { flag = true; }
        ~Delivering() { flag = false; }
    } delivering(_delivering);

    switch (channel)
    {
        case BotChatChannel::Whisper:
            bot->Whisper(text, Language::LANG_UNIVERSAL, speakerGuid);
            break;
        case BotChatChannel::Yell:
            bot->Yell(text, Language::LANG_UNIVERSAL);
            break;
        case BotChatChannel::Party:
        case BotChatChannel::Raid:
        {
            Group* group = bot->GetGroup();
            if (!group)
                break;
            ChatMsg type = ChatMsg::CHAT_MSG_PARTY;
            if (channel == BotChatChannel::Raid)
                type = group->IsLeader(bot->GetGUID()) ? ChatMsg::CHAT_MSG_RAID_LEADER : ChatMsg::CHAT_MSG_RAID;
            else if (group->IsLeader(bot->GetGUID()))
                type = ChatMsg::CHAT_MSG_PARTY_LEADER;

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, type, Language::LANG_UNIVERSAL, bot, nullptr, text);
            group->BroadcastPacket(&data, false, group->GetMemberGroup(bot->GetGUID()));
            break;
        }
        case BotChatChannel::Say:
        default:
            bot->Say(text, Language::LANG_UNIVERSAL);
            break;
    }

    Remember(bot->GetGUID(), bot->GetName(), text);
}
