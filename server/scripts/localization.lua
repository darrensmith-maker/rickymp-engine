local localization = {}

local dictionaries = {}
local native = {}
local wrappersInstalled = false

-- ArenaMP hard fallback for the messages that are needed before and during login.
-- Never let an internal localization key (for example "core.welcome_login") leak
-- into the client if a dictionary fails to load or an older config is mixed in.
local missingLookupWarnings = {}

local function safeColor(name)
    if type(color) == "table" and type(color[name]) == "string" then
        return color[name]
    end
    return ""
end

local function getCoreConfigText(language, key)
    if type(config) ~= "table" then
        return nil
    end

    if key == "welcome_login" or key == "welcome_register" then
        local startup = config.startupMessage
        if type(startup) == "table" then
            local langTable = startup[language]
            if type(langTable) ~= "table" then
                langTable = startup.EN or startup.RU
            end
            if type(langTable) == "table" then
                local mode = key == "welcome_login" and "login" or "register"
                if type(langTable[mode]) == "string" then
                    return langTable[mode]
                end
            end
        end
    elseif key == "chat_instructions" then
        if language == "EN" and type(config.chatWindowInstructionsEN) == "string" then
            return config.chatWindowInstructionsEN
        end
        if type(config.chatWindowInstructions) == "string" then
            return config.chatWindowInstructions
        end
    elseif key == "startup_welcome" then
        if type(config.startupScriptsInstructions) == "string" then
            return config.startupScriptsInstructions
        end
    end

    return nil
end

local function getCoreFallback(language, key)
    local cDefault = safeColor("Default")
    local cGood = safeColor("PaleGreen")
    local cBad = safeColor("RosyBrown")
    local cSky = safeColor("SkyBlue")
    local cAccent = safeColor("Turquoise")

    local fallback = {
        EN = {
            command_required = "Please enter a command after /.\n",
            command_invalid = "Unknown command. Use /help for the command list.\n",
            login_dialog_title = "Enter your password:",
            register_dialog_title = "Create new password:",
            register_dialog_note = "Use a unique password for this server.",
            login_success = cGood .. "You have successfully logged in.\n" .. cDefault,
            register_success = cGood .. "You have successfully registered.\n" .. cDefault,
            incorrect_password = cBad .. "Incorrect password!\n" .. cDefault,
            password_empty = cBad .. "Password can not be empty.\n" .. cDefault,
            account_banned = cBad .. "{name}" .. cDefault .. " is banned from this server.\n",
            auth_failed = cBad .. "{name}" .. cDefault .. " failed to log in.\n",
            max_ip_clients = cBad .. "{name}" .. cDefault .. " was kicked: the server allows up to {max} clients from one IP address.\n",
            player_joined = cGood .. "{name} joined the server.\n" .. cDefault,
            player_left = cBad .. "{name} left the server.\n" .. cDefault,
            welcome_login = cSky .. "Welcome to the server\nArenaMP, " .. cAccent .. "{name}" .. cDefault ..
                "\nPlayers online: {count}.\nYou have {seconds} sec. to log in.\n",
            welcome_register = cSky .. "Welcome to the server\nArenaMP, " .. cAccent .. "{name}" .. cDefault ..
                "\nPlayers online: {count}.\nYou have {seconds} sec. to register.\n",
            chat_instructions = "[Chat - Y] [Hide chat - F5] [All available commands - /help]\n",
            startup_welcome = " \n"
        },
        RU = {
            command_required = "После символа / укажите команду.\n",
            command_invalid = "Неизвестная команда. Используйте /help для списка команд.\n",
            login_dialog_title = "Введите пароль:",
            register_dialog_title = "Создайте новый пароль:",
            register_dialog_note = "Используйте уникальный пароль для этого сервера.",
            login_success = cGood .. "Вы успешно авторизовались.\n" .. cDefault,
            register_success = cGood .. "Вы успешно зарегистрировались.\n" .. cDefault,
            incorrect_password = cBad .. "Неверный пароль!\n" .. cDefault,
            password_empty = cBad .. "Пароль не может быть пустым.\n" .. cDefault,
            account_banned = cBad .. "{name}" .. cDefault .. " заблокирован на этом сервере.\n",
            auth_failed = cBad .. "{name}" .. cDefault .. " не успел авторизоваться.\n",
            max_ip_clients = cBad .. "{name}" .. cDefault .. " был кикнут: сервер допускает максимум {max} клиентов с одного IP адреса.\n",
            player_joined = cGood .. "{name} зашёл(ла) на сервер.\n" .. cDefault,
            player_left = cBad .. "{name} покинул(а) сервер.\n" .. cDefault,
            welcome_login = cSky .. "Добро пожаловать на сервер\nArenaMP, " .. cAccent .. "{name}" .. cDefault ..
                "\nНа сервере сейчас {count} игрок(ов).\nУ вас есть {seconds} сек. чтобы авторизоваться.\n",
            welcome_register = cSky .. "Добро пожаловать на сервер\nArenaMP, " .. cAccent .. "{name}" .. cDefault ..
                "\nНа сервере сейчас {count} игрок(ов).\nУ вас есть {seconds} сек. чтобы зарегистрироваться.\n",
            chat_instructions = "[Написать в чат - Y] [Скрыть чат - F5] [Все доступные команды - /help]\n",
            startup_welcome = " \n"
        }
    }

    local langTable = fallback[language] or fallback.EN
    return langTable[key] or fallback.EN[key]
