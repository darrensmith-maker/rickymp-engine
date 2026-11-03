#ifndef OPENMW_RECORDSDYNAMICAPI_HPP
#define OPENMW_RECORDSDYNAMICAPI_HPP

#include "../Types.hpp"

#define RECORDSDYNAMICAPI \
    SCRIPT_API_ENTRY("ClearRecords", RecordsDynamicFunctions::ClearRecords),\
    \
    SCRIPT_API_ENTRY("GetRecordType", RecordsDynamicFunctions::GetRecordType),\
    SCRIPT_API_ENTRY("GetRecordCount", RecordsDynamicFunctions::GetRecordCount),\
    SCRIPT_API_ENTRY("GetRecordEffectCount", RecordsDynamicFunctions::GetRecordEffectCount),\
    \
    SCRIPT_API_ENTRY("GetRecordId", RecordsDynamicFunctions::GetRecordId),\
    SCRIPT_API_ENTRY("GetRecordBaseId", RecordsDynamicFunctions::GetRecordBaseId),\
    \
    SCRIPT_API_ENTRY("GetRecordSubtype", RecordsDynamicFunctions::GetRecordSubtype),\
    SCRIPT_API_ENTRY("GetRecordName", RecordsDynamicFunctions::GetRecordName),\
    SCRIPT_API_ENTRY("GetRecordModel", RecordsDynamicFunctions::GetRecordModel),\
    SCRIPT_API_ENTRY("GetRecordIcon", RecordsDynamicFunctions::GetRecordIcon),\
    SCRIPT_API_ENTRY("GetRecordScript", RecordsDynamicFunctions::GetRecordScript),\
    SCRIPT_API_ENTRY("GetRecordText", RecordsDynamicFunctions::GetRecordText),\
    SCRIPT_API_ENTRY("GetRecordScrollState", RecordsDynamicFunctions::GetRecordScrollState),\
    SCRIPT_API_ENTRY("GetRecordSkillId", RecordsDynamicFunctions::GetRecordSkillId),\
    SCRIPT_API_ENTRY("GetRecordEnchantmentId", RecordsDynamicFunctions::GetRecordEnchantmentId),\
    SCRIPT_API_ENTRY("GetRecordEnchantmentCharge", RecordsDynamicFunctions::GetRecordEnchantmentCharge),\
    \
    SCRIPT_API_ENTRY("GetRecordAutoCalc", RecordsDynamicFunctions::GetRecordAutoCalc),\
    SCRIPT_API_ENTRY("GetRecordCharge", RecordsDynamicFunctions::GetRecordCharge),\
    SCRIPT_API_ENTRY("GetRecordCost", RecordsDynamicFunctions::GetRecordCost),\
    SCRIPT_API_ENTRY("GetRecordFlags", RecordsDynamicFunctions::GetRecordFlags),\
    SCRIPT_API_ENTRY("GetRecordValue", RecordsDynamicFunctions::GetRecordValue),\
    SCRIPT_API_ENTRY("GetRecordWeight", RecordsDynamicFunctions::GetRecordWeight),\
    SCRIPT_API_ENTRY("GetRecordQuantity", RecordsDynamicFunctions::GetRecordQuantity),\
    \
    SCRIPT_API_ENTRY("GetRecordEffectId", RecordsDynamicFunctions::GetRecordEffectId),\
    SCRIPT_API_ENTRY("GetRecordEffectAttribute", RecordsDynamicFunctions::GetRecordEffectAttribute),\
    SCRIPT_API_ENTRY("GetRecordEffectSkill", RecordsDynamicFunctions::GetRecordEffectSkill),\
    SCRIPT_API_ENTRY("GetRecordEffectRangeType", RecordsDynamicFunctions::GetRecordEffectRangeType),\
    SCRIPT_API_ENTRY("GetRecordEffectArea", RecordsDynamicFunctions::GetRecordEffectArea),\
    SCRIPT_API_ENTRY("GetRecordEffectDuration", RecordsDynamicFunctions::GetRecordEffectDuration),\
    SCRIPT_API_ENTRY("GetRecordEffectMagnitudeMax", RecordsDynamicFunctions::GetRecordEffectMagnitudeMax),\
    SCRIPT_API_ENTRY("GetRecordEffectMagnitudeMin", RecordsDynamicFunctions::GetRecordEffectMagnitudeMin),\
    \
    SCRIPT_API_ENTRY("SetRecordType", RecordsDynamicFunctions::SetRecordType),\
    \
    SCRIPT_API_ENTRY("SetRecordId", RecordsDynamicFunctions::SetRecordId),\
    SCRIPT_API_ENTRY("SetRecordBaseId", RecordsDynamicFunctions::SetRecordBaseId),\
    SCRIPT_API_ENTRY("SetRecordInventoryBaseId", RecordsDynamicFunctions::SetRecordInventoryBaseId),\
    \
    SCRIPT_API_ENTRY("SetRecordSubtype", RecordsDynamicFunctions::SetRecordSubtype),\
    SCRIPT_API_ENTRY("SetRecordName", RecordsDynamicFunctions::SetRecordName),\
    SCRIPT_API_ENTRY("SetRecordModel", RecordsDynamicFunctions::SetRecordModel),\
    SCRIPT_API_ENTRY("SetRecordIcon", RecordsDynamicFunctions::SetRecordIcon),\
    SCRIPT_API_ENTRY("SetRecordScript", RecordsDynamicFunctions::SetRecordScript),\
    SCRIPT_API_ENTRY("SetRecordEnchantmentId", RecordsDynamicFunctions::SetRecordEnchantmentId),\
    SCRIPT_API_ENTRY("SetRecordEnchantmentCharge", RecordsDynamicFunctions::SetRecordEnchantmentCharge),\
    \
    SCRIPT_API_ENTRY("SetRecordAutoCalc", RecordsDynamicFunctions::SetRecordAutoCalc),\
    SCRIPT_API_ENTRY("SetRecordCharge", RecordsDynamicFunctions::SetRecordCharge),\
    SCRIPT_API_ENTRY("SetRecordCost", RecordsDynamicFunctions::SetRecordCost),\
    SCRIPT_API_ENTRY("SetRecordFlags", RecordsDynamicFunctions::SetRecordFlags),\
    SCRIPT_API_ENTRY("SetRecordValue", RecordsDynamicFunctions::SetRecordValue),\
    SCRIPT_API_ENTRY("SetRecordWeight", RecordsDynamicFunctions::SetRecordWeight),\
    SCRIPT_API_ENTRY("SetRecordQuality", RecordsDynamicFunctions::SetRecordQuality),\
    SCRIPT_API_ENTRY("SetRecordUses", RecordsDynamicFunctions::SetRecordUses),\
    SCRIPT_API_ENTRY("SetRecordTime", RecordsDynamicFunctions::SetRecordTime),\
    SCRIPT_API_ENTRY("SetRecordRadius", RecordsDynamicFunctions::SetRecordRadius),\
    SCRIPT_API_ENTRY("SetRecordColor", RecordsDynamicFunctions::SetRecordColor),\
    \
    SCRIPT_API_ENTRY("SetRecordArmorRating", RecordsDynamicFunctions::SetRecordArmorRating),\
    SCRIPT_API_ENTRY("SetRecordHealth", RecordsDynamicFunctions::SetRecordHealth),\
    \
    SCRIPT_API_ENTRY("SetRecordDamageChop", RecordsDynamicFunctions::SetRecordDamageChop),\
    SCRIPT_API_ENTRY("SetRecordDamageSlash", RecordsDynamicFunctions::SetRecordDamageSlash),\
    SCRIPT_API_ENTRY("SetRecordDamageThrust", RecordsDynamicFunctions::SetRecordDamageThrust),\
    SCRIPT_API_ENTRY("SetRecordReach", RecordsDynamicFunctions::SetRecordReach),\
    SCRIPT_API_ENTRY("SetRecordSpeed", RecordsDynamicFunctions::SetRecordSpeed),\
    \
    SCRIPT_API_ENTRY("SetRecordKeyState", RecordsDynamicFunctions::SetRecordKeyState),\
    SCRIPT_API_ENTRY("SetRecordScrollState", RecordsDynamicFunctions::SetRecordScrollState),\
    SCRIPT_API_ENTRY("SetRecordSkillId", RecordsDynamicFunctions::SetRecordSkillId),\
    SCRIPT_API_ENTRY("SetRecordText", RecordsDynamicFunctions::SetRecordText),\
    \
    SCRIPT_API_ENTRY("SetRecordHair", RecordsDynamicFunctions::SetRecordHair),\
    SCRIPT_API_ENTRY("SetRecordHead", RecordsDynamicFunctions::SetRecordHead),\
    SCRIPT_API_ENTRY("SetRecordGender", RecordsDynamicFunctions::SetRecordGender),\
    SCRIPT_API_ENTRY("SetRecordRace", RecordsDynamicFunctions::SetRecordRace),\
    SCRIPT_API_ENTRY("SetRecordClass", RecordsDynamicFunctions::SetRecordClass),\
    SCRIPT_API_ENTRY("SetRecordFaction", RecordsDynamicFunctions::SetRecordFaction),\
    \
    SCRIPT_API_ENTRY("SetRecordScale", RecordsDynamicFunctions::SetRecordScale),\
    SCRIPT_API_ENTRY("SetRecordBloodType", RecordsDynamicFunctions::SetRecordBloodType),\
    SCRIPT_API_ENTRY("SetRecordVampireState", RecordsDynamicFunctions::SetRecordVampireState),\
    \
    SCRIPT_API_ENTRY("SetRecordLevel", RecordsDynamicFunctions::SetRecordLevel),\
    SCRIPT_API_ENTRY("SetRecordMagicka", RecordsDynamicFunctions::SetRecordMagicka),\
    SCRIPT_API_ENTRY("SetRecordFatigue", RecordsDynamicFunctions::SetRecordFatigue),\
    \
    SCRIPT_API_ENTRY("SetRecordSoulValue", RecordsDynamicFunctions::SetRecordSoulValue),\
    \
    SCRIPT_API_ENTRY("SetRecordAIFight", RecordsDynamicFunctions::SetRecordAIFight),\
    SCRIPT_API_ENTRY("SetRecordAIFlee", RecordsDynamicFunctions::SetRecordAIFlee),\
    SCRIPT_API_ENTRY("SetRecordAIAlarm", RecordsDynamicFunctions::SetRecordAIAlarm),\
    SCRIPT_API_ENTRY("SetRecordAIServices", RecordsDynamicFunctions::SetRecordAIServices),\
    \
    SCRIPT_API_ENTRY("SetRecordSound", RecordsDynamicFunctions::SetRecordSound),\
    SCRIPT_API_ENTRY("SetRecordVolume", RecordsDynamicFunctions::SetRecordVolume),\
    SCRIPT_API_ENTRY("SetRecordMinRange", RecordsDynamicFunctions::SetRecordMinRange),\
    SCRIPT_API_ENTRY("SetRecordMaxRange", RecordsDynamicFunctions::SetRecordMaxRange),\
    SCRIPT_API_ENTRY("SetRecordOpenSound", RecordsDynamicFunctions::SetRecordOpenSound),\
    SCRIPT_API_ENTRY("SetRecordCloseSound", RecordsDynamicFunctions::SetRecordCloseSound),\
    \
    SCRIPT_API_ENTRY("SetRecordScriptText", RecordsDynamicFunctions::SetRecordScriptText),\
    SCRIPT_API_ENTRY("SetRecordIntegerVariable", RecordsDynamicFunctions::SetRecordIntegerVariable),\
    SCRIPT_API_ENTRY("SetRecordFloatVariable", RecordsDynamicFunctions::SetRecordFloatVariable),\
    SCRIPT_API_ENTRY("SetRecordStringVariable", RecordsDynamicFunctions::SetRecordStringVariable),\
    \
    SCRIPT_API_ENTRY("SetRecordIdByIndex", RecordsDynamicFunctions::SetRecordIdByIndex),\
    SCRIPT_API_ENTRY("SetRecordEnchantmentIdByIndex", RecordsDynamicFunctions::SetRecordEnchantmentIdByIndex),\
    \
    SCRIPT_API_ENTRY("SetRecordEffectId", RecordsDynamicFunctions::SetRecordEffectId),\
    SCRIPT_API_ENTRY("SetRecordEffectAttribute", RecordsDynamicFunctions::SetRecordEffectAttribute),\
    SCRIPT_API_ENTRY("SetRecordEffectSkill", RecordsDynamicFunctions::SetRecordEffectSkill),\
    SCRIPT_API_ENTRY("SetRecordEffectRangeType", RecordsDynamicFunctions::SetRecordEffectRangeType),\
    SCRIPT_API_ENTRY("SetRecordEffectArea", RecordsDynamicFunctions::SetRecordEffectArea),\
    SCRIPT_API_ENTRY("SetRecordEffectDuration", RecordsDynamicFunctions::SetRecordEffectDuration),\
    SCRIPT_API_ENTRY("SetRecordEffectMagnitudeMax", RecordsDynamicFunctions::SetRecordEffectMagnitudeMax),\
    SCRIPT_API_ENTRY("SetRecordEffectMagnitudeMin", RecordsDynamicFunctions::SetRecordEffectMagnitudeMin),\
    \
    SCRIPT_API_ENTRY("SetRecordBodyPartType", RecordsDynamicFunctions::SetRecordBodyPartType),\
    SCRIPT_API_ENTRY("SetRecordBodyPartIdForMale", RecordsDynamicFunctions::SetRecordBodyPartIdForMale),\
    SCRIPT_API_ENTRY("SetRecordBodyPartIdForFemale", RecordsDynamicFunctions::SetRecordBodyPartIdForFemale),\
    \
    SCRIPT_API_ENTRY("SetRecordInventoryItemId", RecordsDynamicFunctions::SetRecordInventoryItemId),\
    SCRIPT_API_ENTRY("SetRecordInventoryItemCount", RecordsDynamicFunctions::SetRecordInventoryItemCount),\
    \
    SCRIPT_API_ENTRY("AddRecord", RecordsDynamicFunctions::AddRecord),\
    SCRIPT_API_ENTRY("AddRecordEffect", RecordsDynamicFunctions::AddRecordEffect),\
    SCRIPT_API_ENTRY("AddRecordBodyPart", RecordsDynamicFunctions::AddRecordBodyPart),\
    SCRIPT_API_ENTRY("AddRecordInventoryItem", RecordsDynamicFunctions::AddRecordInventoryItem),\
    \
    SCRIPT_API_ENTRY("SendRecordDynamic", RecordsDynamicFunctions::SendRecordDynamic)

