#include "Mechanics.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/Networking.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>

static std::string tempCellDescription;

namespace
{
    std::string friendlyFireMode = "group";

    std::string normalizeFriendlyFireMode(const char* mode)
    {
        if (mode == nullptr)
            return {};

        std::string normalized(mode);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

        if (normalized == "disabled" || normalized == "off" || normalized == "false" || normalized == "0")
            return "disabled";
        if (normalized == "enabled" || normalized == "on" || normalized == "true" || normalized == "1")
            return "enabled";
        if (normalized == "group" || normalized == "party" || normalized == "allies" || normalized == "ally")
            return "group";

        return {};
    }

    bool playerHasAlly(const Player* player, const RakNet::RakNetGUID& allyGuid)
    {
        return player != nullptr && std::find(player->alliedPlayers.begin(), player->alliedPlayers.end(), allyGuid)
            != player->alliedPlayers.end();
    }
}

bool MechanicsFunctions::SetFriendlyFireMode(const char* mode) noexcept
{
    const std::string normalizedMode = normalizeFriendlyFireMode(mode);
    if (normalizedMode.empty())
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "SetFriendlyFireMode: Invalid mode '%s'; expected disabled, enabled or group",
            mode != nullptr ? mode : "(null)");
        return false;
    }

    friendlyFireMode = normalizedMode;
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Friendly fire mode set to %s", friendlyFireMode.c_str());
    return true;
}

const char* MechanicsFunctions::GetFriendlyFireMode() noexcept
{
    return friendlyFireMode.c_str();
}

bool MechanicsFunctions::ArePlayersAllied(unsigned short firstPid, unsigned short secondPid) noexcept
{
    Player* firstPlayer = Players::getPlayer(firstPid);
    Player* secondPlayer = Players::getPlayer(secondPid);

    if (firstPlayer == nullptr || secondPlayer == nullptr)
        return false;

    if (firstPlayer == secondPlayer)
        return true;

    return playerHasAlly(firstPlayer, secondPlayer->guid) || playerHasAlly(secondPlayer, firstPlayer->guid);
}

bool MechanicsFunctions::IsFriendlyFireAllowed(unsigned short attackerPid, unsigned short targetPid) noexcept
{
    Player* attacker = Players::getPlayer(attackerPid);
    Player* target = Players::getPlayer(targetPid);

    if (attacker == nullptr || target == nullptr)
        return false;

    if (attacker == target)
        return true;

    if (friendlyFireMode == "enabled")
        return true;
    if (friendlyFireMode == "disabled")
        return false;

    return !ArePlayersAllied(attackerPid, targetPid);
}

void MechanicsFunctions::ClearAlliedPlayersForPlayer(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->alliedPlayers.clear();
}

unsigned char MechanicsFunctions::GetMiscellaneousChangeType(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->miscellaneousChangeType;
}

const char *MechanicsFunctions::GetMarkCell(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    tempCellDescription = player->cell.getShortDescription().c_str();
    return tempCellDescription.c_str();
}

double MechanicsFunctions::GetMarkPosX(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->markPosition.pos[0];
}

double MechanicsFunctions::GetMarkPosY(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->markPosition.pos[1];
}

double MechanicsFunctions::GetMarkPosZ(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->markPosition.pos[2];
}

double MechanicsFunctions::GetMarkRotX(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->markPosition.rot[0];
}

double MechanicsFunctions::GetMarkRotZ(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0.0f);

    return player->markPosition.rot[2];
}

bool MechanicsFunctions::DoesPlayerHavePlayerKiller(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->killer.isPlayer;
}

int MechanicsFunctions::GetPlayerKillerPid(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    Player *killer = Players::getPlayer(player->killer.guid);

    if (killer != nullptr)
        return killer->getId();

    return -1;
}

const char *MechanicsFunctions::GetPlayerKillerRefId(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, "");

    return player->killer.refId.c_str();
}

