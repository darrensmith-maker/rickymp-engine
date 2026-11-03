-- ArenaMP internal persistence layer.
-- Based on the useful DataManager concepts from Nirn, but kept independent of
-- third-party scripts and hardened for CoreScripts use.

local DataManager = {}

DataManager.configPrefix = "custom/__config_"
DataManager.dataPrefix = "custom/__data_"

local function log(level, message)
    if tes3mp ~= nil and enumerations ~= nil then
        tes3mp.LogMessage(level, "[CoreArenaMP_DataManager] " .. message)
    end
end

local function deepCopy(value, seen)
    if type(value) ~= "table" then return value end
    seen = seen or {}
    if seen[value] ~= nil then return seen[value] end
    local copy = {}
    seen[value] = copy
    for key, child in pairs(value) do
        copy[deepCopy(key, seen)] = deepCopy(child, seen)
    end
    return copy
end

local function safeScriptName(scriptName)
    return type(scriptName) == "string" and #scriptName > 0 and #scriptName <= 96
        and string.match(scriptName, "^[%w_%.%-]+$") ~= nil
end

function DataManager.IsSafeRelativePath(path)
    if type(path) ~= "string" or path == "" or #path > 512 then
        return false
    end
    path = string.gsub(path, "\\", "/")
    if string.sub(path, 1, 1) == "/" or string.match(path, "^%a:") ~= nil then
        return false
    end
    if string.find(path, "%z") ~= nil then
        return false
    end
    for component in string.gmatch(path, "[^/]+") do
        if component == ".." or component == "." or component == "" then
            return false
        end
    end
    return true
end

local function protectedCall(operation, fileName, fn)
    if not DataManager.IsSafeRelativePath(fileName) then
        log(enumerations.log.ERROR, "Rejected unsafe data path: " .. tostring(fileName))
        return false, nil
    end

    local ok, result = pcall(fn)
    if not ok then
        log(enumerations.log.ERROR, operation .. " failed for " .. fileName .. ": " .. tostring(result))
        return false, nil
    end
    return true, result
end

function DataManager.Exists(fileName)
    local ok, result = protectedCall("Exists", fileName, function()
        if jsonInterface == nil or jsonInterface.ioLibrary == nil then
            return false
        end
        local file = jsonInterface.ioLibrary.open(config.dataPath .. "/" .. fileName, "r")
        if file == nil then return false end
        file:close()
        return true
    end)
    return ok and result == true
end

function DataManager.Load(fileName)
    local ok, result = protectedCall("Load", fileName, function()
        return jsonInterface.load(fileName)
    end)
    if not ok then return nil end
    return result
end

function DataManager.Save(fileName, data, keyOrder)
    local ok, result = protectedCall("Save", fileName, function()
        return jsonInterface.save(fileName, data, keyOrder)
    end)
    return ok and result == true
end

function DataManager.Quicksave(fileName, data)
    local ok, result = protectedCall("Quicksave", fileName, function()
        return jsonInterface.quicksave(fileName, data)
    end)
    return ok and result == true
end

-- Nirn DataManager compatibility concept: recursively fill newly introduced
-- defaults without deleting user-owned values from an older configuration.
function DataManager.CheckForNils(target, defaults)
    if type(target) ~= "table" then target = {} end
    if type(defaults) ~= "table" then return target end

    for key, defaultValue in pairs(defaults) do
        if target[key] == nil then
            target[key] = deepCopy(defaultValue)
        elseif type(defaultValue) == "table" and type(target[key]) == "table" then
            DataManager.CheckForNils(target[key], defaultValue)
        end
    end
    return target
end

function DataManager.GetConfigPath(scriptName)
    if not safeScriptName(scriptName) then return nil end
    return DataManager.configPrefix .. scriptName .. ".json"
end

function DataManager.GetDataPath(scriptName)
    if not safeScriptName(scriptName) then return nil end
    return DataManager.dataPrefix .. scriptName .. ".json"
end

local function saveNamed(pathGetter, scriptName, data, keyOrder)
    local path = pathGetter(scriptName)
    if path == nil then
        log(enumerations.log.ERROR, "Rejected unsafe script name: " .. tostring(scriptName))
        return false
    end
    return DataManager.Save(path, data, keyOrder)
end

local function loadNamed(pathGetter, scriptName, defaults, keyOrder)
    local path = pathGetter(scriptName)
    if path == nil then
        log(enumerations.log.ERROR, "Rejected unsafe script name: " .. tostring(scriptName))
        return deepCopy(defaults or {})
    end

    local value = DataManager.Load(path)
    if type(value) ~= "table" then value = deepCopy(defaults or {}) end
    value = DataManager.CheckForNils(value, defaults or {})
    if not DataManager.Save(path, value, keyOrder) then
        log(enumerations.log.WARN, "Could not persist merged defaults for " .. scriptName)
    end
    return value
end

function DataManager.SaveConfiguration(scriptName, data, keyOrder)
    return saveNamed(DataManager.GetConfigPath, scriptName, data, keyOrder)
end

function DataManager.LoadConfiguration(scriptName, defaults, keyOrder)
    return loadNamed(DataManager.GetConfigPath, scriptName, defaults, keyOrder)
end

function DataManager.SaveData(scriptName, data, keyOrder)
    return saveNamed(DataManager.GetDataPath, scriptName, data, keyOrder)
end

function DataManager.LoadData(scriptName, defaults, keyOrder)
    return loadNamed(DataManager.GetDataPath, scriptName, defaults, keyOrder)
end

-- Lower-case aliases ease migration of old DataManager consumers without
-- reintroducing the global legacy DataManager object.
DataManager.getConfigPath = DataManager.GetConfigPath
DataManager.getDataPath = DataManager.GetDataPath
DataManager.checkForNils = DataManager.CheckForNils
DataManager.saveConfiguration = DataManager.SaveConfiguration
DataManager.loadConfiguration = DataManager.LoadConfiguration
DataManager.saveData = DataManager.SaveData
DataManager.loadData = DataManager.LoadData

function DataManager.ValidateDataLayout()
    local required = { "player", "cell", "world", "recordstore" }
    for _, directory in ipairs(required) do
        local probe = directory .. "/.arenamp-write-test"
        local ok, result = protectedCall("Write probe", probe, function()
            return jsonInterface.writeToFile(probe, "{}")
        end)
        if not ok or result ~= true then
            log(enumerations.log.ERROR,
                "Data directory is missing or not writable: " .. config.dataPath .. "/" .. directory)
            return false
        end
        pcall(os.remove, config.dataPath .. "/" .. probe)
    end
    return true
end

return DataManager