class RecordsDynamicFunctions
{
public:

    /**
    * \brief Clear the data from the records stored on the server.
    *
    * \return void
    */
    static void ClearRecords() noexcept;

    /**
    * \brief Get the type of records in the read worldstate's dynamic records.
    *
    * \return The type of records (0 for SPELL, 1 for POTION, 2 for ENCHANTMENT,
    *         3 for NPC).
    */
    static unsigned short GetRecordType() noexcept;

    /**
    * \brief Get the number of records in the read worldstate's dynamic records.
    *
    * \return The number of records.
    */
    static unsigned int GetRecordCount() noexcept;

    /**
    * \brief Get the number of effects for the record at a certain index in the read
    * worldstate's current records.
    *
    * \param recordIndex The index of the record.
    * \return The number of effects.
    */
    static unsigned int GetRecordEffectCount(unsigned int recordIndex) noexcept;

    /**
    * \brief Get the id of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The id of the record.
    */
    static const char *GetRecordId(unsigned int index) noexcept;

    /**
    * \brief Get the base id (i.e. the id this record should inherit default
    * values from) of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The base id of the record.
    */
    static const char *GetRecordBaseId(unsigned int index) noexcept;

    /**
    * \brief Get the subtype of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The type of the record.
    */
    static int GetRecordSubtype(unsigned int index) noexcept;

