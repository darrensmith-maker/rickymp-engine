#ifndef OPENMW_PROCESSORPLAYERCELLCHANGE_HPP
#define OPENMW_PROCESSORPLAYERCELLCHANGE_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"
#include "apps/openmw-mp/Script/Script.hpp"
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>

namespace mwmp
{
    class ProcessorPlayerCellChange : public PlayerProcessor
    {
        PlayerPacketController *playerController;
    public:
        ProcessorPlayerCellChange()
        {
            BPP_INIT(ID_PLAYER_CELL_CHANGE)
            playerController = Networking::get().getPlayerPacketController();
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());
            LOG_APPEND(TimedLog::LOG_INFO, "- Moved to %s", player.cell.getShortDescription().c_str());

            Script::Call<Script::CallbackIdentity("OnPlayerCellChange")>(player.getId());

            Networking::getPtr()->exchangePlayerSnapshots(&player);

            if (player.isVisibleToOthers())
            {
                PlayerPacket* positionPacket = playerController->GetPacket(ID_PLAYER_POSITION);
                positionPacket->setPlayer(&player);
                player.sendToLoaded(positionPacket);

                // Do not broadcast private/instanced cell names globally. Only
                // clients sharing a loaded-cell AOI may move this DedicatedPlayer.
                packet.setPlayer(&player);
                player.sendToLoaded(&packet);

                // CellState is processed first during a scene transition. It may
                // have already removed players that remained in the previous room
                // from this player's loaded-cell AOI. Send the same reliable cell
                // change to those departed observers so their DedicatedPlayer is
                // moved out of the old active scene instead of remaining at the door.
                player.sendToQueuedCellChangeRecipients(&packet);
            }

            LOG_APPEND(TimedLog::LOG_INFO, "- Finished processing ID_PLAYER_CELL_CHANGE", player.cell.getShortDescription().c_str());

        }
    };
}

#endif //OPENMW_PROCESSORPLAYERCELLCHANGE_HPP