end

local function warnMissingLookup(namespace, key)
    local id = tostring(namespace) .. "." .. tostring(key)
    if missingLookupWarnings[id] then
        return
    end
    missingLookupWarnings[id] = true

    if tes3mp ~= nil and tes3mp.LogMessage ~= nil and enumerations ~= nil and enumerations.log ~= nil then
        tes3mp.LogMessage(enumerations.log.ERROR,
            "Missing localization entry " .. id .. "; using safe fallback instead of exposing the key to clients")
    end
end

local function normalizeLanguage(language)
    if type(language) ~= "string" then
        return "EN"
    end

    language = string.upper(language)
    if language == "RU" or language == "RU-RU" or language == "RUSSIAN" then
        return "RU"
    end
    return "EN"
end

local function getConfiguredServerLanguage()
    if config == nil or type(config.serverLanguage) ~= "string" then
        return "AUTO"
    end

    local language = string.upper(config.serverLanguage)
    if language == "RU" or language == "EN" then
        return language
    end
    return "AUTO"
end

local function getClientLanguage(pid)
    -- The engine-side PlayerBaseInfo flag is the source of truth. Do not read
    -- Players[pid].language first: BasePlayer initializes that field to EN,
    -- which used to mask a later RU selection made in the ArenaMP login card.
    if type(pid) == "number" and tes3mp.GetLanguage ~= nil then
        local ok, language = pcall(tes3mp.GetLanguage, pid)
        if ok and type(language) == "string" and language ~= "" then
            local normalized = normalizeLanguage(language)
            -- Keep the Lua-side convenience field synchronized for existing
            -- scripts that read Players[pid].language directly.
            if Players ~= nil and Players[pid] ~= nil then
                Players[pid].language = normalized
            end
            return normalized
        end
    end

    if type(pid) == "number" and Players ~= nil and Players[pid] ~= nil and Players[pid].language ~= nil then
        return normalizeLanguage(Players[pid].language)
    end

    return normalizeLanguage(config ~= nil and config.defaultLanguage or "EN")
end

local function getEffectiveLanguage(pid)
    local serverLanguage = getConfiguredServerLanguage()
    if serverLanguage ~= "AUTO" then
        return serverLanguage
    end
    return getClientLanguage(pid)
end

