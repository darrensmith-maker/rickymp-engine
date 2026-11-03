#include "settings.hpp"
#include "parser.hpp"

#include <sstream>
#include <utility>

#include <components/misc/stringops.hpp>

namespace Settings
{

namespace
{
    void injectArenaMPRendererDefaults(Settings::CategorySettingValueMap& settings)
    {
        // RENDER052 compatibility guard. Players frequently test a newly built
        // tes3mp-client.exe in an existing ArenaMP folder whose defaults.bin
        // predates the renderer backport. Keep those installations bootable by
        // supplying only missing defaults; canonical settings-default.cfg still
        // wins whenever it is present.
        const auto ensure = [&settings](const char* category, const char* name, const char* value)
        {
            settings.emplace(std::make_pair(std::string(category), std::string(name)), std::string(value));
        };

        ensure("Shaders", "antialias alpha test", "false");

        ensure("Post Processing", "enabled", "true");
        ensure("Post Processing", "chain", "bloomlinear, adjustments");
        ensure("Post Processing", "auto exposure speed", "0.9");
        ensure("Post Processing", "transparent postpass", "true");

        // Per-effect uniforms are intentionally not injected here. The .omwfx file is
        // the authoritative source for each uniform's default/min/max/step. This keeps
        // the settings database compatible with newly added or updated shaders and
        // avoids startup failures from stale hard-coded shader setting names.
    }
}

CategorySettingValueMap Manager::mDefaultSettings = CategorySettingValueMap();
CategorySettingValueMap Manager::mUserSettings = CategorySettingValueMap();
CategorySettingVector Manager::mChangedSettings = CategorySettingVector();
std::string Manager::mUserSettingsPath;

void Manager::clear()
{
    mDefaultSettings.clear();
    mUserSettings.clear();
    mChangedSettings.clear();
    mUserSettingsPath.clear();
}

/*
    Start of tes3mp change (major)

    Add a base64encoded argument to this function to allow unencoded files to still be opened
*/
void Manager::loadDefault(const std::string &file, bool base64encoded)
{
    SettingsFileParser parser;
    parser.loadSettingsFile(file, mDefaultSettings, base64encoded);
    injectArenaMPRendererDefaults(mDefaultSettings);
}
/*
    End of tes3mp change (major)
*/

void Manager::loadUser(const std::string &file)
{
    mUserSettingsPath = file;
    SettingsFileParser parser;
    parser.loadSettingsFile(file, mUserSettings);
}

void Manager::saveUser(const std::string &file)
{
    mUserSettingsPath = file;
    SettingsFileParser parser;
    parser.saveSettingsFile(file, mUserSettings);
}

void Manager::setUserSettingsPath(const std::string& file)
{
    mUserSettingsPath = file;
}

void Manager::saveUser()
{
    if (mUserSettingsPath.empty())
        return;

    SettingsFileParser parser;
    parser.saveSettingsFile(mUserSettingsPath, mUserSettings);
}

std::string Manager::getString(const std::string &setting, const std::string &category)
{
    CategorySettingValueMap::key_type key = std::make_pair(category, setting);
    CategorySettingValueMap::iterator it = mUserSettings.find(key);
    if (it != mUserSettings.end())
        return it->second;

    it = mDefaultSettings.find(key);
    if (it != mDefaultSettings.end())
        return it->second;

    throw std::runtime_error(std::string("Trying to retrieve a non-existing setting: ") + setting
                             + ".\nMake sure the defaults.bin file was properly installed.");
}

float Manager::getFloat (const std::string& setting, const std::string& category)
{
    const std::string& value = getString(setting, category);
    std::stringstream stream(value);
    float number = 0.f;
    stream >> number;
    return number;
}

double Manager::getDouble (const std::string& setting, const std::string& category)
{
    const std::string& value = getString(setting, category);
    std::stringstream stream(value);
    double number = 0.0;
    stream >> number;
    return number;
}

int Manager::getInt (const std::string& setting, const std::string& category)
{
    const std::string& value = getString(setting, category);
    std::stringstream stream(value);
    int number = 0;
    stream >> number;
    return number;
}

bool Manager::getBool (const std::string& setting, const std::string& category)
{
    const std::string& string = getString(setting, category);
    return Misc::StringUtils::ciEqual(string, "true");
}

osg::Vec2f Manager::getVector2 (const std::string& setting, const std::string& category)
{
    const std::string& value = getString(setting, category);
    std::stringstream stream(value);
    float x, y;
    stream >> x >> y;
    if (stream.fail())
        throw std::runtime_error(std::string("Can't parse 2d vector: " + value));
    return osg::Vec2f(x, y);
}

osg::Vec3f Manager::getVector3 (const std::string& setting, const std::string& category)
{
    const std::string& value = getString(setting, category);
    std::stringstream stream(value);
    float x, y, z;
    stream >> x >> y >> z;
    if (stream.fail())
        throw std::runtime_error(std::string("Can't parse 3d vector: " + value));
    return osg::Vec3f(x, y, z);
}

void Manager::setString(const std::string &setting, const std::string &category, const std::string &value)
{
    CategorySettingValueMap::key_type key = std::make_pair(category, setting);

    CategorySettingValueMap::iterator found = mUserSettings.find(key);
    if (found != mUserSettings.end())
    {
        if (found->second == value)
            return;
    }

    mUserSettings[key] = value;

    mChangedSettings.insert(key);
}

void Manager::setInt (const std::string& setting, const std::string& category, const int value)
{
    std::ostringstream stream;
    stream << value;
    setString(setting, category, stream.str());
}

void Manager::setFloat (const std::string &setting, const std::string &category, const float value)
{
    std::ostringstream stream;
    stream << value;
    setString(setting, category, stream.str());
}

void Manager::setDouble (const std::string &setting, const std::string &category, const double value)
{
    std::ostringstream stream;
    stream << value;
    setString(setting, category, stream.str());
}

void Manager::setBool(const std::string &setting, const std::string &category, const bool value)
{
    setString(setting, category, value ? "true" : "false");
}

void Manager::setVector2 (const std::string &setting, const std::string &category, const osg::Vec2f value)
{
    std::ostringstream stream;
    stream << value.x() << " " << value.y();
    setString(setting, category, stream.str());
}

void Manager::setVector3 (const std::string &setting, const std::string &category, const osg::Vec3f value)
{
    std::ostringstream stream;
    stream << value.x() << ' ' << value.y() << ' ' << value.z();
    setString(setting, category, stream.str());
}

void Manager::resetPendingChange(const std::string &setting, const std::string &category)
{
    CategorySettingValueMap::key_type key = std::make_pair(category, setting);
    mChangedSettings.erase(key);
}

const CategorySettingVector Manager::getPendingChanges()
{
    return mChangedSettings;
}

void Manager::resetPendingChanges()
{
    mChangedSettings.clear();
}

}
