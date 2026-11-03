tableHelper = require("tableHelper")

local raceTranslations = {
    ["redguard"] = { EN = {male = "Redguard", female = "Redguard"}, RU = {male = "Редгард", female = "Редгардка"} },
    ["dark elf"] = { EN = {male = "Dunmer", female = "Dunmer"}, RU = {male = "Данмер", female = "Данмерка"} },
    ["imperial"] = { EN = {male = "Imperial", female = "Imperial"}, RU = {male = "Имперец", female = "Имперка"} },
    ["breton"] = { EN = {male = "Breton", female = "Breton"}, RU = {male = "Бретон", female = "Бретонка"} },
    ["nord"] = { EN = {male = "Nord", female = "Nord"}, RU = {male = "Норд", female = "Нордка"} },
    ["wood elf"] = { EN = {male = "Bosmer", female = "Bosmer"}, RU = {male = "Босмер", female = "Босмерка"} },
    ["high elf"] = { EN = {male = "Altmer", female = "Altmer"}, RU = {male = "Альтмер", female = "Альтмерка"} },
    ["khajiit"] = { EN = {male = "Khajiit", female = "Khajiit"}, RU = {male = "Хаджит", female = "Хаджитка"} },
    ["argonian"] = { EN = {male = "Argonian", female = "Argonian"}, RU = {male = "Аргонианин", female = "Аргонианка"} },
    ["orc"] = { EN = {male = "Orc", female = "Orc"}, RU = {male = "Орк", female = "Орчиха"} },
    ["t_cnq_chimeriquey"] = { EN = {male = "Chimeri-Quey", female = "Chimeri-Quey"}, RU = {male = "Кимери-Квей", female = "Кимери-Квей"} },
    ["t_cnq_keptu"] = { EN = {male = "Keptu-Quey", female = "Keptu-Quey"}, RU = {male = "Кепту-Квей", female = "Кепту-Квей"} },
    ["t_els_cathay"] = { EN = {male = "Cathay", female = "Cathay"}, RU = {male = "Катай", female = "Катай"} },
    ["t_els_cathay-raht"] = { EN = {male = "Cathay-raht", female = "Cathay-raht"}, RU = {male = "Катай-рат", female = "Катай-рат"} },
    ["t_els_dagi-raht"] = { EN = {male = "Dagi-raht", female = "Dagi-raht"}, RU = {male = "Даги-Рат", female = "Даги-Рат"} },
    ["t_els_ohmes"] = { EN = {male = "Ohmes", female = "Ohmes"}, RU = {male = "Ом", female = "Ом"} },
    ["t_els_ohmes-raht"] = { EN = {male = "Ohmes-raht", female = "Ohmes-raht"}, RU = {male = "Ом-рат", female = "Ом-рат"} },
    ["t_els_suthay"] = { EN = {male = "Suthay", female = "Suthay"}, RU = {male = "Сутай", female = "Сутай"} },
    ["t_hr_riverfolk"] = { EN = {male = "Riverfolk", female = "Riverfolk"}, RU = {male = "Речной народ", female = "Речной народ"} },
    ["t_mw_malahk_orc"] = { EN = {male = "Malahk Orc", female = "Malahk Orc"}, RU = {male = "Орк Малака", female = "Орчиха Малака"} },
    ["t_pya_seaelf"] = { EN = {male = "Sea Elf", female = "Sea Elf"}, RU = {male = "Морской эльф", female = "Морская эльфийка"} },
    ["t_sky_hill_giant"] = { EN = {male = "Hill Giant", female = "Hill Giant"}, RU = {male = "Горный великан", female = "Горная великанша"} },
    ["t_sky_reachman"] = { EN = {male = "Reachman", female = "Reachwoman"}, RU = {male = "Пределец", female = "Пределка"} },
    ["t_val_imga"] = { EN = {male = "Imga", female = "Imga"}, RU = {male = "Имга", female = "Имга"} },
    ["t_yne_ynesai"] = { EN = {male = "Ynesai", female = "Ynesai"}, RU = {male = "Инесай", female = "Инесай"} }
}

local function L(pid, key, variables)
    return localization.Get(pid, "coreChat", key, variables)
end

