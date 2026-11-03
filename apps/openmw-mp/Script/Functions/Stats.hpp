#ifndef OPENMW_STATAPI_HPP
#define OPENMW_STATAPI_HPP

#define STATAPI \
    SCRIPT_API_ENTRY("GetAttributeCount", StatsFunctions::GetAttributeCount),\
    SCRIPT_API_ENTRY("GetSkillCount", StatsFunctions::GetSkillCount),\
    SCRIPT_API_ENTRY("GetAttributeId", StatsFunctions::GetAttributeId),\
    SCRIPT_API_ENTRY("GetSkillId", StatsFunctions::GetSkillId),\
    SCRIPT_API_ENTRY("GetAttributeName", StatsFunctions::GetAttributeName),\
    SCRIPT_API_ENTRY("GetSkillName", StatsFunctions::GetSkillName),\
    \
    SCRIPT_API_ENTRY("GetName", StatsFunctions::GetName),\
    SCRIPT_API_ENTRY("GetRace", StatsFunctions::GetRace),\
    SCRIPT_API_ENTRY("GetHead", StatsFunctions::GetHead),\
    SCRIPT_API_ENTRY("GetHair", StatsFunctions::GetHairstyle),\
    SCRIPT_API_ENTRY("GetIsMale", StatsFunctions::GetIsMale),\
    SCRIPT_API_ENTRY("GetModel", StatsFunctions::GetModel),\
    SCRIPT_API_ENTRY("GetBirthsign", StatsFunctions::GetBirthsign),\
    SCRIPT_API_ENTRY("GetLanguage", StatsFunctions::GetLanguage),\
    \
    SCRIPT_API_ENTRY("GetLevel", StatsFunctions::GetLevel),\
    SCRIPT_API_ENTRY("GetLevelProgress", StatsFunctions::GetLevelProgress),\
    \
    SCRIPT_API_ENTRY("GetHealthBase", StatsFunctions::GetHealthBase),\
    SCRIPT_API_ENTRY("GetHealthCurrent", StatsFunctions::GetHealthCurrent),\
    \
    SCRIPT_API_ENTRY("GetMagickaBase", StatsFunctions::GetMagickaBase),\
    SCRIPT_API_ENTRY("GetMagickaCurrent", StatsFunctions::GetMagickaCurrent),\
    \
    SCRIPT_API_ENTRY("GetFatigueBase", StatsFunctions::GetFatigueBase),\
    SCRIPT_API_ENTRY("GetFatigueCurrent", StatsFunctions::GetFatigueCurrent),\
    \
    SCRIPT_API_ENTRY("GetAttributeBase", StatsFunctions::GetAttributeBase),\
    SCRIPT_API_ENTRY("GetAttributeModifier", StatsFunctions::GetAttributeModifier),\
    SCRIPT_API_ENTRY("GetAttributeDamage", StatsFunctions::GetAttributeDamage),\
    \
    SCRIPT_API_ENTRY("GetSkillBase", StatsFunctions::GetSkillBase),\
    SCRIPT_API_ENTRY("GetSkillModifier", StatsFunctions::GetSkillModifier),\
    SCRIPT_API_ENTRY("GetSkillDamage", StatsFunctions::GetSkillDamage),\
    SCRIPT_API_ENTRY("GetSkillProgress", StatsFunctions::GetSkillProgress),\
    SCRIPT_API_ENTRY("GetSkillIncrease", StatsFunctions::GetSkillIncrease),\
    \
    SCRIPT_API_ENTRY("GetBounty", StatsFunctions::GetBounty),\
    \
    SCRIPT_API_ENTRY("SetName", StatsFunctions::SetName),\
    SCRIPT_API_ENTRY("SetRace", StatsFunctions::SetRace),\
    SCRIPT_API_ENTRY("SetHead", StatsFunctions::SetHead),\
    SCRIPT_API_ENTRY("SetHair", StatsFunctions::SetHairstyle),\
    SCRIPT_API_ENTRY("SetIsMale", StatsFunctions::SetIsMale),\
    SCRIPT_API_ENTRY("SetModel", StatsFunctions::SetModel),\
    SCRIPT_API_ENTRY("SetBirthsign", StatsFunctions::SetBirthsign),\
    SCRIPT_API_ENTRY("SetResetStats", StatsFunctions::SetResetStats),\
    \
    SCRIPT_API_ENTRY("SetLevel", StatsFunctions::SetLevel),\
    SCRIPT_API_ENTRY("SetLevelProgress", StatsFunctions::SetLevelProgress),\
    \
    SCRIPT_API_ENTRY("SetHealthBase", StatsFunctions::SetHealthBase),\
    SCRIPT_API_ENTRY("SetHealthCurrent", StatsFunctions::SetHealthCurrent),\
    SCRIPT_API_ENTRY("SetMagickaBase", StatsFunctions::SetMagickaBase),\
    SCRIPT_API_ENTRY("SetMagickaCurrent", StatsFunctions::SetMagickaCurrent),\
    SCRIPT_API_ENTRY("SetFatigueBase", StatsFunctions::SetFatigueBase),\
    SCRIPT_API_ENTRY("SetFatigueCurrent", StatsFunctions::SetFatigueCurrent),\
    \
    SCRIPT_API_ENTRY("SetAttributeBase", StatsFunctions::SetAttributeBase),\
    SCRIPT_API_ENTRY("ClearAttributeModifier", StatsFunctions::ClearAttributeModifier),\
    SCRIPT_API_ENTRY("SetAttributeDamage", StatsFunctions::SetAttributeDamage),\
    \
    SCRIPT_API_ENTRY("SetSkillBase", StatsFunctions::SetSkillBase),\
    SCRIPT_API_ENTRY("ClearSkillModifier", StatsFunctions::ClearSkillModifier),\
    SCRIPT_API_ENTRY("SetSkillDamage", StatsFunctions::SetSkillDamage),\
    SCRIPT_API_ENTRY("SetSkillProgress", StatsFunctions::SetSkillProgress),\
    SCRIPT_API_ENTRY("SetSkillIncrease", StatsFunctions::SetSkillIncrease),\
    \
    SCRIPT_API_ENTRY("SetBounty", StatsFunctions::SetBounty),\
    SCRIPT_API_ENTRY("SetCharGenStage", StatsFunctions::SetCharGenStage),\
    SCRIPT_API_ENTRY("SetPlayerVisible", StatsFunctions::SetPlayerVisible),\
    \
    SCRIPT_API_ENTRY("SendBaseInfo", StatsFunctions::SendBaseInfo),\
    \
    SCRIPT_API_ENTRY("SendStatsDynamic", StatsFunctions::SendStatsDynamic),\
    SCRIPT_API_ENTRY("SendAttributes", StatsFunctions::SendAttributes),\
    SCRIPT_API_ENTRY("SendSkills", StatsFunctions::SendSkills),\
    SCRIPT_API_ENTRY("SendLevel", StatsFunctions::SendLevel),\
    SCRIPT_API_ENTRY("SendBounty", StatsFunctions::SendBounty)