    /**
    * \brief Get the name of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The name of the record.
    */
    static const char *GetRecordName(unsigned int index) noexcept;

    /**
    * \brief Get the model of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The model of the record.
    */
    static const char *GetRecordModel(unsigned int index) noexcept;

    /**
    * \brief Get the icon of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The icon of the record.
    */
    static const char *GetRecordIcon(unsigned int index) noexcept;

    /**
    * \brief Get the script of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The script of the record.
    */
    static const char *GetRecordScript(unsigned int index) noexcept;

    /** Book text for BOOK dynamic records. */
    static const char *GetRecordText(unsigned int index) noexcept;

    /** BOOK scroll flag (0 book, 1 scroll). */
    static int GetRecordScrollState(unsigned int index) noexcept;

    /** BOOK skill id, including ArenaMP authored marker -2. */
    static int GetRecordSkillId(unsigned int index) noexcept;

    /**
    * \brief Get the enchantment id of the record at a certain index in the read
    * worldstate's dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The enchantment id of the record.
    */
    static const char *GetRecordEnchantmentId(unsigned int index) noexcept;

    /**
    * \brief Get the enchantment charge of the record at a certain index in
    * the read worldstate's dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The enchantment charge of the record.
    */
    static int GetRecordEnchantmentCharge(unsigned int index) noexcept;