local function resolveLanguage(pidOrLanguage)
    if type(pidOrLanguage) == "number" then
        return getEffectiveLanguage(pidOrLanguage)
    end
    return normalizeLanguage(pidOrLanguage)
end

local function applyVariables(text, variables)
    if type(text) ~= "string" or type(variables) ~= "table" then
        return text
    end

    return (text:gsub("{([%w_]+)}", function(name)
        local value = variables[name]
        if value == nil then
            return "{" .. name .. "}"
        end
        return tostring(value)
    end))
end

local function mergeTable(destination, source)
    if type(source) ~= "table" then
        return
    end
    for key, value in pairs(source) do
        destination[key] = value
    end
end

local function getAutomaticTable(language, tableName)
    local result = {}
    for _, dictionary in pairs(dictionaries) do
        local automatic = dictionary.automatic
        if type(automatic) == "table" and type(automatic[language]) == "table" then
            mergeTable(result, automatic[language][tableName])
        end
    end
    return result
end

local function getAutomaticList(language, tableName)
    local result = {}
    for _, dictionary in pairs(dictionaries) do
        local automatic = dictionary.automatic
        if type(automatic) == "table" and type(automatic[language]) == "table" then
            local source = automatic[language][tableName]
            if type(source) == "table" then
                for _, value in ipairs(source) do
                    table.insert(result, value)
                end
            end
        end
    end
    return result
end

local function normalizeLegacyStartupText(language, text)
    if type(text) ~= "string" or text == "" then
        return text
    end

    -- Older CoreScripts builds may still send their original verbose startup
    -- banner directly. Collapse it to ArenaMP's compact config message before
    -- normal localization, even for EN clients. This also protects mixed
    -- installs where an older config.lua is accidentally left in place.
    if string.find(text, "/invite <pid>", 1, true) ~= nil and
        string.find(text, "Chat Window Mode", 1, true) ~= nil and
        string.find(text, "friendly fire", 1, true) ~= nil then

        if language == "RU" and type(config) == "table" and type(config.chatWindowInstructions) == "string" then
            return config.chatWindowInstructions
        elseif type(config) == "table" and type(config.chatWindowInstructionsEN) == "string" then
            return config.chatWindowInstructionsEN
        end
    end

    -- Old ArenaMP startupScriptsInstructions contained a separate welcome line.
    -- The current greeting already contains the welcome and online count, so
    -- suppress the duplicate legacy line.
    if string.find(text, "Welcome to ArenaMP!", 1, true) ~= nil and #text < 128 then
        if type(config) == "table" and type(config.startupScriptsInstructions) == "string" then
            return config.startupScriptsInstructions
        end
        return " \n"
    end

    return text
end

local function translateAutomatic(language, text)
    text = normalizeLegacyStartupText(language, text)

    if language == "EN" or type(text) ~= "string" or text == "" then
        return text
    end

    local exact = getAutomaticTable(language, "exact")
    if exact[text] ~= nil then
        return exact[text]
    end

    local translated = text

    for _, rule in ipairs(getAutomaticList(language, "patterns")) do
        if type(rule) == "table" and type(rule.pattern) == "string" and rule.replacement ~= nil then
            local updated, count = translated:gsub(rule.pattern, rule.replacement)
            if count > 0 then
                translated = updated
            end
        end
    end

    local phrases = getAutomaticTable(language, "phrases")
    local ordered = {}
    for source, replacement in pairs(phrases) do
        table.insert(ordered, {source = source, replacement = replacement})
    end
    table.sort(ordered, function(left, right)
        return #left.source > #right.source
    end)

    for _, entry in ipairs(ordered) do
        translated = translated:gsub(entry.source:gsub("([^%w])", "%%%1"), function()
            return entry.replacement
        end)
    end

    return translated
end