class StatsFunctions
{
public:

    /**
    * \brief Get the number of attributes.
    *
    * The number is 8 before any dehardcoding is done in OpenMW.
    *
    * \return The number of attributes.
    */
    static int GetAttributeCount() noexcept;

    /**
    * \brief Get the number of skills.
    *
    * The number is 27 before any dehardcoding is done in OpenMW.
    *
    * \return The number of skills.
    */
    static int GetSkillCount() noexcept;

    /**
    * \brief Get the numerical ID of an attribute with a certain name.
    *
    * If an invalid name is used, the ID returned is -1
    *
    * \param name The name of the attribute.
    * \return The ID of the attribute.
    */
    static int GetAttributeId(const char *name) noexcept;

    /**
    * \brief Get the numerical ID of a skill with a certain name.
    *
    * If an invalid name is used, the ID returned is -1
    *
    * \param name The name of the skill.
    * \return The ID of the skill.
    */
    static int GetSkillId(const char *name) noexcept;

    /**
    * \brief Get the name of the attribute with a certain numerical ID.
    *
    * If an invalid ID is used, "invalid" is returned.
    *
    * \param attributeId The ID of the attribute.
    * \return The name of the attribute.
    */
    static const char *GetAttributeName(unsigned short attributeId) noexcept;

    /**
    * \brief Get the name of the skill with a certain numerical ID.
    *
    * If an invalid ID is used, "invalid" is returned.
    *
    * \param skillId The ID of the skill.
    * \return The name of the skill.
    */
    static const char *GetSkillName(unsigned short skillId) noexcept;