    /**
    * \brief Get the auto-calculation flag value of the record at a certain index in
    * the read worldstate's dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The auto-calculation flag value of the record.
    */
    static int GetRecordAutoCalc(unsigned int index) noexcept;

    /**
    * \brief Get the charge of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The charge of the record.
    */
    static int GetRecordCharge(unsigned int index) noexcept;

    /**
    * \brief Get the cost of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The cost of the record.
    */
    static int GetRecordCost(unsigned int index) noexcept;

    /**
    * \brief Get the flags of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The flags of the spell as an integer.
    */
    static int GetRecordFlags(unsigned int index) noexcept;

    /**
    * \brief Get the value of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The value of the record.
    */
    static int GetRecordValue(unsigned int index) noexcept;

    /**
    * \brief Get the weight of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The weight of the record.
    */
    static double GetRecordWeight(unsigned int index) noexcept;

    /**
    * \brief Get the quantity of the record at a certain index in the read worldstate's
    * dynamic records of the current type.
    *
    * \param index The index of the record.
    * \return The brewed count of the record.
    */
    static unsigned int GetRecordQuantity(unsigned int index) noexcept;

    /**
    * \brief Get the ID of the effect at a certain index in the read worldstate's
    * current records.
    *
    * \param recordIndex The index of the record.
    * \param effectIndex The index of the effect.
    * \return The ID of the effect.
    */
    static unsigned int GetRecordEffectId(unsigned int recordIndex, unsigned int effectIndex) noexcept;

    /**
    * \brief Get the ID of the attribute modified by the effect at a certain index in the
    * read worldstate's current records.
    *
    * \param recordIndex The index of the record.
    * \param effectIndex The index of the effect.
    * \return The attribute ID for the effect.
    */
    static int GetRecordEffectAttribute(unsigned int recordIndex, unsigned int effectIndex) noexcept;

