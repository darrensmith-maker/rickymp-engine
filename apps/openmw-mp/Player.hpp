#ifndef OPENMW_PLAYER_HPP
#define OPENMW_PLAYER_HPP

#include <map>
#include <set>
#include <string>
#include <chrono>
#include <RakNetTypes.h>

#include <components/esm/npcstats.hpp>
#include <components/esm/cellid.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/esm/loadcell.hpp>

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>
#include "Cell.hpp"
#include "CellController.hpp"

typedef std::map<RakNet::RakNetGUID, Player*> TPlayers;
typedef std::map<unsigned short, Player*> TSlots;

class Players
{
public:
    static void newPlayer(RakNet::RakNetGUID guid);
    static void deletePlayer(RakNet::RakNetGUID guid);
    static Player *getPlayer(RakNet::RakNetGUID guid);
    static Player *getPlayer(unsigned short id);
    static TPlayers *getPlayers();
    static unsigned short getLastPlayerId();
    static bool doesPlayerExist(RakNet::RakNetGUID guid);

private:
    static TPlayers players;
    static TSlots slots;
};

class Player : public mwmp::BasePlayer
{
    friend class Cell;
    unsigned short id;
public:

    enum
    {
        NOTLOADED=0,
        LOADED,
        POSTLOADED,
        KICKED
    };
    Player(RakNet::RakNetGUID guid);

    unsigned short getId();
    void setId(unsigned short id);

    bool isHandshaked();
    int getHandshakeAttempts();
    void incrementHandshakeAttempts();
    void setHandshake();

    void setLoadState(int state);
    int getLoadState();

    void setVisibleToOthers(bool state);
    bool isVisibleToOthers() const;

    void setAppearanceAuthoritative(bool state);
    bool isAppearanceAuthoritative() const;

    virtual ~Player();

    CellController::TContainer *getCells();
    void sendToLoaded(mwmp::PlayerPacket *myPacket);

    // Snapshot the current AOI recipients. This is also used to remember
    // players that were sharing a cell immediately before a CellState unload.
    std::set<RakNet::RakNetGUID> getLoadedPlayerGuids() const;
    void queueCellChangeRecipient(RakNet::RakNetGUID guid);
    void sendToQueuedCellChangeRecipients(mwmp::PlayerPacket *myPacket);

    void forEachLoaded(std::function<void(Player *pl, Player *other)> func);

private:
    CellController::TContainer cells;
    int loadState;
    int handshakeCounter;
    bool visibleToOthers;
    bool appearanceAuthoritative;

    // A PlayerCellState packet can remove the old-cell observers before the
    // following PlayerCellChange packet is processed. Keep those departed AOI
    // recipients long enough to send them the cell change that despawns the
    // remote representation from their old active cell.
    std::set<RakNet::RakNetGUID> pendingCellChangeRecipients;

};

#endif //OPENMW_PLAYER_HPP
