#include <iostream>
#include "LangLua.hpp"
#include <Script/Script.hpp>
#include <Script/Types.hpp>

std::set<std::string> LangLua::packagePath;
std::set<std::string> LangLua::packageCPath;

void setLuaPath(lua_State* L, const char* path, bool cpath = false)
{
    std::string field = cpath ? "cpath" : "path";
    lua_getglobal(L, "package");

    lua_getfield(L, -1, field.c_str());
    std::string cur_path = lua_tostring(L, -1);
    cur_path.append(";");
    cur_path.append(path);
    lua_pop(L, 1);
    lua_pushstring(L, cur_path.c_str());
    lua_setfield(L, -2, field.c_str());
    lua_pop(L, 1);
}

lib_t LangLua::GetInterface()
{
    return reinterpret_cast<lib_t>(lua);
}

LangLua::LangLua(lua_State *lua)
{
    this->lua = lua;
}

LangLua::LangLua()
{
    lua = luaL_newstate();
    luaL_openlibs(lua); // load all lua std libs

    std::string p, cp;
    for (auto& path : packagePath)
        p += path + ';';

    for (auto& path : packageCPath)
        cp += path + ';';

    setLuaPath(lua, p.c_str());
    setLuaPath(lua, cp.c_str(), true);

}

LangLua::~LangLua()
{

}

namespace
{
    void registerLuaFunctions(luabridge::Namespace& tes3mp)
    {
        // These four functions need custom Lua stack handling and therefore
        // remain regular lua_CFunction bindings.
        tes3mp.addCFunction("CreateTimer", LangLua::CreateTimer);
        tes3mp.addCFunction("CreateTimerEx", LangLua::CreateTimerEx);
        tes3mp.addCFunction("MakePublic", LangLua::MakePublic);
        tes3mp.addCFunction("CallPublic", LangLua::CallPublic);

        // All regular API functions are bound with their exact C++ signatures.
        // The previous dispatcher erased the signatures and called every
        // function through R(*)(...), which is not ABI-safe on Apple Silicon.
        tes3mp.addFunction("StartTimer", ScriptFunctions::StartTimer);
        tes3mp.addFunction("StopTimer", ScriptFunctions::StopTimer);
        tes3mp.addFunction("RestartTimer", ScriptFunctions::RestartTimer);
        tes3mp.addFunction("FreeTimer", ScriptFunctions::FreeTimer);
        tes3mp.addFunction("IsTimerElapsed", ScriptFunctions::IsTimerElapsed);

#undef SCRIPT_API_ENTRY
#define SCRIPT_API_ENTRY(name, function) (tes3mp.addFunction(name, function), 0)
        const int registeredFunctions[] = {
            ACTORAPI,
            BOOKAPI,
            CELLAPI,
            CHARCLASSAPI,
            CHATAPI,
            DIALOGUEAPI,
            FACTIONAPI,
            GUIAPI,
            ITEMAPI,
            MECHANICSAPI,
            MISCELLANEOUSAPI,
            POSITIONAPI,
            QUESTAPI,
            RECORDSDYNAMICAPI,
            SHAPESHIFTAPI,
            SERVERAPI,
            SETTINGSAPI,
            SPELLAPI,
            STATAPI,
            OBJECTAPI,
            WORLDSTATEAPI
        };
#undef SCRIPT_API_ENTRY
#define SCRIPT_API_ENTRY(name, function) {name, function}

        (void)registeredFunctions;
    }
}

void LangLua::LoadProgram(const char *filename)
{
    int err = 0;

    if ((err =luaL_loadfile(lua, filename)) != 0)
        throw std::runtime_error("Lua script " + std::string(filename) + " error (" + std::to_string(err) + "): \"" +
                            std::string(lua_tostring(lua, -1)) + "\"");

    luabridge::Namespace tes3mp = luabridge::getGlobalNamespace(lua).beginNamespace("tes3mp");
    registerLuaFunctions(tes3mp);
    tes3mp.endNamespace();

if ((err = lua_pcall(lua, 0, 0, 0)) != 0) // Run once script for load in memory.
    throw std::runtime_error("Lua script " + std::string(filename) + " error (" + std::to_string(err) + "): \"" +
                        std::string(lua_tostring(lua, -1)) + "\"");

}