    /**
    * \brief Get the ID of the skill modified by the effect at a certain index in the
    * read worldstate's current records.
    *
    * \param recordIndex The index of the record.
    * \param effectIndex The index of the effect.
    * \return The skill ID for the effect.
    */
    static int GetRecordEffectSkill(unsigned int recordIndex, unsigned int effectIndex) noexcept;

    /**
    * \brief Get the range type of the effect at a certain index in the read worldstate's
    * current records (0 for self, 1 for touch, 2 for target).
    *
    * \param recordIndex The index of the record.
    * \param effectIndex The index of the effect.
    * \return The range of the effect.
    */
    static unsigned int GetRecordEffectRangeType(unsigned int recordIndex, unsigned int effectIndex) noexcept;

    /**
    * \brief Get the area of the effect at a certain index in the read worldstate's current
    * records.
    *
    * \param recordIndex The index of the record.
    * \param effectIndex The index of the effect.
    * \return The area of the effect.
    */
    static int GetRecordEffectArea(unsigned int recordIndex, unsigned int effectIndex) noexcept;

    /**
    * \brief Get the duration of the effect at a certain index in the read worldstate's current
    * records.
    *
    * \param recordIndex The index of the record.
    * \param effectIndex The index of the effect.
    * \return The duration of the effect.
    */
    static int GetRecordEffectDuration(unsigned int recordIndex, unsigned int effectIndex) noexcept;

    /**
    * \brief Get the maximum magnitude of the effect at a certain index in the read
    * worldstate's current records.
    *
    * \param recordIndex The index of the record.
    * \param effectIndex The index of the effect.
    * \return The maximum magnitude of the effect.
    */
    static int GetRecordEffectMagnitudeMax(unsigned int recordIndex, unsigned int effectIndex) noexcept;

    /**
    * \brief Get the minimum magnitude of the effect at a certain index in the read
    * worldstate's current records.
    *
    * \param recordIndex The index of the record.
    * \param effectIndex The index of the effect.
    * \return The minimum magnitude of the effect.
    */
    static int GetRecordEffectMagnitudeMin(unsigned int recordIndex, unsigned int effectIndex) noexcept;

    /**
    * \brief Set which type of temporary records stored on the server should have
    * their data changed via setter functions.
    *
    * \param type The type of records.
    * \return void
    */
    static void SetRecordType(unsigned int type) noexcept;

    /**
    * \brief Set the id of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param id The id of the record.
    * \return void
    */
    static void SetRecordId(const char* id) noexcept;

    /**
    * \brief Set the base id (i.e. the id this record should inherit default
    * values from) of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param baseId The baseId of the record.
    * \return void
    */
    static void SetRecordBaseId(const char* baseId) noexcept;

    /**
    * \brief Set the inventory base id (i.e. the id this record should inherit
    * its inventory contents from) of the temporary record stored on the server for
    * the currently specified record type.
    *
    * \param inventoryBaseId The inventoryBaseId of the record.
    * \return void
    */
    static void SetRecordInventoryBaseId(const char* inventoryBaseId) noexcept;

    /**
    * \brief Set the subtype of the temporary record stored on the server for
    * the currently specified record type.
    *
    * \param subtype The spell type.
    * \return void
    */
    static void SetRecordSubtype(unsigned int subtype) noexcept;

    /**
    * \brief Set the name of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param name The name of the record.
    * \return void
    */
    static void SetRecordName(const char* name) noexcept;

    /**
    * \brief Set the model of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param model The model of the record.
    * \return void
    */
    static void SetRecordModel(const char* model) noexcept;

    /**
    * \brief Set the icon of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param icon The icon of the record.
    * \return void
    */
    static void SetRecordIcon(const char* icon) noexcept;

    /**
    * \brief Set the script of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param script The script of the record.
    * \return void
    */
    static void SetRecordScript(const char* script) noexcept;

    /**
    * \brief Set the enchantment id of the temporary record stored on the server
    * for the currently specified record type.
    *
    * \param enchantmentId The enchantment id of the record.
    * \return void
    */
    static void SetRecordEnchantmentId(const char* enchantmentId) noexcept;

    /**
    * \brief Set the enchantment charge of the temporary record stored on the server
    * for the currently specified record type.
    *
    * \param enchantmentCharge The enchantmentCharge of the record.
    * \return void
    */
    static void SetRecordEnchantmentCharge(int enchantmentCharge) noexcept;

    /**
    * \brief Set the auto-calculation flag value of the temporary record stored
    * on the server for the currently specified record type.
    *
    * \param autoCalc The auto-calculation flag value of the record.
    * \return void
    */
    static void SetRecordAutoCalc(int autoCalc) noexcept;

    /**
    * \brief Set the charge of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param charge The charge of the record.
    * \return void
    */
    static void SetRecordCharge(int charge) noexcept;