    /**
    * \brief Get the name of a player.
    *
    * \param pid The player ID.
    * \return The name of the player.
    */
    static const char *GetName(unsigned short pid) noexcept;

    /**
    * \brief Get the race of a player.
    *
    * \param pid The player ID.
    * \return The race of the player.
    */
    static const char *GetRace(unsigned short pid) noexcept;

    /**
    * \brief Get the head mesh used by a player.
    *
    * \param pid The player ID.
    * \return The head mesh of the player.
    */
    static const char *GetHead(unsigned short pid) noexcept;

    /**
    * \brief Get the hairstyle mesh used by a player.
    *
    * \param pid The player ID.
    * \return The hairstyle mesh of the player.
    */
    static const char *GetHairstyle(unsigned short pid) noexcept;

    /**
    * \brief Check whether a player is male or not.
    *
    * \param pid The player ID.
    * \return Whether the player is male.
    */
    static int GetIsMale(unsigned short pid) noexcept;

    /**
    * \brief Get the model of a player.
    *
    * \param pid The player ID.
    * \return The model of the player.
    */
    static const char* GetModel(unsigned short pid) noexcept;

    /**
    * \brief Get the birthsign of a player.
    *
    * \param pid The player ID.
    * \return The birthsign of the player.
    */
    static const char *GetBirthsign(unsigned short pid) noexcept;

    /**
    * \brief Get the normalized language reported by a player client.
    *
    * \param pid The player ID.
    * \return "RU" for Russian clients, otherwise "EN".
    */
    static const char *GetLanguage(unsigned short pid) noexcept;

    /**
    * \brief Get the character level of a player.
    *
    * \param pid The player ID.
    * \return The level of the player.
    */
    static int GetLevel(unsigned short pid) noexcept;

    /**
    * \brief Get the player's progress to their next character level.
    *
    * \param pid The player ID.
    * \return The level progress.
    */
    static int GetLevelProgress(unsigned short pid) noexcept;

    /**
    * \brief Get the base health of the player.
    *
    * \param pid The player ID.
    * \return The base health.
    */
    static double GetHealthBase(unsigned short pid) noexcept;

    /**
    * \brief Get the current health of the player.
    *
    * \param pid The player ID.
    * \return The current health.
    */
    static double GetHealthCurrent(unsigned short pid) noexcept;

    /**
    * \brief Get the base magicka of the player.
    *
    * \param pid The player ID.
    * \return The base magicka.
    */
    static double GetMagickaBase(unsigned short pid) noexcept;

    /**
    * \brief Get the current magicka of the player.
    *
    * \param pid The player ID.
    * \return The current magicka.
    */
    static double GetMagickaCurrent(unsigned short pid) noexcept;

    /**
    * \brief Get the base fatigue of the player.
    *
    * \param pid The player ID.
    * \return The base fatigue.
    */
    static double GetFatigueBase(unsigned short pid) noexcept;

    /**
    * \brief Get the current fatigue of the player.
    *
    * \param pid The player ID.
    * \return The current fatigue.
    */
    static double GetFatigueCurrent(unsigned short pid) noexcept;

