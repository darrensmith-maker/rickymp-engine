#ifndef OPENMW_PROCESSORPLAYERBASEINFO_HPP
#define OPENMW_PROCESSORPLAYERBASEINFO_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerBaseInfo final: public PlayerProcessor
    {
    public:
        ProcessorPlayerBaseInfo()
        {
            BPP_INIT(ID_PLAYER_BASEINFO)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_BASEINFO from server");

            if (isLocal())
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- Packet was about LocalPlayer");

                if (isRequest())
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- Requesting info");
                    packet.Send(serverAddr);
                }
                else
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- Setting character for LocalPlayer");
                    static_cast<LocalPlayer*>(player)->setCharacter();
                }
            }
            else
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- Packet was about %s", player == 0 ? "new player" : player->npc.mName.c_str());

                if (player == 0)
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- Exchanging data with new player");
                    player = PlayerList::newPlayer(guid);

                    packet.setPlayer(player);
                    packet.Read();
                }

                DedicatedPlayer* dedicatedPlayer = static_cast<DedicatedPlayer*>(player);
                LOG_APPEND(TimedLog::LOG_INFO,
                    "- Remote appearance payload: race=%s head=%s hair=%s model=%s flags=%u",
                    dedicatedPlayer->npc.mRace.c_str(), dedicatedPlayer->npc.mHead.c_str(),
                    dedicatedPlayer->npc.mHair.c_str(), dedicatedPlayer->npc.mModel.c_str(),
                    static_cast<unsigned int>(dedicatedPlayer->npc.mFlags));
                dedicatedPlayer->setBaseInfo();
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERBASEINFO_HPP
