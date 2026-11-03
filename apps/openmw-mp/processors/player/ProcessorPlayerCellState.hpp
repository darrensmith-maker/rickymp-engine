#ifndef OPENMW_PROCESSORPLAYERCELLSTATE_HPP
#define OPENMW_PROCESSORPLAYERCELLSTATE_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"
#include "apps/openmw-mp/Script/Script.hpp"
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>

namespace mwmp
{
    class ProcessorPlayerCellState : public PlayerProcessor
    {
        PlayerPacketController *playerController = nullptr;
    public:
        ProcessorPlayerCellState()
        {
            BPP_INIT(ID_PLAYER_CELL_STATE)
            // RENDER052 server safety: CellState can send a targeted CellChange
            // after the AOI shrinks. Initialize the packet controller before use;
            // leaving this pointer indeterminate causes a dedicated-server access
            // violation (read at null + 0x30) when an instanced cell is entered.
            playerController = Networking::get().getPlayerPacketController();
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            // Remember who could see this player before the CellState update.
            // Scene transitions send CellState before the subsequent CellChange;
            // without this snapshot the old-room observers disappear from
            // sendToLoaded() too early and never learn that the player left.
            const std::set<RakNet::RakNetGUID> recipientsBefore = player.getLoadedPlayerGuids();

            bool currentCellIsBeingUnloaded = false;
            const std::string currentCellDescription = player.cell.getShortDescription();
            if (!currentCellDescription.empty())
            {
                for (const auto& cellState : player.cellStateChanges)
                {
                    if (cellState.type == mwmp::CellState::UNLOAD
                        && cellState.cell.getShortDescription() == currentCellDescription)
                    {
                        currentCellIsBeingUnloaded = true;
                        break;
                    }
                }
            }

            CellController::get()->update(&player);

            const std::set<RakNet::RakNetGUID> recipientsAfter = player.getLoadedPlayerGuids();
            PlayerPacket* cellChangePacket = playerController
                ? playerController->GetPacket(ID_PLAYER_CELL_CHANGE)
                : nullptr;

            for (const RakNet::RakNetGUID& recipientGuid : recipientsBefore)
            {
                if (recipientsAfter.count(recipientGuid) != 0)
                    continue;

                if (currentCellIsBeingUnloaded)
                {
                    // The authoritative player.cell still describes the old room
                    // until ProcessorPlayerCellChange runs. Defer these recipients
                    // so that processor can send them the real destination cell.
                    player.queueCellChangeRecipient(recipientGuid);
                }
                else
                {
                    // AOI can also shrink because a surrounding exterior cell was
                    // unloaded while the player's authoritative cell stayed the
                    // same. In that case the current cell is already correct, so
                    // a targeted CellChange safely removes the stale remote now.
                    if (cellChangePacket)
                    {
                        cellChangePacket->setPlayer(&player);
                        cellChangePacket->Send(recipientGuid);
                    }
                    else
                    {
                        LOG_APPEND(TimedLog::LOG_ERROR,
                            "- PlayerPacketController unavailable while shrinking CellState AOI; "
                            "skipping targeted CellChange instead of crashing");
                    }
                }
            }

            // Cell-state packets are the authoritative AOI boundary. If the
            // player's authoritative cell itself is being unloaded, PlayerCellChange
            // has not updated player.cell yet; exchanging a full snapshot here would
            // publish the old room to the newly loaded AOI. Defer that exchange to
            // ProcessorPlayerCellChange, which already repeats it with the real cell.
            if (!currentCellIsBeingUnloaded)
                Networking::getPtr()->exchangePlayerSnapshots(&player);
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERCELLSTATE_HPP
