local coreChat = {}

coreChat.config = {
    whisperDistance = 1000,
    speakDistance = 2500,
    shoutDistance = 3500,
    maxPopupMessageLength = 200,
    crossModeMaxLength = 250,
    colorMenuId = 31339,
    helpMenuId = 31341,
    hideSystemChatInRP = true
}

local CHANNEL_GLOBAL_OOC = 1
local CHANNEL_SPEAK = 2
local CHANNEL_WHISPER = 3
local CHANNEL_SHOUT = 4
local CHANNEL_LOCAL_OOC = 5

local playerColors = {
    color.SkyBlue, color.DarkSkyBlue, color.MediumSeaGreen, color.DarkSeaGreen,
    color.Pink, color.DarkPink, color.Khaki, color.DarkKhaki,
    color.Salmon, color.DarkSalmon, color.Turquoise, color.DarkTurquoise,
    color.Goldenrod, color.DarkGoldenrod, color.RosyBrown, color.DarkRosyBrown,
    color.Thistle, color.DarkThistle, color.Mint, color.DarkMint,
    color.Honeydew, color.DarkHoneydew, color.LightSteelBlue, color.DarkLightSteelBlue,
    color.Plum, color.DarkPlum, color.PeachPuff, color.DarkPeachPuff,
    color.Wheat, color.DarkWheat, color.BurlyWood, color.DarkBurlyWood,
    color.PaleGoldenrod, color.DarkPaleGoldenrod, color.AntiqueWhite, color.DarkAntiqueWhite,
    color.Moccasin, color.DarkMoccasin, color.LightSkyBlue, color.DarkLightSkyBlue
}

local lastUsedColors = {}

-- Capture the localized SendMessage wrapper before coreChat installs its RP
-- isolation filter. Player chat must be able to pass through that filter, while
-- ordinary server/system chat is hidden from players who enabled RP mode.
local playerChatSendMessage = tes3mp.SendMessage

local function L(pid, key, variables)
    return localization.Get(pid, "coreChat", key, variables)
end

local function isLoggedIn(pid)
    return Players[pid] ~= nil and Players[pid]:IsLoggedIn()
end

local function ensureCustomVariables(pid)
    if not Players[pid] or not Players[pid].data then return nil end
    if Players[pid].data.customVariables == nil then
        Players[pid].data.customVariables = {}
    end
    return Players[pid].data.customVariables
end

local function isRP(pid)
    local custom = ensureCustomVariables(pid)
    return custom ~= nil and custom.RPStatus == true
end