    /**
    * \brief Set the cost of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param cost The cost of the record.
    * \return void
    */
    static void SetRecordCost(int cost) noexcept;

    /**
    * \brief Set the flags of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param flags The flags of the record.
    * \return void
    */
    static void SetRecordFlags(int flags) noexcept;

    /**
    * \brief Set the value of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param value The value of the record.
    * \return void
    */
    static void SetRecordValue(int value) noexcept;

    /**
    * \brief Set the weight of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param weight The weight of the record.
    * \return void
    */
    static void SetRecordWeight(double weight) noexcept;

    /**
    * \brief Set the item quality of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param quality The quality of the record.
    * \return void
    */
    static void SetRecordQuality(double quality) noexcept;

    /**
    * \brief Set the number of uses of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param uses The number of uses of the record.
    * \return void
    */
    static void SetRecordUses(int uses) noexcept;

    /**
    * \brief Set the time of the temporary record stored on the server for the currently
    * specified record type.
    *
    * \param time The time of the record.
    * \return void
    */
    static void SetRecordTime(int time) noexcept;

    /**
    * \brief Set the radius of the temporary record stored on the server for the currently
    * specified record type.
    *
    * \param radius The radius of the record.
    * \return void
    */
    static void SetRecordRadius(int radius) noexcept;

    /**
    * \brief Set the color of the temporary record stored on the server for the currently
    * specified record type.
    *
    * \param red The red value of the record.
    * \param green The green value of the record.
    * \param blue The blue value of the record.
    * \return void
    */
    static void SetRecordColor(unsigned int red, unsigned int green, unsigned int blue) noexcept;

    /**
    * \brief Set the armor rating of the temporary record stored on the server
    * for the currently specified record type.
    *
    * \param armorRating The armor rating of the record.
    * \return void
    */
    static void SetRecordArmorRating(int armorRating) noexcept;

    /**
    * \brief Set the health of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param health The health of the record.
    * \return void
    */
    static void SetRecordHealth(int health) noexcept;

    /**
    * \brief Set the chop damage of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param minDamage The minimum damage of the record.
    * \param maxDamage The maximum damage of the record.
    * \return void
    */
    static void SetRecordDamageChop(unsigned int minDamage, unsigned int maxDamage) noexcept;

    /**
    * \brief Set the slash damage of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param minDamage The minimum damage of the record.
    * \param maxDamage The maximum damage of the record.
    * \return void
    */
    static void SetRecordDamageSlash(unsigned int minDamage, unsigned int maxDamage) noexcept;

    /**
    * \brief Set the thrust damage of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param minDamage The minimum damage of the record.
    * \param maxDamage The maximum damage of the record.
    * \return void
    */
    static void SetRecordDamageThrust(unsigned int minDamage, unsigned int maxDamage) noexcept;

    /**
    * \brief Set the reach of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param reach The reach of the record.
    * \return void
    */
    static void SetRecordReach(double reach) noexcept;

    /**
    * \brief Set the speed of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param speed The speed of the record.
    * \return void
    */
    static void SetRecordSpeed(double speed) noexcept;

    /**
    * \brief Set whether the temporary record stored on the server for the
    * currently specified record type is a key.
    *
    * Note: This is only applicable to Miscellaneous records.
    *
    * \param keyState Whether the record is a key.
    * \return void
    */
    static void SetRecordKeyState(bool keyState) noexcept;

    /**
    * \brief Set whether the temporary record stored on the server for the
    * currently specified record type is a scroll.
    *
    * Note: This is only applicable to Book records.
    *
    * \param scrollState Whether the record is a scroll.
    * \return void
    */
    static void SetRecordScrollState(bool scrollState) noexcept;

    /**
    * \brief Set the skill ID of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param skillId The skill ID of the record.
    * \return void
    */
    static void SetRecordSkillId(int skillId) noexcept;

    /**
    * \brief Set the text of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param text The text of the record.
    * \return void
    */
    static void SetRecordText(const char* text) noexcept;

    /**
    * \brief Set the hair of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param hair The hair of the record.
    * \return void
    */
    static void SetRecordHair(const char* hair) noexcept;

    /**
    * \brief Set the head of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param head The head of the record.
    * \return void
    */
    static void SetRecordHead(const char* head) noexcept;

    /**
    * \brief Set the gender of the temporary record stored on the server for the
    * currently specified record type (0 for female, 1 for male).
    *
    * \param gender The gender of the record.
    * \return void
    */
    static void SetRecordGender(unsigned int gender) noexcept;

    /**
    * \brief Set the race of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param race The race of the record.
    * \return void
    */
    static void SetRecordRace(const char* race) noexcept;

    /**
    * \brief Set the character class of the temporary record stored on the server
    * for the currently specified record type.
    *
    * \param charClass The character class of the record.
    * \return void
    */
    static void SetRecordClass(const char* charClass) noexcept;