local function getTranslatedRaceName(pid, raceId, gender)
    if raceId == nil then return L(pid, "unknown_race") end
    local language = localization.GetLanguage(pid)
    local entry = raceTranslations[string.lower(raceId)]
    if entry then
        local genderKey = (gender == 1) and "male" or "female"
        return (entry[language] or entry.EN)[genderKey]
    end
    return raceId
end

local function getPlayerRankTag(pid)
    if Players[pid]:IsAdmin() then return "[A]"
    elseif Players[pid]:IsModerator() then return "[M]"
    else return "[P]" end
end

local function getRolePlayModeTag(pid)
    local custom = Players[pid].data.customVariables or {}
    return custom.RPStatus == true and "[RP]" or "[G]"
end

local function formatDays(pid, days)
    if localization.GetLanguage(pid) == "RU" then
        local d, dd = days % 10, days % 100
        local key = "day_many"
        if not (dd >= 11 and dd <= 19) then
            if d == 1 then key = "day_one"
            elseif d >= 2 and d <= 4 then key = "day_few" end
        end
        return days .. " " .. L(pid, key)
    end
    return days .. " " .. L(pid, days == 1 and "day_one" or "day_many")
end

local function buildPingAndTimeLine(requestingPid, playerIndex)
    local ping = tostring(tes3mp.GetAvgPing(Players[playerIndex].pid))
    local line = L(requestingPid, "list_ping") .. ": " .. ping
    local ts = Players[playerIndex].data.timestamps
    local creation = ts and tonumber(ts.creation) or nil
    if creation then
        line = line .. " | " .. L(requestingPid, "list_on_server") .. ": " ..
            formatDays(requestingPid, math.max(0, math.floor((os.time() - creation) / 86400)))
    end
    local lastLogin = ts and tonumber(ts.lastLogin) or nil
    if lastLogin then
        local mins = math.max(0, math.floor((os.time() - lastLogin) / 60))
        local hours, rem = math.floor(mins / 60), mins % 60
        local session
        if hours > 0 then session = hours .. " " .. L(requestingPid, "list_hour") .. " " .. rem .. " " .. L(requestingPid, "list_min")
        elseif mins > 0 then session = mins .. " " .. L(requestingPid, "list_min")
        else session = L(requestingPid, "list_less_minute") end
        line = line .. " | " .. L(requestingPid, "list_online") .. ": " .. session
    end
    return line
end

guiHelper = {}
guiHelper.names = {"LOGIN", "REGISTER", "PLAYERSLIST", "CELLSLIST"}
guiHelper.ID = tableHelper.enum(guiHelper.names)

guiHelper.ShowLogin = function(pid)
    tes3mp.PasswordDialog(pid, guiHelper.ID.LOGIN, localization.Get(pid, "core", "login_dialog_title"), "")
end

guiHelper.ShowRegister = function(pid)
    tes3mp.PasswordDialog(pid, guiHelper.ID.REGISTER,
        localization.Get(pid, "core", "register_dialog_title"),
        localization.Get(pid, "core", "register_dialog_note"))
end