    /**
    * \brief Get the base value of a player's attribute.
    *
    * \param pid The player ID.
    * \param attributeId The attribute ID.
    * \return The base value of the attribute.
    */
    static int GetAttributeBase(unsigned short pid, unsigned short attributeId) noexcept;

    /**
    * \brief Get the modifier value of a player's attribute.
    *
    * \param pid The player ID.
    * \param attributeId The attribute ID.
    * \return The modifier value of the attribute.
    */
    static int GetAttributeModifier(unsigned short pid, unsigned short attributeId) noexcept;

    /**
    * \brief Get the amount of damage (as caused through the Damage Attribute effect)
    *        to a player's attribute.
    *
    * \param pid The player ID.
    * \param attributeId The attribute ID.
    * \return The amount of damage to the attribute.
    */
    static double GetAttributeDamage(unsigned short pid, unsigned short attributeId) noexcept;

    /**
    * \brief Get the base value of a player's skill.
    *
    * \param pid The player ID.
    * \param skillId The skill ID.
    * \return The base value of the skill.
    */
    static int GetSkillBase(unsigned short pid, unsigned short skillId) noexcept;

    /**
    * \brief Get the modifier value of a player's skill.
    *
    * \param pid The player ID.
    * \param skillId The skill ID.
    * \return The modifier value of the skill.
    */
    static int GetSkillModifier(unsigned short pid, unsigned short skillId) noexcept;

    /**
    * \brief Get the amount of damage (as caused through the Damage Skill effect)
    *        to a player's skill.
    *
    * \param pid The player ID.
    * \param skillId The skill ID.
    * \return The amount of damage to the skill.
    */
    static double GetSkillDamage(unsigned short pid, unsigned short skillId) noexcept;

    /**
    * \brief Get the progress the player has made towards increasing a certain skill by 1.
    *
    * \param pid The player ID.
    * \param skillId The skill ID.
    * \return The skill progress.
    */
    static double GetSkillProgress(unsigned short pid, unsigned short skillId) noexcept;

    /**
    * \brief Get the bonus applied to a certain attribute at the next level up as a result
    *        of associated skill increases.
    *
    * Although confusing, the term "skill increase" for this is taken from OpenMW itself.
    *
    * \param pid The player ID.
    * \param attributeId The attribute ID.
    * \return The increase in the attribute caused by skills.
    */
    static int GetSkillIncrease(unsigned short pid, unsigned int attributeId) noexcept;

    /**
    * \brief Get the bounty of the player.
    *
    * \param pid The player ID.
    * \return The bounty.
    */
    static int GetBounty(unsigned short pid) noexcept;

    /**
    * \brief Set the name of a player.
    *
    * \param pid The player ID.
    * \param name The new name of the player.
    * \return void
    */
    static void SetName(unsigned short pid, const char *name) noexcept;

    /**
    * \brief Set the race of a player.
    *
    * \param pid The player ID.
    * \param race The new race of the player.
    * \return void
    */
    static void SetRace(unsigned short pid, const char *race) noexcept;

    /**
    * \brief Set the head mesh used by a player.
    *
    * \param pid The player ID.
    * \param head The new head mesh of the player.
    * \return void
    */
    static void SetHead(unsigned short pid, const char *head) noexcept;

    /**
    * \brief Set the hairstyle mesh used by a player.
    *
    * \param pid The player ID.
    * \param hairstyle The new hairstyle mesh of the player.
    * \return void
    */
    static void SetHairstyle(unsigned short pid, const char *hairstyle) noexcept;

    /**
    * \brief Set whether a player is male or not.
    *
    * \param pid The player ID.
    * \param state Whether the player is male.
    * \return void
    */
    static void SetIsMale(unsigned short pid, int state) noexcept;

    /**
    * \brief Set the model of a player.
    *
    * \param pid The player ID.
    * \param model The new model of the player.
    * \return void
    */
    static void SetModel(unsigned short pid, const char *model) noexcept;