    /**
    * \brief Set the faction of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param faction The faction of the record.
    * \return void
    */
    static void SetRecordFaction(const char* faction) noexcept;

    /**
    * \brief Set the scale of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param scale The scale of the record.
    * \return void
    */
    static void SetRecordScale(double scale) noexcept;

    /**
    * \brief Set the blood type of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param bloodType The blood type of the record.
    * \return void
    */
    static void SetRecordBloodType(int bloodType) noexcept;

    /**
    * \brief Set the vampire state of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param vampireState The vampire state of the record.
    * \return void
    */
    static void SetRecordVampireState(bool vampireState) noexcept;

    /**
    * \brief Set the level of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param level The level of the record.
    * \return void
    */
    static void SetRecordLevel(int level) noexcept;

    /**
    * \brief Set the magicka of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param magicka The magicka of the record.
    * \return void
    */
    static void SetRecordMagicka(int magicka) noexcept;

    /**
    * \brief Set the fatigue of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param fatigue The fatigue of the record.
    * \return void
    */
    static void SetRecordFatigue(int fatigue) noexcept;

    /**
    * \brief Set the soul value of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param soulValue The soul value of the record.
    * \return void
    */
    static void SetRecordSoulValue(int soulValue) noexcept;

    /**
    * \brief Set the AI fight value of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param aiFight The AI fight value of the record.
    * \return void
    */
    static void SetRecordAIFight(int aiFight) noexcept;

    /**
    * \brief Set the AI flee value of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param aiFlee The AI flee value of the record.
    * \return void
    */
    static void SetRecordAIFlee(int aiFlee) noexcept;

    /**
    * \brief Set the AI alarm value of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param aiAlarm The AI alarm value of the record.
    * \return void
    */
    static void SetRecordAIAlarm(int aiAlarm) noexcept;

    /**
    * \brief Set the AI services value of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param aiServices The AI services value of the record.
    * \return void
    */
    static void SetRecordAIServices(int aiServices) noexcept;

    /**
    * \brief Set the sound of the temporary record stored on the server for the currently
    * specified record type.
    *
    * \param sound The sound of the record.
    * \return void
    */
    static void SetRecordSound(const char* sound) noexcept;

    /**
    * \brief Set the volume of the temporary record stored on the server for the currently
    * specified record type.
    *
    * \param volume The volume of the record.
    * \return void
    */
    static void SetRecordVolume(double volume) noexcept;

    /**
    * \brief Set the minimum range of the temporary record stored on the server for the currently
    * specified record type.
    *
    * \param minRange The minimum range of the record.
    * \return void
    */
    static void SetRecordMinRange(double minRange) noexcept;

    /**
    * \brief Set the maximum range of the temporary record stored on the server for the currently
    * specified record type.
    *
    * \param maxRange The maximum range of the record.
    * \return void
    */
    static void SetRecordMaxRange(double maxRange) noexcept;

    /**
    * \brief Set the opening sound of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param sound The opening sound of the record.
    * \return void
    */
    static void SetRecordOpenSound(const char* sound) noexcept;

    /**
    * \brief Set the closing sound of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param sound The closing sound of the record.
    * \return void
    */
    static void SetRecordCloseSound(const char* sound) noexcept;

    /**
    * \brief Set the script text of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param scriptText The script text of the record.
    * \return void
    */
    static void SetRecordScriptText(const char* scriptText) noexcept;

    /**
    * \brief Set the integer variable of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param intVar The integer variable of the record.
    * \return void
    */
    static void SetRecordIntegerVariable(int intVar) noexcept;

    /**
    * \brief Set the float variable of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param floatVar The float variable of the record.
    * \return void
    */
    static void SetRecordFloatVariable(double floatVar) noexcept;

    /**
    * \brief Set the string variable of the temporary record stored on the server for the
    * currently specified record type.
    *
    * \param stringVar The string variable of the record.
    * \return void
    */
    static void SetRecordStringVariable(const char* stringVar) noexcept;

    /**
    * \brief Set the id of the record at a certain index in the records stored on the server.
    *
    * When resending a received RecordsDynamic packet, this allows you to set the server-generated
    * id of a record without having to clear and recreate the packet.
    *
    * \param index The index of the record.
    * \param id The id of the record.
    * \return void
    */
    static void SetRecordIdByIndex(unsigned int index, const char* id) noexcept;

    /**
    * \brief Set the enchantment id of the record at a certain index in the records stored on
    * the server.
    *
    * When resending a received RecordsDynamic packet, this allows you to set the server-generated
    * enchantment id of a record without having to clear and recreate the packet.
    *
    * \param index The index of the record.
    * \param enchantmentId The enchantment id of the record.
    * \return void
    */
    static void SetRecordEnchantmentIdByIndex(unsigned int index, const char* enchantmentId) noexcept;

