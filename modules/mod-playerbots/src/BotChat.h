/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*
* Chat seam for playerbots. The module decides when a line is conversation
* rather than an order, builds a BotChatRequest, and asks the active provider.
* A provider (Ollama, llama.cpp, a script) does the model call on its own
* thread and posts the text back through BotChat::Complete. The world thread
* is the only place that makes the bot speak.
*
* Register a backend from its own .cpp, before the config is applied:
*
*   class OllamaBotChat : public BotChatProvider
*   {
*   public:
*       char const* Name() const override { return "ollama"; }
*       void Submit(BotChatRequest const& request) override
*       {
*           std::thread([request]
*           {
*               std::string reply = AskOllama(request); // your HTTP call
*               sBotChat->Complete(request.botGuid, request.speakerGuid, request.channel, reply);
*           }).detach();
*       }
*   };
*
*   RegisterBotChatProvider("ollama", [] { return std::make_unique<OllamaBotChat>(); });
*
* Then set Playerbots.Chat.Enable = 1 and Playerbots.Chat.Provider = "ollama".
*/

#ifndef _SF_BOT_CHAT_H
#define _SF_BOT_CHAT_H

#include "Define.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class Group;

enum class BotChatChannel : uint8
{
    Say = 0,
    Yell,
    Whisper,
    Party,
    Raid
};

// Plain data. Safe to copy onto another thread. No Player pointers.
struct BotChatRequest
{
    uint64 speakerGuid = 0;
    std::string speakerName;
    uint64 botGuid = 0;
    std::string botName;
    uint8 botRace = 0;
    uint8 botClass = 0;
    uint8 botGender = 0;
    uint8 botLevel = 0;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    BotChatChannel channel = BotChatChannel::Say;
    std::string text;
    // Earlier lines this bot heard or said, oldest first. "Name: text".
    std::vector<std::string> memory;
};

class BotChatProvider
{
public:
    virtual ~BotChatProvider() = default;
    virtual char const* Name() const = 0;

    // Called on the world thread. Return quickly. Post the reply, from any
    // thread, with sBotChat->Complete. An empty completion drops the turn.
    virtual void Submit(BotChatRequest const& request) = 0;
};

using BotChatFactory = std::function<std::unique_ptr<BotChatProvider>()>;

// name is matched case-insensitively against Playerbots.Chat.Provider.
void RegisterBotChatProvider(char const* name, BotChatFactory factory);

class BotChat
{
public:
    static BotChat* instance();

    void LoadConfig();
    void Update();

    bool IsEnabled() const { return _enabled; }
    BotChatProvider* GetProvider() const { return _provider.get(); }

    // Whisper that was not a bot order.
    void OfferWhisper(Player* speaker, Player* bot, std::string const& text);
    // Party or raid line that no bot took as an order. roleFilter limits
    // who may answer ("tank", "dps", "heal", "ranged"); empty means any bot.
    void OfferGroup(Player* speaker, Group* group, std::string const& text, bool raid, std::string const& roleFilter = {});
    // Local say or yell. One nearby bot is offered the line.
    void OfferSay(Player* speaker, std::string const& text, bool yell);

    // Thread-safe. Queues the reply for the next world tick.
    void Complete(uint64 botGuid, uint64 speakerGuid, BotChatChannel channel, std::string text);

private:
    BotChat() = default;

    bool AcceptLine(std::string const& text) const;
    bool OfferTo(Player* speaker, Player* bot, BotChatChannel channel, std::string const& text);
    void Deliver(uint64 botGuid, uint64 speakerGuid, BotChatChannel channel, std::string text);
    void Remember(uint64 botGuid, std::string const& speakerName, std::string const& text);

    bool _enabled = false;
    bool _delivering = false;
    uint32 _range = 25;
    uint32 _cooldownMs = 8000;
    uint32 _memoryLines = 8;
    uint32 _maxReplyLength = 200;
    std::string _providerName = "none";
    std::unique_ptr<BotChatProvider> _provider;

    struct Pending
    {
        uint32 startedMs = 0;
    };

    struct QueuedReply
    {
        uint64 botGuid = 0;
        uint64 speakerGuid = 0;
        BotChatChannel channel = BotChatChannel::Say;
        std::string text;
    };

    std::mutex _lock;
    std::unordered_map<uint64, Pending> _pending;
    std::unordered_map<uint64, uint32> _cooldownSince;
    std::unordered_map<uint64, std::vector<std::string>> _memory;
    std::vector<QueuedReply> _replies;
};

#define sBotChat BotChat::instance()

#endif
