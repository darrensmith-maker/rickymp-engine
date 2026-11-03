local friendlyFire = {}

local aliases = {
    disabled = "disabled", off = "disabled", ["false"] = "disabled", ["0"] = "disabled",
    enabled = "enabled", on = "enabled", ["true"] = "enabled", ["1"] = "enabled",
    group = "group", party = "group", allies = "group", ally = "group"
}

local function normalizeMode(mode)
    if type(mode) ~= "string" then
        return nil
    end

    return aliases[string.lower(mode)]
end

local function updateConfiguredGameSetting(mode)
    for _, setting in ipairs(config.gameSettings) do
        if setting.name == "friendly fire mode" then
            setting.value = mode
            return
        end
    end

    table.insert(config.gameSettings, { name = "friendly fire mode", value = mode })
end

--- Initialize the native rule from config.lua.
function friendlyFire.Initialize()
    local mode = normalizeMode(config.friendlyFireMode) or "group"
    config.friendlyFireMode = mode
    updateConfiguredGameSetting(mode)

    if not tes3mp.SetFriendlyFireMode(mode) then
        error("Unable to initialize friendly fire mode: " .. tostring(mode))
    end
end

--- Return the canonical mode: disabled, enabled or group.
function friendlyFire.GetMode()
    return tes3mp.GetFriendlyFireMode()
end

--- Return whether two connected players are currently treated as grouped.
function friendlyFire.ArePlayersGrouped(firstPid, secondPid)
    return tes3mp.ArePlayersAllied(firstPid, secondPid)
end

--- Return whether the attacker may damage the target under the active rule.
--- Use this from custom Lua combat, traps, scripted damage or admin systems.
function friendlyFire.CanDamage(attackerPid, targetPid)
    return tes3mp.IsFriendlyFireAllowed(attackerPid, targetPid)
end

--- Change the live mode. By default, resend server settings to all logged-in
--- players so their clients enforce the same rule immediately.
function friendlyFire.SetMode(mode, sendToPlayers)
    local normalized = normalizeMode(mode)
    if normalized == nil then
        return false
    end

    if sendToPlayers == nil then
        sendToPlayers = true
    end

    if not tes3mp.SetFriendlyFireMode(normalized) then
        return false
    end

    config.friendlyFireMode = normalized
    updateConfiguredGameSetting(normalized)

    if sendToPlayers and Players ~= nil then
        for pid, player in pairs(Players) do
            if player ~= nil and player.loggedIn then
                player:LoadSettings()
            end
        end
    end

    return true
end

return friendlyFire