    /**
    * \brief Set the birthsign of a player.
    *
    * \param pid The player ID.
    * \param name The new birthsign of the player.
    * \return void
    */
    static void SetBirthsign(unsigned short pid, const char *name) noexcept;

    /**
    * \brief Set whether the player's stats should be reset based on their
    *        current race as the result of a PlayerBaseInfo packet.
    *
    * This changes the resetState for that player in the server memory, but does not by itself
    * send a packet.
    *
    * \param pid The player ID.
    * \param resetStats The stat reset state.
    * \return void
    */
    static void SetResetStats(unsigned short pid, bool resetStats) noexcept;
    
    /**
    * \brief Set the character level of a player.
    *
    * \param pid The player ID.
    * \param value The new level of the player.
    * \return void
    */
    static void SetLevel(unsigned short pid, int value) noexcept;

    /**
    * \brief Set the player's progress to their next character level.
    *
    * \param pid The player ID.
    * \param value The new level progress of the player.
    * \return void
    */
    static void SetLevelProgress(unsigned short pid, int value) noexcept;

    /**
    * \brief Set the base health of a player.
    *
    * \param pid The player ID.
    * \param value The new base health of the player.
    * \return void
    */
    static void SetHealthBase(unsigned short pid, double value) noexcept;

    /**
    * \brief Set the current health of a player.
    *
    * \param pid The player ID.
    * \param value The new current health of the player.
    * \return void
    */
    static void SetHealthCurrent(unsigned short pid, double value) noexcept;

    /**
    * \brief Set the base magicka of a player.
    *
    * \param pid The player ID.
    * \param value The new base magicka of the player.
    * \return void
    */
    static void SetMagickaBase(unsigned short pid, double value) noexcept;

    /**
    * \brief Set the current magicka of a player.
    *
    * \param pid The player ID.
    * \param value The new current magicka of the player.
    * \return void
    */
    static void SetMagickaCurrent(unsigned short pid, double value) noexcept;

    /**
    * \brief Set the base fatigue of a player.
    *
    * \param pid The player ID.
    * \param value The new base fatigue of the player.
    * \return void
    */
    static void SetFatigueBase(unsigned short pid, double value) noexcept;

    /**
    * \brief Set the current fatigue of a player.
    *
    * \param pid The player ID.
    * \param value The new current fatigue of the player.
    * \return void
    */
    static void SetFatigueCurrent(unsigned short pid, double value) noexcept;

    /**
    * \brief Set the base value of a player's attribute.
    *
    * \param pid The player ID.
    * \param attributeId The attribute ID.
    * \param value The new base value of the player's attribute.
    * \return void
    */
    static void SetAttributeBase(unsigned short pid, unsigned short attributeId, int value) noexcept;

    /**
    * \brief Clear the modifier value of a player's attribute.
    *
    * There's no way to set a modifier to a specific value because it can come from
    * multiple different sources, but clearing it is a straightforward process that
    * dispels associated effects on a client and, if necessary, unequips associated
    * items.
    *
    * \param pid The player ID.
    * \param attributeId The attribute ID.
    * \return void
    */
    static void ClearAttributeModifier(unsigned short pid, unsigned short attributeId) noexcept;

    /**
    * \brief Set the amount of damage (as caused through the Damage Attribute effect) to
    *        a player's attribute.
    *
    * \param pid The player ID.
    * \param attributeId The attribute ID.
    * \param value The amount of damage to the player's attribute.
    * \return void
    */
    static void SetAttributeDamage(unsigned short pid, unsigned short attributeId, double value) noexcept;

    /**
    * \brief Set the base value of a player's skill.
    *
    * \param pid The player ID.
    * \param skillId The skill ID.
    * \param value The new base value of the player's skill.
    * \return void
    */
    static void SetSkillBase(unsigned short pid, unsigned short skillId, int value) noexcept;    

