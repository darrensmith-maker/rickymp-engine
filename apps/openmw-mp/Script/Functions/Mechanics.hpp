#ifndef OPENMW_MECHANICSAPI_HPP
#define OPENMW_MECHANICSAPI_HPP

#include "../Types.hpp"

#define MECHANICSAPI \
    SCRIPT_API_ENTRY("ClearAlliedPlayersForPlayer", MechanicsFunctions::ClearAlliedPlayersForPlayer),\
    SCRIPT_API_ENTRY("SetFriendlyFireMode", MechanicsFunctions::SetFriendlyFireMode),\
    SCRIPT_API_ENTRY("GetFriendlyFireMode", MechanicsFunctions::GetFriendlyFireMode),\
    SCRIPT_API_ENTRY("ArePlayersAllied", MechanicsFunctions::ArePlayersAllied),\
    SCRIPT_API_ENTRY("IsFriendlyFireAllowed", MechanicsFunctions::IsFriendlyFireAllowed),\
    \
    SCRIPT_API_ENTRY("GetMiscellaneousChangeType", MechanicsFunctions::GetMiscellaneousChangeType),\
    \
    SCRIPT_API_ENTRY("GetMarkCell", MechanicsFunctions::GetMarkCell),\
    SCRIPT_API_ENTRY("GetMarkPosX", MechanicsFunctions::GetMarkPosX),\
    SCRIPT_API_ENTRY("GetMarkPosY", MechanicsFunctions::GetMarkPosY),\
    SCRIPT_API_ENTRY("GetMarkPosZ", MechanicsFunctions::GetMarkPosZ),\
    SCRIPT_API_ENTRY("GetMarkRotX", MechanicsFunctions::GetMarkRotX),\
    SCRIPT_API_ENTRY("GetMarkRotZ", MechanicsFunctions::GetMarkRotZ),\
    SCRIPT_API_ENTRY("GetSelectedSpellId", MechanicsFunctions::GetSelectedSpellId),\
    \
    SCRIPT_API_ENTRY("DoesPlayerHavePlayerKiller", MechanicsFunctions::DoesPlayerHavePlayerKiller),\
    SCRIPT_API_ENTRY("GetPlayerKillerPid", MechanicsFunctions::GetPlayerKillerPid),\
    SCRIPT_API_ENTRY("GetPlayerKillerRefId", MechanicsFunctions::GetPlayerKillerRefId),\
    SCRIPT_API_ENTRY("GetPlayerKillerRefNum", MechanicsFunctions::GetPlayerKillerRefNum),\
    SCRIPT_API_ENTRY("GetPlayerKillerMpNum", MechanicsFunctions::GetPlayerKillerMpNum),\
    SCRIPT_API_ENTRY("GetPlayerKillerName", MechanicsFunctions::GetPlayerKillerName),\
    \
    SCRIPT_API_ENTRY("GetDrawState", MechanicsFunctions::GetDrawState),\
    SCRIPT_API_ENTRY("GetSneakState", MechanicsFunctions::GetSneakState),\
    \
    SCRIPT_API_ENTRY("SetMarkCell", MechanicsFunctions::SetMarkCell),\
    SCRIPT_API_ENTRY("SetMarkPos", MechanicsFunctions::SetMarkPos),\
    SCRIPT_API_ENTRY("SetMarkRot", MechanicsFunctions::SetMarkRot),\
    SCRIPT_API_ENTRY("SetSelectedSpellId", MechanicsFunctions::SetSelectedSpellId),\
    \
    SCRIPT_API_ENTRY("AddAlliedPlayerForPlayer", MechanicsFunctions::AddAlliedPlayerForPlayer),\
    \
    SCRIPT_API_ENTRY("SendMarkLocation", MechanicsFunctions::SendMarkLocation),\
    SCRIPT_API_ENTRY("SendSelectedSpell", MechanicsFunctions::SendSelectedSpell),\
    SCRIPT_API_ENTRY("SendAlliedPlayers", MechanicsFunctions::SendAlliedPlayers),\
    \
    SCRIPT_API_ENTRY("Jail", MechanicsFunctions::Jail),\
    SCRIPT_API_ENTRY("Resurrect", MechanicsFunctions::Resurrect),\
    \
    SCRIPT_API_ENTRY("GetDeathReason", MechanicsFunctions::GetDeathReason),\
    SCRIPT_API_ENTRY("GetPlayerKillerRefNumIndex", MechanicsFunctions::GetPlayerKillerRefNumIndex)

class MechanicsFunctions
{
public:

    /**
    * \brief Set the server-wide friendly fire mode.
    *
    * Supported canonical values are "disabled", "enabled" and "group".
    * Common aliases such as "off", "on", "party" and "allies" are accepted.
    *
    * \param mode The requested friendly fire mode.
    * \return Whether the mode was valid and has been applied.
    */
    static bool SetFriendlyFireMode(const char* mode) noexcept;

