#ifndef OPENMW_PROCESSORPLAYERATTACK_HPP
#define OPENMW_PROCESSORPLAYERATTACK_HPP

#include "../PlayerProcessor.hpp"
#include "../../Script/Functions/Mechanics.hpp"

namespace mwmp
{
    class ProcessorPlayerAttack : public PlayerProcessor
    {
        PlayerPacketController *playerController;
    public:
        ProcessorPlayerAttack()
        {
            BPP_INIT(ID_PLAYER_ATTACK)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (!player.creatureStats.mDead)
            {
                if (player.attack.isHit && player.attack.target.isPlayer)
                {
                    Player* targetPlayer = Players::getPlayer(player.attack.target.guid);
                    if (targetPlayer != nullptr && !MechanicsFunctions::IsFriendlyFireAllowed(
                        player.getId(), targetPlayer->getId()))
                    {
                        // Keep the attack/release animation visible, but remove
                        // every gameplay result before relaying the packet.
                        player.attack.isHit = false;
                        player.attack.success = false;
                        player.attack.damage = 0.f;
                        player.attack.knockdown = false;
                        player.attack.block = false;
                        player.attack.applyWeaponEnchantment = false;
                        player.attack.applyAmmoEnchantment = false;
                    }
                }

                player.sendToLoaded(&packet);
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERATTACK_HPP