    /**
    * \brief Clear the modifier value of a player's skill.
    *
    * There's no way to set a modifier to a specific value because it can come from
    * multiple different sources, but clearing it is a straightforward process that
    * dispels associated effects on a client and, if necessary, unequips associated
    * items.
    *
    * \param pid The player ID.
    * \param skillId The skill ID.
    * \return void
    */
    static void ClearSkillModifier(unsigned short pid, unsigned short skillId) noexcept;

    /**
    * \brief Set the amount of damage (as caused through the Damage Skill effect) to
    *        a player's skill.
    *
    * \param pid The player ID.
    * \param skillId The skill ID.
    * \param value The amount of damage to the player's skill.
    * \return void
    */
    static void SetSkillDamage(unsigned short pid, unsigned short skillId, double value) noexcept;

    /**
    * \brief Set the progress the player has made towards increasing a certain skill by 1.
    *
    * \param pid The player ID.
    * \param skillId The skill ID.
    * \param value The progress value.
    * \return void
    */
    static void SetSkillProgress(unsigned short pid, unsigned short skillId, double value) noexcept;

    /**
    * \brief Set the bonus applied to a certain attribute at the next level up as a result
    *        of associated skill increases.
    *
    * Although confusing, the term "skill increase" for this is taken from OpenMW itself.
    *
    * \param pid The player ID.
    * \param attributeId The attribute ID.
    * \param value The increase in the attribute caused by skills.
    * \return void
    */
    static void SetSkillIncrease(unsigned short pid, unsigned int attributeId, int value) noexcept;

    /**
    * \brief Set the bounty of a player.
    *
    * \param pid The player ID.
    * \param value The new bounty.
    * \return void
    */
    static void SetBounty(unsigned short pid, int value) noexcept;

    /**
    * \brief Set the current and ending stages of character generation for a player.
    *
    * This is used to repeat part of character generation or to only go through part of it.
    *
    * \param pid The player ID.
    * \param currentStage The new current stage.
    * \param endStage The new ending stage.
    * \return void
    */
    static void SetCharGenStage(unsigned short pid, int currentStage, int endStage) noexcept;

    /** Reveal or hide a player from other clients. Revealing sends a full presence snapshot. */
    static void SetPlayerVisible(unsigned short pid, bool state) noexcept;

    /**
    * \brief Send a PlayerBaseInfo packet with a player's name, race, head mesh,
    *        hairstyle mesh, birthsign and stat reset state.
    *
    * It is always sent to all players.
    *
    * \param pid The player ID.
    * \return void
    */
    static void SendBaseInfo(unsigned short pid) noexcept;

    /**
    * \brief Send a PlayerStatsDynamic packet with a player's dynamic stats (health,
    *        magicka and fatigue).
    *
    * It is always sent to all players.
    *
    * \param pid The player ID.
    * \return void
    */
    static void SendStatsDynamic(unsigned short pid) noexcept;

    /**
    * \brief Send a PlayerAttribute packet with a player's attributes and bonuses
    *        to those attributes at the next level up (the latter being called
    *        "skill increases" as in OpenMW).
    *
    * It is always sent to all players.
    *
    * \param pid The player ID.
    * \return void
    */
    static void SendAttributes(unsigned short pid) noexcept;

    /**
    * \brief Send a PlayerSkill packet with a player's skills.
    *
    * It is always sent to all players.
    *
    * \param pid The player ID.
    * \return void
    */
    static void SendSkills(unsigned short pid) noexcept;

    /**
    * \brief Send a PlayerLevel packet with a player's character level and
    *        progress towards the next level up
    *
    * It is always sent to all players.
    *
    * \param pid The player ID.
    * \return void
    */
    static void SendLevel(unsigned short pid) noexcept;

    /**
    * \brief Send a PlayerBounty packet with a player's bounty.
    *
    * It is always sent to all players.
    *
    * \param pid The player ID.
    * \return void
    */
    static void SendBounty(unsigned short pid) noexcept;
};

#endif //OPENMW_STATAPI_HPP