    /**
    * \brief Get the current server-wide friendly fire mode.
    *
    * \return One of "disabled", "enabled" or "group".
    */
    static const char* GetFriendlyFireMode() noexcept;

    /**
    * \brief Check whether two connected players are in the same alliance/group.
    *
    * The check is intentionally symmetric: either player's ally list is enough
    * to regard the pair as grouped. This protects players while alliance updates
    * are still propagating to all clients.
    *
    * \param firstPid The first player ID.
    * \param secondPid The second player ID.
    * \return Whether the players are grouped.
    */
    static bool ArePlayersAllied(unsigned short firstPid, unsigned short secondPid) noexcept;

    /**
    * \brief Check whether one connected player may damage another according to
    *        the current friendly fire mode.
    *
    * Self-targeted effects are always allowed. In "disabled" mode all damage
    * between different players is denied. In "enabled" mode it is always
    * allowed. In "group" mode it is denied only between allied players.
    *
    * \param attackerPid The attacking/casting player ID.
    * \param targetPid The target player ID.
    * \return Whether player-to-player damage is allowed.
    */
    static bool IsFriendlyFireAllowed(unsigned short attackerPid, unsigned short targetPid) noexcept;

    /**
    * \brief Clear the list of players who will be regarded as being player's allies.
    *
    * \param pid The player ID.
    * \return void
    */
    static void ClearAlliedPlayersForPlayer(unsigned short pid) noexcept;

    /**
    * \brief Get the type of a PlayerMiscellaneous packet.
    *
    * \param pid The player ID.
    * \return The type.
    */
    static unsigned char GetMiscellaneousChangeType(unsigned short pid) noexcept;

    /**
    * \brief Get the cell description of a player's Mark cell.
    *
    * \param pid The player ID.
    * \return The cell description.
    */
    static const char *GetMarkCell(unsigned short pid) noexcept;

    /**
    * \brief Get the X position of a player's Mark.
    *
    * \param pid The player ID.
    * \return The X position.
    */
    static double GetMarkPosX(unsigned short pid) noexcept;

    /**
    * \brief Get the Y position of a player's Mark.
    *
    * \param pid The player ID.
    * \return The Y position.
    */
    static double GetMarkPosY(unsigned short pid) noexcept;

    /**
    * \brief Get the Z position of a player's Mark.
    *
    * \param pid The player ID.
    * \return The Z position.
    */
    static double GetMarkPosZ(unsigned short pid) noexcept;

    /**
    * \brief Get the X rotation of a player's Mark.
    *
    * \param pid The player ID.
    * \return The X rotation.
    */
    static double GetMarkRotX(unsigned short pid) noexcept;

    /**
    * \brief Get the Z rotation of a player's Mark.
    *
    * \param pid The player ID.
    * \return The X rotation.
    */
    static double GetMarkRotZ(unsigned short pid) noexcept;

    /**
    * \brief Get the ID of a player's selected spell.
    *
    * \param pid The player ID.
    * \return The spell ID.
    */
    static const char *GetSelectedSpellId(unsigned short pid) noexcept;

    /**
    * \brief Check whether the killer of a certain player is also a player.
    *
    * \param pid The player ID of the killed player.
    * \return Whether the player was killed by another player.
    */
    static bool DoesPlayerHavePlayerKiller(unsigned short pid) noexcept;

    /**
    * \brief Get the player ID of the killer of a certain player.
    *
    * \param pid The player ID of the killed player.
    * \return The player ID of the killer.
    */
    static int GetPlayerKillerPid(unsigned short pid) noexcept;

    /**
    * \brief Get the refId of the actor killer of a certain player.
    *
    * \param pid The player ID of the killed player.
    * \return The refId of the killer.
    */
    static const char *GetPlayerKillerRefId(unsigned short pid) noexcept;

    /**
    * \brief Get the refNum of the actor killer of a certain player.
    *
    * \param pid The player ID of the killed player.
    * \return The refNum of the killer.
    */
    static unsigned int GetPlayerKillerRefNum(unsigned short pid) noexcept;

    /**
    * \brief Get the mpNum of the actor killer of a certain player.
    *
    * \param pid The player ID of the killed player.
    * \return The mpNum of the killer.
    */
    static unsigned int GetPlayerKillerMpNum(unsigned short pid) noexcept;

    /**
    * \brief Get the name of the actor killer of a certain player.
    *
    * \param pid The player ID of the killed player.
    * \return The name of the killer.
    */
    static const char *GetPlayerKillerName(unsigned short pid) noexcept;