unsigned int MechanicsFunctions::GetPlayerKillerRefNum(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->killer.refNum;
}

unsigned int MechanicsFunctions::GetPlayerKillerMpNum(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->killer.mpNum;
}

const char *MechanicsFunctions::GetPlayerKillerName(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, "");

    return player->killer.name.c_str();
}

const char *MechanicsFunctions::GetSelectedSpellId(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->selectedSpellId.c_str();
}

unsigned int MechanicsFunctions::GetDrawState(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->drawState;
}

bool MechanicsFunctions::GetSneakState(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, false);

    // TODO: Avoid having to use a magic number here
    return (player->movementFlags & 8) != 0;
}

void MechanicsFunctions::SetMarkCell(unsigned short pid, const char *cellDescription) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->markCell = Utils::getCellFromDescription(cellDescription);
}

void MechanicsFunctions::SetMarkPos(unsigned short pid, double x, double y, double z) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->markPosition.pos[0] = x;
    player->markPosition.pos[1] = y;
    player->markPosition.pos[2] = z;
}

void MechanicsFunctions::SetMarkRot(unsigned short pid, double x, double z) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->markPosition.rot[0] = x;
    player->markPosition.rot[2] = z;
}

void MechanicsFunctions::SetSelectedSpellId(unsigned short pid, const char *spellId) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->selectedSpellId = spellId;
}

void MechanicsFunctions::AddAlliedPlayerForPlayer(unsigned short pid, unsigned short alliedPlayerPid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    Player *alliedPlayer = Players::getPlayer(alliedPlayerPid);
    if (alliedPlayer == nullptr)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
            "%s: Allied player with pid '%u' not found\n", __PRETTY_FUNCTION__, alliedPlayerPid);
        return;
    }

    player->alliedPlayers.push_back(alliedPlayer->guid);
}

void MechanicsFunctions::SendMarkLocation(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::MARK_LOCATION;

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_MISCELLANEOUS);
    packet->setPlayer(player);
    
    packet->Send(false);
}

void MechanicsFunctions::SendSelectedSpell(unsigned short pid)
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->miscellaneousChangeType = mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_SPELL;

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_MISCELLANEOUS);
    packet->setPlayer(player);
    
    packet->Send(false);
}

void MechanicsFunctions::SendAlliedPlayers(unsigned short pid, bool sendToOtherPlayers)
{
    Player *player;
    GET_PLAYER(pid, player, );

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_ALLY);
    packet->setPlayer(player);

    packet->Send(false);
    if (sendToOtherPlayers)
        packet->Send(true);
}

void MechanicsFunctions::Jail(unsigned short pid, int jailDays, bool ignoreJailTeleportation, bool ignoreJailSkillIncreases,
                              const char* jailProgressText, const char* jailEndText) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->jailDays = jailDays;
    player->ignoreJailTeleportation = ignoreJailTeleportation;
    player->ignoreJailSkillIncreases = ignoreJailSkillIncreases;
    player->jailProgressText = jailProgressText;
    player->jailEndText = jailEndText;

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_JAIL);
    packet->setPlayer(player);
    
    packet->Send(false);
}

void MechanicsFunctions::Resurrect(unsigned short pid, unsigned int type) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    player->resurrectType = type;

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_RESURRECT);
    packet->setPlayer(player);

    packet->Send(false);
    packet->Send(true);
}

// All methods below are deprecated versions of methods from above

const char *MechanicsFunctions::GetDeathReason(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    if (player->killer.isPlayer)
    {
        Player *killerPlayer = Players::getPlayer(player->killer.guid);

        if (killerPlayer != nullptr)
            return killerPlayer->npc.mName.c_str();
    }
    else if (!player->killer.name.empty())
        return player->killer.name.c_str();

    return "suicide";
}

unsigned int MechanicsFunctions::GetPlayerKillerRefNumIndex(unsigned short pid) noexcept
{
    return GetPlayerKillerRefNum(pid);
}
