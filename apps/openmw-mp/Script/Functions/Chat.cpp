#include "Chat.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/Networking.hpp>

void ChatFunctions::SendMessage(unsigned short pid, const char *message, bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->chatMessage = message;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "System: %s", message);

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_CHAT_MESSAGE);
    packet->setPlayer(player);

    if (!skipAttachedPlayer)
        packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void ChatFunctions::SendMessageTo(unsigned short sourcePid, unsigned short targetPid, const char *message) noexcept
{
    Player *sourcePlayer = Players::getPlayer(sourcePid);
    if (sourcePlayer == nullptr)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "%s: Source player with pid '%u' not found\n", __PRETTY_FUNCTION__, sourcePid);
        return;
    }

    Player *targetPlayer = Players::getPlayer(targetPid);
    if (targetPlayer == nullptr)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "%s: Target player with pid '%u' not found\n", __PRETTY_FUNCTION__, targetPid);
        return;
    }

    sourcePlayer->chatMessage = message;

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "System to %u: %s", targetPid, message);

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_CHAT_MESSAGE);
    packet->setPlayer(sourcePlayer);
    packet->Send(targetPlayer->guid);
}

void ChatFunctions::CleanChatForPid(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player,);

    player->chatMessage.clear();

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_CHAT_MESSAGE);
    packet->setPlayer(player);

    packet->Send(false);
}

void ChatFunctions::CleanChat()
{
    for (auto player : *Players::getPlayers())
    {
        player.second->chatMessage.clear();

        mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_CHAT_MESSAGE);
        packet->setPlayer(player.second);

        packet->Send(false);
    }
}