    /**
    * \brief Get the draw state of a player (0 for nothing, 1 for drawn weapon,
    *        2 for drawn spell).
    *
    * \param pid The player ID.
    * \return The draw state.
    */
    static unsigned int GetDrawState(unsigned short pid) noexcept;

    /**
    * \brief Get the sneak state of a player.
    *
    * \param pid The player ID.
    * \return Whether the player is sneaking.
    */
    static bool GetSneakState(unsigned short pid) noexcept;

    /**
    * \brief Set the Mark cell of a player.
    *
    * This changes the Mark cell recorded for that player in the server memory, but does not by itself
    * send a packet.
    *
    * The cell is determined to be an exterior cell if it fits the pattern of a number followed
    * by a comma followed by another number.
    *
    * \param pid The player ID.
    * \param cellDescription The cell description.
    * \return void
    */
    static void SetMarkCell(unsigned short pid, const char *cellDescription) noexcept;

    /**
    * \brief Set the Mark position of a player.
    *
    * This changes the Mark positional coordinates recorded for that player in the server memory, but
    * does not by itself send a packet.
    *
    * \param pid The player ID.
    * \param x The X position.
    * \param y The Y position.
    * \param z The Z position.
    * \return void
    */
    static void SetMarkPos(unsigned short pid, double x, double y, double z) noexcept;

    /**
    * \brief Set the Mark rotation of a player.
    *
    * This changes the Mark positional coordinates recorded for that player in the server memory, but
    * does not by itself send a packet.
    *
    * \param pid The player ID.
    * \param x The X rotation.
    * \param z The Z rotation.
    * \return void
    */
    static void SetMarkRot(unsigned short pid, double x, double z) noexcept;

    /**
    * \brief Set the ID of a player's selected spell.
    *
    * This changes the spell ID recorded for that player in the server memory, but does not by itself
    * send a packet.
    *
    * \param pid The player ID.
    * \param spellId The spell ID.
    * \return void
    */
    static void SetSelectedSpellId(unsigned short pid, const char *spellId) noexcept;

    /**
    * \brief Add an ally to a player's list of allied players.
    *
    * \param pid The player ID.
    * \param alliedPlayerPid The ally's player ID.
    * \return void
    */
    static void AddAlliedPlayerForPlayer(unsigned short pid, unsigned short alliedPlayerPid) noexcept;

    /**
    * \brief Send a PlayerMiscellaneous packet with a Mark location to a player.
    *
    * \param pid The player ID.
    * \return void
    */
    static void SendMarkLocation(unsigned short pid);

    /**
    * \brief Send a PlayerMiscellaneous packet with a selected spell ID to a player.
    *
    * \param pid The player ID.
    * \return void
    */
    static void SendSelectedSpell(unsigned short pid);

    /**
    * \brief Send a PlayerAlly packet with a list of team member IDs to a player.
    *
    * \param pid The player ID.
    * \param sendToOtherPlayers Whether this packet should be sent to players other than the
    *                           player attached to the packet (false by default).
    * \return void
    */
    static void SendAlliedPlayers(unsigned short pid, bool sendToOtherPlayers);

    /**
    * \brief Send a PlayerJail packet about a player.
    *
    * This is similar to the player being jailed by a guard, but provides extra parameters for
    * increased flexibility.
    *
    * It is only sent to the player being jailed, as the other players will be informed of the
    * jailing's actual consequences via other packets sent by the affected client.
    *
    * \param pid The player ID.
    * \param jailDays The number of days to spend jailed, where each day affects one skill point.
    * \param ignoreJailTeleportation Whether the player being teleported to the nearest jail
    *                                marker should be overridden.
    * \param ignoreJailSkillIncreases Whether the player's Sneak and Security skills should be
    *                                 prevented from increasing as a result of the jailing,
    *                                 overriding default behavior.
    * \param jailProgressText The text that should be displayed while jailed.
    * \param jailEndText The text that should be displayed once the jailing period is over.
    * \return void
    */
    static void Jail(unsigned short pid, int jailDays, bool ignoreJailTeleportation, bool ignoreJailSkillIncreases,
                     const char* jailProgressText, const char* jailEndText) noexcept;

    /**
    * \brief Send a PlayerResurrect packet about a player.
    *
    * This sends the packet to all players connected to the server.
    *
    * \param pid The player ID.
    * \param type The type of resurrection (0 for REGULAR, 1 for IMPERIAL_SHRINE,
    *             2 for TRIBUNAL_TEMPLE).
    * \return void
    */
    static void Resurrect(unsigned short pid, unsigned int type) noexcept;

    // All methods below are deprecated versions of methods from above

    static const char *GetDeathReason(unsigned short pid) noexcept;
    static unsigned int GetPlayerKillerRefNumIndex(unsigned short pid) noexcept;

};

#endif //OPENMW_MECHANICSAPI_HPP
