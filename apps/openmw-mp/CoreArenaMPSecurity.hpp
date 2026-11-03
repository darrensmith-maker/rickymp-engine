#ifndef OPENMW_COREARENAMPSECURITY_HPP
#define OPENMW_COREARENAMPSECURITY_HPP

#include <cstdint>

#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>
#include <components/openmw-mp/Base/BaseWorldstate.hpp>

class Player;

namespace mwmp
{
    // Server-authoritative validation for data that must never be trusted only
    // because it came from an ArenaMP client. Client-side guards improve UX,
    // but this is the security boundary against modified clients/memory editors.
    class CoreArenaMPSecurity
    {
    public:
        static void CapturePlayerState(Player& player, std::uint8_t packetId);
        static bool ValidatePlayerPacket(Player& player, std::uint8_t packetId);
        static bool ValidatePlayerPosition(Player& player);
        static bool ValidatePlayerInventory(Player& player);
        static bool ValidateObjectPacket(Player& player, const BaseObjectList& objectList, std::uint8_t packetId);
        static bool ValidateObjectInteraction(Player& player, const BaseObjectList& objectList, std::uint8_t packetId);
        static bool ValidateActorPacket(Player& player, const BaseActorList& actorList, std::uint8_t packetId);
        static bool ValidateWorldstatePacket(Player& player, const BaseWorldstate& worldstate, std::uint8_t packetId);
        static void ResetPosition(Player& player);
        static void ForgetPlayer(Player& player);
    };
}

#endif