local function getRandomColor()
    if #playerColors == 0 then return color.White end
    return playerColors[math.random(1, #playerColors)] or color.White
end

local function ensurePlayerColor(pid)
    local custom = ensureCustomVariables(pid)
    if custom == nil then return color.White end
    if custom.chatColor == nil or custom.chatColor == "" then
        custom.chatColor = getRandomColor()
        Players[pid]:Save()
    end
    return custom.chatColor or color.White
end

local function ensureNickname(pid)
    local custom = ensureCustomVariables(pid)
    if custom == nil then return "" end
    if custom.jrpChatName == nil or custom.jrpChatName == "" then
        custom.jrpChatName = Players[pid].name
    end
    return custom.jrpChatName
end

local function initializePlayer(pid)
    local custom = ensureCustomVariables(pid)
    if custom == nil then return end
    if custom.chat_channel == nil then custom.chat_channel = CHANNEL_SPEAK end
    if custom.popupMode == nil then custom.popupMode = false end
    if custom.RPStatus == nil then custom.RPStatus = false end
    ensureNickname(pid)
    ensurePlayerColor(pid)
end

local function sendMessageBoxSafe(pid, message)
    if not isLoggedIn(pid) then return end
    if #message > coreChat.config.maxPopupMessageLength then
        message = message:sub(1, coreChat.config.maxPopupMessageLength - 3) .. "..."
    end
    tes3mp.MessageBox(pid, -1, message)
end

local function sendCrossModeMessageBox(pid, message, direction)
    if not isLoggedIn(pid) then return end
    local limit = coreChat.config.crossModeMaxLength
    local footerKey = direction == "rp_to_nonrp" and "cross_rp_footer" or "cross_ooc_footer"
    local footer = color.Gray .. L(pid, footerKey, {limit = limit})
    local available = limit - #footer - 3
    if available < 16 then available = 16 end
    if #message > available then
        message = message:sub(1, available) .. "..."
    end
    tes3mp.MessageBox(pid, -1, message .. footer)
end

local function formatMessage(message, pid)
    local codes = {
        ["\\r"] = color.Red, ["\\g"] = color.DarkSeaGreen, ["\\b"] = color.SkyBlue,
        ["\\y"] = color.Yellow, ["\\w"] = color.White, ["\\s"] = color.Silver,
        ["\\d"] = color.Default
    }
    local parts = {}
    local currentIndex = 1
    local lastColor = lastUsedColors[pid] or color.White
    local custom = ensureCustomVariables(pid)
    if custom then custom.popupMessages = nil end

    while currentIndex <= #message do
        local earliestCode, earliestPos, codeType, replacementColor, matchEnd
        for code, colorValue in pairs(codes) do
            local pos = message:find(code, currentIndex, true)
            if pos and (earliestPos == nil or pos < earliestPos) then
                earliestCode, earliestPos, codeType, replacementColor = code, pos, "color", colorValue
                matchEnd = pos + #code - 1
            end
        end
        local s, e = message:find("%(%(%(.-%)%)%)", currentIndex)
        if s and (earliestPos == nil or s < earliestPos) then
            earliestCode, earliestPos, codeType, replacementColor, matchEnd = message:sub(s, e), s, "popup", lastColor, e
        end
        local ss, se = message:find("%*.-%*", currentIndex)
        if ss and (earliestPos == nil or ss < earliestPos) then
            earliestCode, earliestPos, codeType, replacementColor, matchEnd = message:sub(ss, se), ss, "star", color.White, se
        end

        if earliestPos then
            if earliestPos > currentIndex then
                parts[#parts + 1] = lastColor .. message:sub(currentIndex, earliestPos - 1)
            end
            if codeType == "color" then
                lastColor = replacementColor
                parts[#parts + 1] = lastColor
            elseif codeType == "popup" then
                local popupText = earliestCode:match("%(%(%((.-)%)%)%)")
                if popupText and custom then
                    custom.popupMessages = custom.popupMessages or {}
                    custom.popupMessages[#custom.popupMessages + 1] = lastColor .. popupText
                end
            elseif codeType == "star" then
                local starText = earliestCode:match("%*(.-)%*")
                if starText then parts[#parts + 1] = color.Plum .. starText end
                lastColor = replacementColor
            end
            currentIndex = matchEnd + 1
        else
            parts[#parts + 1] = lastColor .. message:sub(currentIndex)
            break
        end
    end
    lastUsedColors[pid] = lastColor
    return table.concat(parts)
end

local function clearPopupFragments(pid)
    local custom = ensureCustomVariables(pid)
    if custom then custom.popupMessages = nil end
end

local function sendPopupFragments(senderPid, receiverPid, prefix, crossDirection)
    local custom = ensureCustomVariables(senderPid)
    if not custom or not custom.popupMessages then return end
    for _, popupText in ipairs(custom.popupMessages) do
        local message = prefix .. popupText
        if crossDirection then
            sendCrossModeMessageBox(receiverPid, message, crossDirection)
        else
            sendMessageBoxSafe(receiverPid, message)
        end
    end
end

local function getCellVisitors(pid)
    if not Players[pid] or not Players[pid].data or not Players[pid].data.location then return {} end
    local cell = Players[pid].data.location.cell
    if not cell or not logicHandler.IsCellLoaded(cell) or not LoadedCells[cell] then return {} end
    return LoadedCells[cell].visitors or {}
end

local function sameMode(a, b)
    return isRP(a) == isRP(b)
end

local function sendLocalSpeech(pidSender, message, state)
    if not isLoggedIn(pidSender) then return end
    initializePlayer(pidSender)
    local radius, tag = coreChat.config.speakDistance, ""
    if state == "whisper" then
        radius, tag = coreChat.config.whisperDistance, color.SkyBlue .. L(pidSender, "whisper_tag")
    elseif state == "shout" then
        radius, tag = coreChat.config.shoutDistance, color.LightCoral .. L(pidSender, "shout_tag")
    end

    local formatted = formatMessage(message, pidSender)
    local senderColor = ensurePlayerColor(pidSender)
    local rpName = ensureNickname(pidSender)
    local senderX, senderY = tes3mp.GetPosX(pidSender), tes3mp.GetPosY(pidSender)

    for _, receiverPid in pairs(getCellVisitors(pidSender)) do
        if isLoggedIn(receiverPid) and Players[receiverPid].data.location.cell == Players[pidSender].data.location.cell then
            initializePlayer(receiverPid)
            local dx = senderX - tes3mp.GetPosX(receiverPid)
            local dy = senderY - tes3mp.GetPosY(receiverPid)
            if math.sqrt(dx * dx + dy * dy) < radius then
                local receiverCustom = ensureCustomVariables(receiverPid)
                if sameMode(pidSender, receiverPid) then
                    local line = senderColor .. rpName .. tag .. ": " ..
                        ((receiverPid == pidSender) and color.LightSteelBlue or color.White) .. formatted
                    if receiverCustom.popupMode then sendMessageBoxSafe(receiverPid, line)
                    else playerChatSendMessage(receiverPid, line .. "\n", false) end
                    sendPopupFragments(pidSender, receiverPid, senderColor .. rpName .. ": ")
                elseif isRP(pidSender) then
                    local prefix = color.LightCoral .. "[RP] " .. senderColor .. rpName .. tag .. ": "
                    sendCrossModeMessageBox(receiverPid, prefix .. formatted, "rp_to_nonrp")
                    sendPopupFragments(pidSender, receiverPid, prefix, "rp_to_nonrp")
                else
                    local prefix = color.Turquoise .. "[L] " .. senderColor .. Players[pidSender].name .. tag .. ": "
                    sendCrossModeMessageBox(receiverPid, prefix .. formatted, "nonrp_to_rp")
                    sendPopupFragments(pidSender, receiverPid, prefix, "nonrp_to_rp")
                end
            end
        end
    end
    clearPopupFragments(pidSender)
end

local function sendGlobalOOC(pid, message)
    if not isLoggedIn(pid) then return end
    initializePlayer(pid)
    if isRP(pid) then
        tes3mp.MessageBox(pid, -1, color.Red .. L(pid, "ooc_blocked_in_rp"))
        return
    end
    local formatted = formatMessage(message, pid)
    local playerColor = ensurePlayerColor(pid)
    for receiverPid, player in pairs(Players) do
        if player and player:IsLoggedIn() then
            initializePlayer(receiverPid)
            local prefix = color.Turquoise .. "[G] " .. playerColor .. Players[pid].name .. " (" .. pid .. "): "
            if isRP(receiverPid) then
                sendCrossModeMessageBox(receiverPid, prefix .. formatted, "nonrp_to_rp")
                sendPopupFragments(pid, receiverPid, prefix, "nonrp_to_rp")
            else
                local custom = ensureCustomVariables(receiverPid)
                if custom.popupMode then sendMessageBoxSafe(receiverPid, prefix .. formatted)
                else playerChatSendMessage(receiverPid, prefix .. color.Khaki .. formatted .. "\n", false) end
                sendPopupFragments(pid, receiverPid, prefix)
            end
        end
    end
    clearPopupFragments(pid)
end

local function sendLocalOOC(pid, message)
    if not isLoggedIn(pid) then return end
    initializePlayer(pid)
    if isRP(pid) then
        tes3mp.MessageBox(pid, -1, color.Red .. L(pid, "ooc_blocked_in_rp"))
        return
    end
    local formatted = formatMessage(message, pid)
    local playerColor = ensurePlayerColor(pid)
    for _, receiverPid in pairs(getCellVisitors(pid)) do
        if isLoggedIn(receiverPid) then
            initializePlayer(receiverPid)
            local prefix = color.Yellow .. "[L] " .. playerColor .. Players[pid].name .. " (" .. pid .. "): "
            if isRP(receiverPid) then
                sendCrossModeMessageBox(receiverPid, prefix .. formatted, "nonrp_to_rp")
                sendPopupFragments(pid, receiverPid, prefix, "nonrp_to_rp")
            else
                local custom = ensureCustomVariables(receiverPid)
                if custom.popupMode then sendMessageBoxSafe(receiverPid, prefix .. formatted)
                else playerChatSendMessage(receiverPid, prefix .. color.Khaki .. formatted .. "\n", false) end
                sendPopupFragments(pid, receiverPid, prefix)
            end
        end
    end
    clearPopupFragments(pid)
end

local function sendRoleplayAction(pid, commandName, text)
    if not isLoggedIn(pid) or text == "" then return end
    initializePlayer(pid)
    local formatted = formatMessage(text, pid)
    local playerColor = ensurePlayerColor(pid)
    local rpName = ensureNickname(pid)
    local resultText = ""
    if commandName == "try" then
        local result = math.random(0, 3)
        local resultColor = result <= 1 and color.Red or color.Turquoise
        resultText = resultColor .. L(pid, "try_" .. result) .. " "
    end

    for _, receiverPid in pairs(getCellVisitors(pid)) do
        if isLoggedIn(receiverPid) then
            initializePlayer(receiverPid)
            local normalLine
            if commandName == "do" then
                normalLine = color.DarkKhaki .. formatted
            else
                normalLine = color.Red .. "| " .. playerColor .. rpName .. " " .. resultText .. color.SlateGray .. formatted
            end
            if sameMode(pid, receiverPid) then
                if ensureCustomVariables(receiverPid).popupMode then sendMessageBoxSafe(receiverPid, normalLine)
                else playerChatSendMessage(receiverPid, normalLine .. "\n", false) end
                sendPopupFragments(pid, receiverPid, playerColor .. rpName .. ": ")
            else
                local direction = isRP(pid) and "rp_to_nonrp" or "nonrp_to_rp"
                local tag = isRP(pid) and ("[RP-" .. commandName .. "] ") or ("[OOC-" .. commandName .. "] ")
                local prefix = (isRP(pid) and color.LightCoral or color.Turquoise) .. tag
                sendCrossModeMessageBox(receiverPid, prefix .. normalLine, direction)
                sendPopupFragments(pid, receiverPid, prefix, direction)
            end
        end
    end
    clearPopupFragments(pid)
end

local function sendGlobalRP(pid, text)
    if not isLoggedIn(pid) then return end
    initializePlayer(pid)
    if not isRP(pid) then
        tes3mp.MessageBox(pid, -1, color.Red .. L(pid, "rp_required"))
        return
    end
    local formatted = formatMessage(text, pid)
    local name = ensurePlayerColor(pid) .. ensureNickname(pid)
    for receiverPid, player in pairs(Players) do
        if player and player:IsLoggedIn() then
            if isRP(receiverPid) then
                tes3mp.MessageBox(receiverPid, -1, L(receiverPid, "rp_global_from", {name = name, message = formatted}))
            else
                local body = color.LightCoral .. "[RP-Global] " ..
                    L(receiverPid, "rp_global_from", {name = name, message = formatted})
                sendCrossModeMessageBox(receiverPid, body, "rp_to_nonrp")
            end
        end
    end
    clearPopupFragments(pid)
end

local function showColorMenu(pid)
    if not isLoggedIn(pid) then return end
    local list = {}
    for i, currentColor in ipairs(playerColors) do
        list[#list + 1] = currentColor .. "[" .. i .. "] ▊ " .. L(pid, string.format("color_name_%02d", i))
    end
    tes3mp.ListBox(pid, coreChat.config.colorMenuId, L(pid, "color_menu_title"), table.concat(list, "\n"))
end

local function onColorChoice(pid, index)
    if not isLoggedIn(pid) then return end
    local colorIndex = tonumber(index) and tonumber(index) + 1 or nil
    if colorIndex and playerColors[colorIndex] then
        ensureCustomVariables(pid).chatColor = playerColors[colorIndex]
        Players[pid]:Save()
        tes3mp.MessageBox(pid, -1, playerColors[colorIndex] .. L(pid, "color_selected", {
            index = colorIndex, name = L(pid, string.format("color_name_%02d", colorIndex))
        }) .. color.Default)
    end
end

local function setChannel(pid, channel, feedbackKey, optionalText, speechState)
    if not isLoggedIn(pid) then return end
    initializePlayer(pid)
    ensureCustomVariables(pid).chat_channel = channel
    if optionalText and optionalText ~= "" then
        if channel == CHANNEL_GLOBAL_OOC then sendGlobalOOC(pid, optionalText)
        elseif channel == CHANNEL_LOCAL_OOC then sendLocalOOC(pid, optionalText)
        else sendLocalSpeech(pid, optionalText, speechState) end
    else
        tes3mp.MessageBox(pid, -1, color.SlateGray .. L(pid, feedbackKey) .. color.Default)
    end
end

local function getCommandText(cmd, startIndex)
    return tableHelper.concatenateFromIndex(cmd, startIndex or 2) or ""
end

local function commandSpeech(pid, cmd)
    local name = string.lower(cmd[1] or "")
    local text = getCommandText(cmd, 2)
    if name == "w" or name == "whisper" then setChannel(pid, CHANNEL_WHISPER, "channel_whisper", text, "whisper")
    elseif name == "s" or name == "say" then setChannel(pid, CHANNEL_SPEAK, "channel_speak", text, "speak")
    elseif name == "sh" or name == "shout" then setChannel(pid, CHANNEL_SHOUT, "channel_shout", text, "shout") end
end

local function commandRP(pid, cmd)
    local text = getCommandText(cmd, 2)
    if text ~= "" then
        sendGlobalRP(pid, text)
        return
    end
    initializePlayer(pid)
    local custom = ensureCustomVariables(pid)
    custom.RPStatus = not custom.RPStatus
    if custom.RPStatus then custom.chat_channel = CHANNEL_SPEAK end
    Players[pid]:Save()
    tes3mp.MessageBox(pid, -1, custom.RPStatus and L(pid, "rp_enabled") or L(pid, "rp_disabled"))
end

local function commandPopup(pid)
    if not isLoggedIn(pid) then return end
    initializePlayer(pid)
    local custom = ensureCustomVariables(pid)
    custom.popupMode = not custom.popupMode
    Players[pid]:Save()
    tes3mp.MessageBox(pid, -1, custom.popupMode and L(pid, "popup_enabled") or L(pid, "popup_disabled"))
end

local function commandHelp(pid)
    tes3mp.CustomMessageBox(pid, coreChat.config.helpMenuId, L(pid, "help_title") .. L(pid, "help_text"), "OK")
end

local function commandAction(pid, cmd)
    sendRoleplayAction(pid, string.lower(cmd[1] or ""), getCommandText(cmd, 2))
end

local function rawSlashValidator(eventStatus, pid, message)
    if not isLoggedIn(pid) then return end

    -- Legacy ArenaMP/Nirn user-facing slash layout. CoreScripts removes the first
    -- slash before command dispatch, so the old visible commands were:
    --   //    -> local OOC channel
    --   ///   -> global OOC channel
    --   ////  -> staff popup
    -- Handle them here before the stock command parser so bare mode switches do
    -- not become "Not a valid command" errors.
    local raw = tostring(message or "")

    if raw == "////" or raw:sub(1, 5) == "//// " then
        local text = raw == "////" and "" or raw:sub(6)
        if not Players[pid]:IsAdmin() then
            tes3mp.SendMessage(pid, color.Red .. L(pid, "admin_only") .. color.Default .. "\n", false)
        elseif text == "" then
            tes3mp.SendMessage(pid, color.SlateGray .. L(pid, "admin_popup_usage") .. color.Default .. "\n", false)
        else
            local senderColor = config.rankColors.admin or color.Red
            if Players[pid]:IsServerOwner() then
                senderColor = config.rankColors.serverOwner or senderColor
            end
            for receiverPid, player in pairs(Players) do
                if player and player:IsLoggedIn() then
                    tes3mp.MessageBox(receiverPid, -1, senderColor .. "[ADMIN] " .. color.White ..
                        Players[pid].name .. ": " .. formatMessage(text, pid))
                end
            end
            clearPopupFragments(pid)
        end
        return customEventHooks.makeEventStatus(false, nil)
    elseif raw == "///" or raw:sub(1, 4) == "/// " then
        if isRP(pid) then
            tes3mp.MessageBox(pid, -1, color.Red .. L(pid, "ooc_blocked_in_rp"))
        else
            local text = raw == "///" and "" or raw:sub(5)
            setChannel(pid, CHANNEL_GLOBAL_OOC, "channel_global", text)
        end
        return customEventHooks.makeEventStatus(false, nil)
    elseif raw == "//" or raw:sub(1, 3) == "// " then
        if isRP(pid) then
            tes3mp.MessageBox(pid, -1, color.Red .. L(pid, "ooc_blocked_in_rp"))
        else
            local text = raw == "//" and "" or raw:sub(4)
            setChannel(pid, CHANNEL_LOCAL_OOC, "channel_local_ooc", text)
        end
        return customEventHooks.makeEventStatus(false, nil)
    elseif raw:sub(1, 1) == "/" then
        -- Leave ordinary /commands (/w, /s, /sh, /rp, /color, etc.) to
        -- customCommandHooks/commandHandler.
        return
    end

    initializePlayer(pid)
    local channel = ensureCustomVariables(pid).chat_channel
    if channel == CHANNEL_WHISPER then sendLocalSpeech(pid, raw, "whisper")
    elseif channel == CHANNEL_SHOUT then sendLocalSpeech(pid, raw, "shout")
    elseif channel == CHANNEL_GLOBAL_OOC then sendGlobalOOC(pid, raw)
    elseif channel == CHANNEL_LOCAL_OOC then sendLocalOOC(pid, raw)
    else sendLocalSpeech(pid, raw, "speak") end
    return customEventHooks.makeEventStatus(false, nil)
end

local function onGUIAction(eventStatus, pid, idGui, data)
    if idGui == coreChat.config.colorMenuId and tonumber(data) ~= nil then
        onColorChoice(pid, tonumber(data))
    end
    return eventStatus
end

local function onFinishLogin(eventStatus, pid)
    if isLoggedIn(pid) then
        initializePlayer(pid)
        -- Preserve an explicitly chosen RP state, but always begin a new session
        -- in normal speech instead of inheriting a temporary OOC channel.
        ensureCustomVariables(pid).chat_channel = CHANNEL_SPEAK
    end
    return eventStatus
end

local function onDisconnect(eventStatus, pid)
    lastUsedColors[pid] = nil
    return eventStatus
end

-- Old ArenaMP RP isolation: while RP is active, the normal server chat stream
-- (join/leave notices, command text, script notices and other SendMessage output)
-- is hidden completely. Messages authored by players are delivered by coreChat:
-- RP -> RP remains in chat, while cross-mode traffic is shown in MessageBox.
local rpSystemFilterInstalled = false

local function shouldHideSystemChat(pid)
    if coreChat.config.hideSystemChatInRP ~= true then return false end
    return isLoggedIn(pid) and isRP(pid)
end

local function installRPSystemChatFilter()
    if rpSystemFilterInstalled then return end

    local systemSendMessage = tes3mp.SendMessage

    tes3mp.SendMessage = function(pid, message, sendToOtherPlayers, skipAttachedPlayer)
        sendToOtherPlayers = sendToOtherPlayers == true
        skipAttachedPlayer = skipAttachedPlayer == true

        if sendToOtherPlayers then
            -- A native broadcast cannot exclude RP recipients. Split it into
            -- localized targeted sends so RP players do not receive system chat.
            local deliveredAttached = false
            if Players ~= nil then
                for recipientPid, player in pairs(Players) do
                    if not (skipAttachedPlayer and recipientPid == pid) and not shouldHideSystemChat(recipientPid) then
                        systemSendMessage(recipientPid, message, false, false)
                    end
                    if recipientPid == pid then deliveredAttached = true end
                end
            end

            -- Preserve early connection messages for a pid that has not yet been
            -- inserted into Players. RP mode does not exist before login anyway.
            if not skipAttachedPlayer and not deliveredAttached then
                systemSendMessage(pid, message, false, false)
            end
            return
        end

        if not skipAttachedPlayer and shouldHideSystemChat(pid) then
            return
        end

        systemSendMessage(pid, message, false, skipAttachedPlayer)
    end

    rpSystemFilterInstalled = true
end

coreChat.IsRP = isRP
coreChat.GetPlayerColor = ensurePlayerColor
coreChat.GetDisplayName = function(pid)
    if not Players[pid] then return "" end
    return ensureNickname(pid)
end
coreChat.ShowColorMenu = showColorMenu
coreChat.SendLocalMessage = sendLocalSpeech
coreChat.SendPlayerChat = function(pid, message)
    playerChatSendMessage(pid, message, false, false)
end
coreChat.IsSystemChatHidden = shouldHideSystemChat

customEventHooks.registerHandler("OnGUIAction", onGUIAction)
customEventHooks.registerHandler("OnPlayerFinishLogin", onFinishLogin)
customEventHooks.registerHandler("OnPlayerDisconnect", onDisconnect)
customEventHooks.registerValidator("OnPlayerSendMessage", rawSlashValidator)

customCommandHooks.registerCommand("color", function(pid) showColorMenu(pid) end)
customCommandHooks.registerCommand("w", commandSpeech)
customCommandHooks.registerCommand("whisper", commandSpeech)
customCommandHooks.registerCommand("s", commandSpeech)
customCommandHooks.registerCommand("say", commandSpeech)
customCommandHooks.registerCommand("sh", commandSpeech)
customCommandHooks.registerCommand("shout", commandSpeech)
customCommandHooks.registerCommand("me", commandAction)
customCommandHooks.registerCommand("do", commandAction)
customCommandHooks.registerCommand("try", commandAction)
customCommandHooks.registerCommand("rp", commandRP)
customCommandHooks.registerCommand("popup", commandPopup)
customCommandHooks.registerCommand("?", commandHelp)
customCommandHooks.registerCommand("chathelp", commandHelp)

-- Book recording from the old RolePlayMode is intentionally not ported.
installRPSystemChatFilter()
tes3mp.LogMessage(enumerations.log.INFO, "[coreChat] ArenaMP RP chat loaded (RU/EN, no book recording)")

return coreChat