int LangLua::FreeProgram()
{
    lua_close(lua);
    return 0;
}

bool LangLua::IsCallbackPresent(const char *name)
{
    return luabridge::getGlobal(lua, name).isFunction();
}

boost::any LangLua::Call(const char *name, const char *argl, int buf, ...)
{
    va_list vargs;
    va_start(vargs, buf);

    int n_args = (int)(strlen(argl));

    lua_getglobal(lua, name);

    for (int index = 0; index < n_args; index++)
    {
        switch (argl[index])
        {
            case 'i':
                luabridge::Stack<unsigned int>::push(lua,va_arg(vargs, unsigned int));
                break;

            case 'q':
                luabridge::Stack<signed int>::push(lua,va_arg(vargs, signed int));
                break;

            case 'l':
                luabridge::Stack<unsigned long long>::push(lua, va_arg(vargs, unsigned long long));
                break;

            case 'w':
                luabridge::Stack<signed long long>::push(lua, va_arg(vargs, signed long long));
                break;

            case 'f':
                luabridge::Stack<double>::push(lua, va_arg(vargs, double));
                break;

            case 'p':
                luabridge::Stack<void*>::push(lua, va_arg(vargs, void*));
                break;

            case 's':
                luabridge::Stack<const char*>::push(lua, va_arg(vargs, const char*));
                break;

            case 'b':
                luabridge::Stack<bool>::push(lua, (bool) va_arg(vargs, int));
                break;

            default:
                throw std::runtime_error(std::string("C++ call: Unknown argument identifier ") + argl[index]);
        }
    }

    va_end(vargs);

    luabridge::LuaException::pcall(lua, n_args, 1);
    return boost::any(luabridge::LuaRef::fromStack(lua, -1));
}

boost::any LangLua::Call(const char *name, const char *argl, const std::vector<boost::any> &args)
{
    int n_args = (int)(strlen(argl));

    lua_getglobal(lua, name);

    for (int index = 0; index < n_args; index++)
    {
        switch (argl[index])
        {
            case 'i':
                luabridge::Stack<unsigned int>::push(lua, boost::any_cast<unsigned int>(args.at(index)));
                break;

            case 'q':
                luabridge::Stack<signed int>::push(lua, boost::any_cast<signed int>(args.at(index)));
                break;

            case 'l':
                luabridge::Stack<unsigned long long>::push(lua, boost::any_cast<unsigned long long>(args.at(index)));
                break;

            case 'w':
                luabridge::Stack<signed long long>::push(lua, boost::any_cast<signed long long>(args.at(index)));
                break;

            case 'f':
                luabridge::Stack<double>::push(lua, boost::any_cast<double>(args.at(index)));
                break;

            case 'p':
                luabridge::Stack<void *>::push(lua, boost::any_cast<void *>(args.at(index)));
                break;

            case 's':
                luabridge::Stack<const char *>::push(lua, boost::any_cast<const char *>(args.at(index)));
                break;

            case 'b':
                luabridge::Stack<bool>::push(lua, boost::any_cast<int>(args.at(index)));
                break;
            default:
                throw std::runtime_error(std::string("Lua call: Unknown argument identifier ") + argl[index]);
        }
    }

    luabridge::LuaException::pcall(lua, n_args, 1);
    return boost::any(luabridge::LuaRef::fromStack(lua, -1));
}

void LangLua::AddPackagePath(const std::string& path)
{
    packagePath.emplace(path);
}

void LangLua::AddPackageCPath(const std::string& path)
{
    packageCPath.emplace(path);
}