--- Register a dictionary that can be reused by any server script.
--- Expected format:
--- {
---   EN = { key = "English text {name}" },
---   RU = { key = "Русский текст {name}" },
---   automatic = { RU = { exact = {}, patterns = {}, phrases = {} } }
--- }
function localization.RegisterDictionary(namespace, dictionary)
    if type(namespace) ~= "string" or namespace == "" or type(dictionary) ~= "table" then
        return false
    end

    dictionaries[namespace] = dictionary
    return true
end

function localization.LoadDictionary(namespace, moduleName)
    if type(moduleName) ~= "string" or moduleName == "" then
        return false
    end

    local ok, dictionary = pcall(require, moduleName)
    if not ok then
        tes3mp.LogMessage(enumerations.log.ERROR,
            "Unable to load localization dictionary " .. tostring(moduleName) .. ": " .. tostring(dictionary))
        return false
    end

    return localization.RegisterDictionary(namespace, dictionary)
end

function localization.GetLanguage(pid)
    return getEffectiveLanguage(pid)
end

function localization.GetClientLanguage(pid)
    return getClientLanguage(pid)
end

function localization.GetServerLanguage()
    return getConfiguredServerLanguage()
end

function localization.SetServerLanguage(language)
    if config == nil or type(language) ~= "string" then
        return false
    end

    language = string.upper(language)
    if language ~= "AUTO" and language ~= "RU" and language ~= "EN" then
        return false
    end

    config.serverLanguage = language
    return true
end

function localization.SetPlayerLanguage(pid, language)
    if Players == nil or Players[pid] == nil then
        return false
    end

    Players[pid].language = normalizeLanguage(language)
    return true
end

-- Refresh the Lua-side flag from the current engine-side PlayerBaseInfo value.
-- This is useful after clients change language during the pre-login flow.
function localization.RefreshPlayerLanguage(pid)
    if type(pid) ~= "number" then
        return "EN"
    end
    return getClientLanguage(pid)
end

function localization.Get(pidOrLanguage, namespace, key, variables)
    local language = resolveLanguage(pidOrLanguage)

    -- These values intentionally live in config.lua. Resolve them before the
    -- dictionary so the old compact ArenaMP welcome/config format remains the
    -- single source of truth.
    if namespace == "core" then
        local configText = getCoreConfigText(language, key)
        if configText ~= nil then
            return applyVariables(configText, variables)
        end
    end

    local dictionary = dictionaries[namespace]
    if type(dictionary) ~= "table" then
        warnMissingLookup(namespace, key)
        if namespace == "core" then
            local fallback = getCoreFallback(language, key)
            if fallback ~= nil then
                return applyVariables(fallback, variables)
            end
        end
        -- Do not ever expose internal namespace.key tokens in the game chat.
        return ""
    end

    local languageTable = dictionary[language]
    local englishTable = dictionary.EN
    local text = nil

    if type(languageTable) == "table" then
        text = languageTable[key]
    end
    if text == nil and type(englishTable) == "table" then
        text = englishTable[key]
    end
    if text == nil then
        warnMissingLookup(namespace, key)
        if namespace == "core" then
            text = getCoreFallback(language, key)
        end
        if text == nil then
            return ""
        end
    end

    return applyVariables(text, variables)
end

function localization.TranslateText(pidOrLanguage, text)
    return translateAutomatic(resolveLanguage(pidOrLanguage), text)
end

local function translateButtonList(pid, buttons)
    if type(buttons) ~= "string" or buttons == "" then
        return buttons
    end

    local translated = {}
    local startIndex = 1
    while true do
        local separator = string.find(buttons, ";", startIndex, true)
        if separator == nil then
            table.insert(translated, localization.TranslateText(pid, string.sub(buttons, startIndex)))
            break
        end
        table.insert(translated, localization.TranslateText(pid, string.sub(buttons, startIndex, separator - 1)))
        startIndex = separator + 1
    end
    return table.concat(translated, ";")
end

function localization.Message(pid, namespace, key, variables, sendToOtherPlayers, skipAttachedPlayer)
    tes3mp.SendMessage(pid, localization.Get(pid, namespace, key, variables),
        sendToOtherPlayers == true, skipAttachedPlayer == true)
