-- ArenaMP internal base mechanics.
-- Inspired by Nirn_BaseScript's module tracking and protected event wrappers,
-- without overriding global require() or adding runtime /load /unload hooks.
-- Those legacy techniques can invalidate CoreScripts callbacks during reloads.

local BaseScript = {}
BaseScript.version = "1.1"
BaseScript.modules = {}

local function log(level, message)
    tes3mp.LogMessage(level, "[CoreArenaMP_BaseScript] " .. message)
end

function BaseScript.RegisterModule(name, version)
    if type(name) ~= "string" or name == "" or #name > 96 then return false end
    BaseScript.modules[name] = {
        version = tostring(version or "unknown"),
        registeredAt = os.time()
    }
    return true
end

function BaseScript.GetModules()
    return BaseScript.modules
end

function BaseScript.SafeRequire(moduleName, allowFailure)
    if type(moduleName) ~= "string" or moduleName == "" or #moduleName > 160 then
        return nil
    end
    local ok, result = pcall(require, moduleName)
    if not ok then
        local level = allowFailure and enumerations.log.WARN or enumerations.log.ERROR
        log(level, "Could not load module '" .. moduleName .. "': " .. tostring(result))
        return nil
    end
    return result
end

local function protectedCallback(owner, event, callback)
    return function(...)
        local results = { pcall(callback, ...) }
        if not results[1] then
            log(enumerations.log.ERROR,
                "Callback failure in " .. tostring(owner) .. " for " .. tostring(event) .. ": " .. tostring(results[2]))
            return customEventHooks.makeEventStatus(nil, nil)
        end
        table.remove(results, 1)
        return unpack(results)
    end
end

function BaseScript.RegisterValidator(owner, event, callback)
    if type(owner) ~= "string" or type(event) ~= "string" or type(callback) ~= "function" then return false end
    customEventHooks.registerValidator(event, protectedCallback(owner, event, callback))
    return true
end

function BaseScript.RegisterHandler(owner, event, callback)
    if type(owner) ~= "string" or type(event) ~= "string" or type(callback) ~= "function" then return false end
    customEventHooks.registerHandler(event, protectedCallback(owner, event, callback))
    return true
end

function BaseScript.Initialize()
    if CoreArenaMP_DataManager == nil then
        log(enumerations.log.ERROR, "CoreArenaMP_DataManager is not loaded")
        return false
    end

    if not CoreArenaMP_DataManager.ValidateDataLayout() then
        log(enumerations.log.ERROR,
            "Server data layout validation failed; stopping to avoid profile corruption")
        tes3mp.StopServer(2)
        return false
    end

    BaseScript.RegisterModule("CoreArenaMP_BaseScript", BaseScript.version)
    BaseScript.RegisterModule("CoreArenaMP_DataManager", "1.1")
    log(enumerations.log.INFO,
        "DataManager and native C++ data-tamper guard initialized")
    return true
end

return BaseScript
