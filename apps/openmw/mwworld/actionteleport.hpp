#ifndef GAME_MWWORLD_ACTIONTELEPORT_H
#define GAME_MWWORLD_ACTIONTELEPORT_H

#include <set>
#include <string>

#include <components/esm/defs.hpp>

#include "action.hpp"

namespace MWWorld
{
    class ActionTeleport : public Action
    {
            std::string mCellName;
            ESM::Position mPosition;
            bool mTeleportFollowers;

            /// Teleports this actor and also teleports anyone following that actor.
            void executeImp (const Ptr& actor) override;

            /// Teleports only the given actor (internal use).
            void teleport(const Ptr& actor, const Ptr& teleportTarget = Ptr(), bool returnHome = false);

        public:

            /// If cellName is empty, an exterior cell is assumed.
            /// @param teleportFollowers Whether to teleport any following actors of the target actor as well.
            ActionTeleport (const std::string& cellName, const ESM::Position& position, bool teleportFollowers);

            /// @param includeHostiles If true, include nearby hostile actors whose active AiCombat
            ///                        package follows this target through teleport doors.
            static void getFollowers(const MWWorld::Ptr& actor, std::set<MWWorld::Ptr>& out, bool includeHostiles = false);

            /// Defer a non-player cell move until mechanics finished iterating actors.
            /// requiredCombatTargetId keeps delayed pursuers from entering after combat already ended.
            static void queueDelayedTeleport(const MWWorld::Ptr& actor, const std::string& cellName,
                const ESM::Position& position, float delay, int requiredCombatTargetId = -1, bool returnHome = false);
            static void updateDelayedTeleports(float duration);
            static void clearDelayedTeleports();
    };
}

#endif