    /**
    * \brief Set the ID of the temporary effect stored on the server.
    *
    * \param effectId The ID of the effect.
    * \return void
    */
    static void SetRecordEffectId(unsigned int effectId) noexcept;

    /**
    * \brief Set the ID of the attribute modified by the temporary effect stored on
    * the server.
    *
    * \param attributeId The ID of the attribute.
    * \return void
    */
    static void SetRecordEffectAttribute(int attributeId) noexcept;

    /**
    * \brief Set the ID of the skill modified by the temporary effect stored on the
    * server.
    *
    * \param skillId The ID of the skill.
    * \return void
    */
    static void SetRecordEffectSkill(int skillId) noexcept;

    /**
    * \brief Set the range type of the temporary effect stored on the server (0 for
    * self, 1 for touch, 2 for target).
    *
    * \param rangeType The range type of the effect.
    * \return void
    */
    static void SetRecordEffectRangeType(unsigned int rangeType) noexcept;

    /**
    * \brief Set the area of the temporary effect stored on the server.
    *
    * \param area The area of the effect.
    * \return void
    */
    static void SetRecordEffectArea(int area) noexcept;

    /**
    * \brief Set the duration of the temporary effect stored on the server.
    *
    * \param duration The duration of the effect.
    * \return void
    */
    static void SetRecordEffectDuration(int duration) noexcept;

    /**
    * \brief Set the maximum magnitude of the temporary effect stored on the server.
    *
    * \param magnitudeMax The maximum magnitude of the effect.
    * \return void
    */
    static void SetRecordEffectMagnitudeMax(int magnitudeMax) noexcept;

    /**
    * \brief Set the minimum magnitude of the temporary effect stored on the server.
    *
    * \param magnitudeMin The minimum magnitude of the effect.
    * \return void
    */
    static void SetRecordEffectMagnitudeMin(int magnitudeMin) noexcept;

    /**
    * \brief Set the body part type of the temporary body part stored on the server
    * (which then needs to be added to ARMOR or CLOTHING records) or set the body part
    * type of the current record if it's a BODYPART.
    *
    * \param partType The type of the body part.
    * \return void
    */
    static void SetRecordBodyPartType(unsigned int partType) noexcept;

    /**
    * \brief Set the id of the male version of the temporary body part stored on the
    * server.
    *
    * \param partId The id of the body part.
    * \return void
    */
    static void SetRecordBodyPartIdForMale(const char* partId) noexcept;

    /**
    * \brief Set the id of the female version of the temporary body part stored on the
    * server.
    *
    * \param partId The id of the body part.
    * \return void
    */
    static void SetRecordBodyPartIdForFemale(const char* partId) noexcept;

    /**
    * \brief Set the id of the of the temporary inventory item stored on the server.
    *
    * \param itemId The id of the inventory item.
    * \return void
    */
    static void SetRecordInventoryItemId(const char* itemId) noexcept;

    /**
    * \brief Set the count of the of the temporary inventory item stored on the server.
    *
    * \param count The count of the inventory item.
    * \return void
    */
    static void SetRecordInventoryItemCount(unsigned int count) noexcept;

    /**
    * \brief Add a copy of the server's temporary record of the current specified
    * type to the stored records.
    *
    * In the process, the server's temporary record will automatically be cleared
    * so a new one can be set up.
    *
    * \return void
    */
    static void AddRecord() noexcept;

    /**
    * \brief Add a copy of the server's temporary effect to the temporary record
    * of the current specified type.
    *
    * In the process, the server's temporary effect will automatically be cleared
    * so a new one can be set up.
    *
    * \return void
    */
    static void AddRecordEffect() noexcept;

    /**
    * \brief Add a copy of the server's temporary body part to the temporary record
    * of the current specified type.
    *
    * In the process, the server's temporary body part will automatically be cleared
    * so a new one can be set up.
    *
    * \return void
    */
    static void AddRecordBodyPart() noexcept;

    /**
    * \brief Add a copy of the server's temporary inventory item to the temporary record
    * of the current specified type.
    *
    * In the process, the server's temporary inventory item will automatically be cleared
    * so a new one can be set up.
    *
    * Note: Any items added this way will be ignored if the record already has a valid
    *       inventoryBaseId.
    *
    * \return void
    */
    static void AddRecordInventoryItem() noexcept;

    /**
    * \brief Send a RecordDynamic packet with the current specified record type.
    *
    * \param pid The player ID attached to the packet.
    * \param sendToOtherPlayers Whether this packet should be sent to players other than the
    *                           player attached to the packet (false by default).
    * \param skipAttachedPlayer Whether the packet should skip being sent to the player attached
    *                           to the packet (false by default).
    * \return void
    */
    static void SendRecordDynamic(unsigned short pid, bool sendToOtherPlayers, bool skipAttachedPlayer) noexcept;

};

#endif //OPENMW_RECORDSDYNAMICAPI_HPP
