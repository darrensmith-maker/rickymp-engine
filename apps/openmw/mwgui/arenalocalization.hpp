#ifndef MWGUI_ARENALOCALIZATION_H
#define MWGUI_ARENALOCALIZATION_H

#include <map>
#include <string>

#include <components/to_utf8/to_utf8.hpp>

namespace VFS
{
    class Manager;
}

namespace MWGui
{
    /// Lightweight UTF-8 localisation for ArenaMP-owned UI strings.
    /// English is always loaded as the fallback language.
    class ArenaLocalization
    {
    public:
        ArenaLocalization(const VFS::Manager* vfs, ToUTF8::FromType encoding);

        std::string translate(const std::string& key) const;
        const std::string& getLanguage() const { return mLanguage; }
        void setLanguage(const std::string& language);

    private:
        using Strings = std::map<std::string, std::string>;

        static std::string normaliseLanguage(std::string language, ToUTF8::FromType encoding);
        static std::string trim(const std::string& value);
        static std::string decodeValue(const std::string& value);
        void load(const std::string& language, Strings& destination);

        const VFS::Manager* mVfs;
        ToUTF8::FromType mEncoding;
        std::string mLanguage;
        Strings mEnglish;
        Strings mCurrent;
    };
}

#endif
