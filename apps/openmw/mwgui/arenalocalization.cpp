#include "arenalocalization.hpp"

#include <algorithm>
#include <cctype>
#include <istream>

#include <components/debug/debuglog.hpp>
#include <components/settings/settings.hpp>
#include <components/vfs/manager.hpp>

namespace MWGui
{
    ArenaLocalization::ArenaLocalization(const VFS::Manager* vfs, ToUTF8::FromType encoding)
        : mVfs(vfs)
        , mEncoding(encoding)
        , mLanguage(normaliseLanguage(Settings::Manager::getString("interface language", "General"), encoding))
    {
        load("en", mEnglish);
        if (mLanguage != "en")
            load(mLanguage, mCurrent);
    }

    void ArenaLocalization::setLanguage(const std::string& language)
    {
        const std::string normalized = normaliseLanguage(language, mEncoding);
        if (normalized == mLanguage)
            return;

        mLanguage = normalized;
        mCurrent.clear();
        if (mLanguage != "en")
            load(mLanguage, mCurrent);
    }

    std::string ArenaLocalization::translate(const std::string& key) const
    {
        const auto current = mCurrent.find(key);
        if (current != mCurrent.end())
            return current->second;

        const auto english = mEnglish.find(key);
        if (english != mEnglish.end())
            return english->second;

        Log(Debug::Warning) << "ArenaMP localisation key is missing: " << key;
        return key;
    }

    std::string ArenaLocalization::normaliseLanguage(std::string language, ToUTF8::FromType encoding)
    {
        std::transform(language.begin(), language.end(), language.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        language = trim(language);

        if (language.empty() || language == "auto")
            return encoding == ToUTF8::WINDOWS_1251 ? "ru" : "en";
        if (language == "ru" || language == "ru-ru" || language == "russian")
            return "ru";
        if (language == "en" || language == "en-us" || language == "en-gb" || language == "english")
            return "en";

        Log(Debug::Warning) << "Unsupported ArenaMP interface language '" << language
            << "', falling back to English";
        return "en";
    }

    std::string ArenaLocalization::trim(const std::string& value)
    {
        const auto first = std::find_if_not(value.begin(), value.end(),
            [](unsigned char c) { return std::isspace(c) != 0; });
        if (first == value.end())
            return {};
        const auto last = std::find_if_not(value.rbegin(), value.rend(),
            [](unsigned char c) { return std::isspace(c) != 0; }).base();
        return std::string(first, last);
    }

    std::string ArenaLocalization::decodeValue(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());
        bool escaped = false;
        for (char c : value)
        {
            if (!escaped)
            {
                if (c == '\\')
                    escaped = true;
                else
                    result += c;
                continue;
            }

            switch (c)
            {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default: result += c; break;
            }
            escaped = false;
        }
        if (escaped)
            result += '\\';
        return result;
    }

    void ArenaLocalization::load(const std::string& language, Strings& destination)
    {
        if (!mVfs)
            return;

        const std::string path = "l10n/arenamp/" + language + ".ini";
        if (!mVfs->exists(path))
        {
            Log(Debug::Warning) << "ArenaMP localisation file is missing: " << path;
            return;
        }

        try
        {
            auto stream = mVfs->get(path);
            std::string line;
            while (std::getline(*stream, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const std::string clean = trim(line);
                if (clean.empty() || clean.front() == '#' || clean.front() == ';')
                    continue;

                const std::size_t equals = clean.find('=');
                if (equals == std::string::npos)
                    continue;

                const std::string key = trim(clean.substr(0, equals));
                const std::string value = decodeValue(trim(clean.substr(equals + 1)));
                if (!key.empty())
                    destination[key] = value;
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to load ArenaMP localisation file " << path << ": " << e.what();
        }
    }
}