end

function localization.MessageBox(pid, id, namespace, key, variables)
    tes3mp.MessageBox(pid, id, localization.Get(pid, namespace, key, variables))
end

function localization.GetDictionaries()
    return dictionaries
end

local function sendLocalizedBroadcast(attachedPid, message, skipAttachedPlayer)
    if Players == nil then
        native.SendMessage(attachedPid, message, true, skipAttachedPlayer == true)
        return
    end

    local recipients = {}
    for recipientPid, player in pairs(Players) do
        if player ~= nil and not (skipAttachedPlayer == true and recipientPid == attachedPid) then
            recipients[recipientPid] = localization.TranslateText(recipientPid, message)
        end
    end

    -- During early connection validation, the attached pid may not have been
    -- inserted into Players yet, but it must still receive its own message.
    if not skipAttachedPlayer and recipients[attachedPid] == nil then
        recipients[attachedPid] = localization.TranslateText(attachedPid, message)
    end

    local firstText = nil
    local allTextsMatch = true
    local recipientCount = 0
    for _, translated in pairs(recipients) do
        recipientCount = recipientCount + 1
        if firstText == nil then
            firstText = translated
        elseif translated ~= firstText then
            allTextsMatch = false
            break
        end
    end

    -- Keep the original single broadcast packet whenever every recipient uses
    -- the same text. Mixed-language servers fall back to targeted packets.
    if recipientCount == 0 or allTextsMatch then
        native.SendMessage(attachedPid, firstText or message, true, skipAttachedPlayer == true)
        return
    end

    for recipientPid, translated in pairs(recipients) do
        native.SendMessageTo(attachedPid, recipientPid, translated)
    end
end

--- Install transparent wrappers around all CoreScripts chat and GUI output.
--- Existing scripts are localized without having to replace every SendMessage call.
function localization.InstallWrappers()
    if wrappersInstalled then
        return
    end

    native.SendMessage = tes3mp.SendMessage
    native.SendMessageTo = tes3mp.SendMessageTo
    native.MessageBox = tes3mp.MessageBox
    native.CustomMessageBox = tes3mp.CustomMessageBox
    native.InputDialog = tes3mp.InputDialog
    native.PasswordDialog = tes3mp.PasswordDialog
    native.ListBox = tes3mp.ListBox

    tes3mp.SendMessage = function(pid, message, sendToOtherPlayers, skipAttachedPlayer)
        sendToOtherPlayers = sendToOtherPlayers == true
        skipAttachedPlayer = skipAttachedPlayer == true

        if sendToOtherPlayers then
            sendLocalizedBroadcast(pid, message, skipAttachedPlayer)
        elseif not skipAttachedPlayer then
            native.SendMessage(pid, localization.TranslateText(pid, message), false, false)
        end
    end

    tes3mp.MessageBox = function(pid, id, label)
        native.MessageBox(pid, id, localization.TranslateText(pid, label))
    end

    tes3mp.CustomMessageBox = function(pid, id, label, buttons)
        native.CustomMessageBox(pid, id, localization.TranslateText(pid, label),
            translateButtonList(pid, buttons))
    end

    tes3mp.InputDialog = function(pid, id, label, note)
        native.InputDialog(pid, id, localization.TranslateText(pid, label),
            localization.TranslateText(pid, note))
    end

    tes3mp.PasswordDialog = function(pid, id, label, note)
        native.PasswordDialog(pid, id, localization.TranslateText(pid, label),
            localization.TranslateText(pid, note))
    end

    tes3mp.ListBox = function(pid, id, label, items)
        native.ListBox(pid, id, localization.TranslateText(pid, label),
            localization.TranslateText(pid, items))
    end

    wrappersInstalled = true
end

localization.NormalizeLanguage = normalizeLanguage

return localization
