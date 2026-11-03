#ifndef OPENMW_CHATAPI_HPP
#define OPENMW_CHATAPI_HPP

#include "../Types.hpp"

#define CHATAPI \
    SCRIPT_API_ENTRY("SendMessage", ChatFunctions::SendMessage),\
    SCRIPT_API_ENTRY("SendMessageTo", ChatFunctions::SendMessageTo),\
    SCRIPT_API_ENTRY("CleanChatForPid", ChatFunctions::CleanChatForPid),\
    SCRIPT_API_ENTRY("CleanChat", ChatFunctions::CleanChat)

class ChatFunctions
{
public:

    /**
    * \brief Send a message to a certain player.
    *
    * \param pid The player ID.
    * \param message The contents of the message.
    * \param sendToOtherPlayers Whether this packet should be sent to players other than the
    *                           player attached to the packet (false by default).
    * \param skipAttachedPlayer Whether the packet should skip being sent to the player attached
    *                           to the packet (false by default).
    * \return void
    */
    static void SendMessage(unsigned short pid, const char *message, bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept;

    /**
    * \brief Send a chat message owned by one player to one specific recipient.
    *
    * This preserves the original attached player for chat speaker, TTS and voice
    * integrations while allowing server-side per-recipient localization.
    *
    * \param sourcePid The player attached to the chat packet.
    * \param targetPid The only player who should receive the packet.
    * \param message The contents of the message.
    */
    static void SendMessageTo(unsigned short sourcePid, unsigned short targetPid, const char *message) noexcept;

    /**
    * \brief Remove all messages from chat for a certain player.
    *
    * \param pid The player ID.
    * \return void
    */
    static void CleanChatForPid(unsigned short pid);

    /**
    * \brief Remove all messages from chat for everyone on the server.
    *
    * \return void
    */
    static void CleanChat();
};

#endif //OPENMW_CHATAPI_HPP
