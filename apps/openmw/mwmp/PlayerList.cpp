#include <components/openmw-mp/TimedLog.hpp>
#include <apps/openmw/mwclass/creature.hpp>

#include "../mwbase/environment.hpp"

#include "../mwclass/npc.hpp"

#include "../mwmechanics/creaturestats.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldimp.hpp"

#include "PlayerList.hpp"
#include "Main.hpp"
#include "DedicatedPlayer.hpp"
#include "CellController.hpp"
#include "GUIController.hpp"


using namespace mwmp;

std::map <RakNet::RakNetGUID, DedicatedPlayer *> PlayerList::playerList;

void PlayerList::update(float dt)
{
    for (auto &playerEntry : playerList)
    {
        DedicatedPlayer *player = playerEntry.second;
        if (player == nullptr) continue;

        player->update(dt);
    }
}

DedicatedPlayer *PlayerList::newPlayer(RakNet::RakNetGUID guid)
{
    // Never leave a null placeholder in playerList. Historically getPlayer(guid)
    // used std::map::operator[], which could create guid -> nullptr entries when a
    // packet for a not-yet-announced remote player arrived. A later HUD/worldstate
    // cell scan then dereferenced that null entry while comparing ESM::Cell values.
    auto existing = playerList.find(guid);
    if (existing != playerList.end() && existing->second != nullptr)
        return existing->second;

    LOG_APPEND(TimedLog::LOG_INFO, "- Creating new DedicatedPlayer with guid %s", guid.ToString());

    DedicatedPlayer* player = new DedicatedPlayer(guid);
    if (existing != playerList.end())
        existing->second = player;
    else
        playerList.emplace(guid, player);

    LOG_APPEND(TimedLog::LOG_INFO, "- There are now %i DedicatedPlayers", playerList.size());

    return player;
}

void PlayerList::deletePlayer(RakNet::RakNetGUID guid)
{
    const auto it = playerList.find(guid);
    if (it == playerList.end())
        return;

    DedicatedPlayer* player = it->second;
    if (player != nullptr)
    {
        if (player->reference)
            player->deleteReference();
        delete player;
    }

    playerList.erase(it);
}

void PlayerList::cleanUp()
{
    for (auto &playerEntry : playerList)
        delete playerEntry.second;
    playerList.clear();
}

DedicatedPlayer *PlayerList::getPlayer(RakNet::RakNetGUID guid)
{
    // IMPORTANT: lookup must be non-mutating. operator[] inserts a nullptr for an
    // unknown GUID and that poisoned entry can later be dereferenced by cell scans.
    const auto it = playerList.find(guid);
    return it != playerList.end() ? it->second : nullptr;
}

DedicatedPlayer *PlayerList::getPlayer(const MWWorld::Ptr &ptr)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;
        
        std::string refId = ptr.getCellRef().getRefId();
        
        if (playerEntry.second->getPtr().getCellRef().getRefId() == refId)
            return playerEntry.second;
    }

    return nullptr;
}

DedicatedPlayer* PlayerList::getPlayer(int actorId)
{
    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        MWWorld::Ptr playerPtr = playerEntry.second->getPtr();
        int playerActorId = playerPtr.getClass().getCreatureStats(playerPtr).getActorId();

        if (actorId == playerActorId)
            return playerEntry.second;
    }

    return nullptr;
}

std::vector<RakNet::RakNetGUID> PlayerList::getPlayersInCell(const ESM::Cell& cell)
{
    std::vector<RakNet::RakNetGUID> playersInCell;

    for (auto& playerEntry : playerList)
    {
        if (playerEntry.first == RakNet::UNASSIGNED_CRABNET_GUID)
            continue;

        DedicatedPlayer* player = playerEntry.second;
        if (player == nullptr || player->getPtr().mRef == nullptr)
            continue;

        if (Main::get().getCellController()->isSameCell(cell, player->cell))
            playersInCell.push_back(playerEntry.first);
    }

    return playersInCell;
}

bool PlayerList::isDedicatedPlayer(const MWWorld::Ptr &ptr)
{
    if (ptr.mRef == nullptr)
        return false;

    // Players always have 0 as their refNum and mpNum
    if (ptr.getCellRef().getRefNum().mIndex != 0 || ptr.getCellRef().getMpNum() != 0)
        return false;

    return (getPlayer(ptr) != nullptr);
}

void PlayerList::enableMarkers(const ESM::Cell& cell)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        if (Main::get().getCellController()->isSameCell(cell, playerEntry.second->cell))
        {
            playerEntry.second->enableMarker();
        }
    }
}

/*
    Go through all DedicatedPlayers checking if their mHitAttemptActorId matches this one
    and set it to -1 if it does

    This resets the combat target for a DedicatedPlayer's followers in Actors::update()
*/
void PlayerList::clearHitAttemptActorId(int actorId)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        MWMechanics::CreatureStats &playerCreatureStats = playerEntry.second->getPtr().getClass().getCreatureStats(playerEntry.second->getPtr());

        if (playerCreatureStats.getHitAttemptActorId() == actorId)
            playerCreatureStats.setHitAttemptActorId(-1);
    }
}