local GetConnectedPlayerList = function(requestingPid)
    local lines = {}
    local isRequestingAdmin = Players[requestingPid] and Players[requestingPid]:IsAdmin()
    local lastPid = tes3mp.GetLastPlayerId()
    for playerIndex = 0, lastPid do
        if Players[playerIndex] ~= nil and Players[playerIndex]:IsLoggedIn() then
            local custom = Players[playerIndex].data.customVariables or {}
            local playerColor = custom.chatColor or color.Default
            local character = Players[playerIndex].data.character or {}
            local stats = Players[playerIndex].data.stats or {}
            local location = Players[playerIndex].data.location or {}
            local race = getTranslatedRaceName(requestingPid, character.race, character.gender)
            local level = stats.level or 1
            local header = playerColor .. getPlayerRankTag(playerIndex) .. getRolePlayModeTag(playerIndex) ..
                " PID " .. tostring(Players[playerIndex].pid) .. " [ " .. tostring(Players[playerIndex].name) ..
                ", " .. race .. ", " .. tostring(level) .. " " .. L(requestingPid, "list_level") .. " ]"

            local isGhost = custom.Ghost == 1
            local hasBounty = stats.bounty ~= nil and stats.bounty > 2000
            local canSee = isRequestingAdmin or not isGhost or hasBounty
            local cell = canSee and tostring(location.cell or "-") or L(requestingPid, "list_hidden")
            local region = canSee and tostring(location.regionName or "-") or L(requestingPid, "list_hidden")
            local block = header .. "\n" ..
                playerColor .. L(requestingPid, "list_location") .. ": [ " .. cell .. " ]\n" ..
                playerColor .. L(requestingPid, "list_region") .. ": [ " .. region .. " ]\n" ..
                playerColor .. buildPingAndTimeLine(requestingPid, playerIndex)
            if isGhost and isRequestingAdmin then
                block = block .. "\n" .. playerColor .. L(requestingPid, "ghost_marker")
            end
            lines[#lines + 1] = block
        end
    end
    return table.concat(lines, "\n")
end

local GetLoadedCellList = function()
    local list, cellCount, cellIndex = "", logicHandler.GetLoadedCellCount(), 0
    for key in pairs(LoadedCells) do
        cellIndex = cellIndex + 1
        list = list .. key .. " (auth: " .. LoadedCells[key]:GetAuthority() .. ", loaded by " ..
            LoadedCells[key]:GetVisitorCount() .. ")" .. (cellIndex == cellCount and "" or "\n")
    end
    return list
end

local GetLoadedRegionList = function()
    local list, regionCount, regionIndex = "", logicHandler.GetLoadedRegionCount(), 0
    for key in pairs(WorldInstance.storedRegions) do
        local visitorCount = WorldInstance:GetRegionVisitorCount(key)
        if visitorCount > 0 then
            regionIndex = regionIndex + 1
            list = list .. key .. " (auth: " .. WorldInstance:GetRegionAuthority(key) .. ", loaded by " .. visitorCount .. ")" ..
                (regionIndex == regionCount and "" or "\n")
        end
    end
    return list
end

local GetPlayerInventoryList = function(pid)
    local lines = {}
    for index, currentItem in ipairs(Players[pid].data.inventory) do
        lines[#lines + 1] = index .. ": " .. currentItem.refId .. " (count: " .. currentItem.count .. ")"
    end
    return table.concat(lines, "\n")
end

guiHelper.ShowPlayerList = function(pid)
    local playerCount = logicHandler.GetConnectedPlayerCount()
    tes3mp.ListBox(pid, guiHelper.ID.PLAYERSLIST, L(pid, "players_online", {count = playerCount}), GetConnectedPlayerList(pid))
end

guiHelper.ShowCellList = function(pid)
    local cellCount = logicHandler.GetLoadedCellCount()
    local label = cellCount .. " loaded cell" .. (cellCount ~= 1 and "s" or "")
    tes3mp.ListBox(pid, guiHelper.ID.CELLSLIST, label, GetLoadedCellList())
end

guiHelper.ShowRegionList = function(pid)
    local regionCount = logicHandler.GetLoadedRegionCount()
    local label = regionCount .. " loaded region" .. (regionCount ~= 1 and "s" or "")
    tes3mp.ListBox(pid, guiHelper.ID.CELLSLIST, label, GetLoadedRegionList())
end

guiHelper.ShowInventoryList = function(menuId, pid, inventoryPid)
    local inventoryCount = tableHelper.getCount(Players[pid].data.inventory)
    local label = inventoryCount .. " item" .. (inventoryCount ~= 1 and "s" or "")
    tes3mp.ListBox(pid, menuId, label, GetPlayerInventoryList(inventoryPid))
end

local function ToggleGhostCommand(pid)
    if not Players[pid] or not Players[pid]:IsLoggedIn() then return end
    Players[pid].data.customVariables = Players[pid].data.customVariables or {}
    local custom = Players[pid].data.customVariables
    if custom.Ghost == 1 then
        custom.Ghost = 0
        tes3mp.MessageBox(pid, -1, L(pid, "ghost_disabled"))
    else
        custom.Ghost = 1
        tes3mp.MessageBox(pid, -1, L(pid, "ghost_enabled"))
    end
    Players[pid]:Save()
end

customCommandHooks.registerCommand("ghost", ToggleGhostCommand)

return guiHelper
