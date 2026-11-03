#include <cstdarg>
#include <iostream>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <sstream>
#include <vector>
#include <boost/lexical_cast.hpp>
#include "TimedLog.hpp"

TimedLog *TimedLog::sTimedLog = nullptr;

TimedLog::TimedLog(int logLevel) : logLevel(logLevel)
{

}

void TimedLog::Create(int logLevel)
{
    if (sTimedLog != nullptr)
        return;
    sTimedLog = new TimedLog(logLevel);
}

void TimedLog::Delete()
{
    if (sTimedLog == nullptr)
        return;
    delete sTimedLog;
    sTimedLog = nullptr;
}

const TimedLog &TimedLog::Get()
{
    return *sTimedLog;
}

int TimedLog::GetLevel()
{
    return sTimedLog->logLevel;
}

void TimedLog::SetLevel(int level)
{
    sTimedLog->logLevel = level;
}

const char* getTime()
{
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    static char result[20] = "0000-00-00 00:00:00";

    if (local != nullptr)
        std::strftime(result, sizeof(result), "%Y-%m-%d %H:%M:%S", local);

    return result;
}

void TimedLog::print(int level, bool hasPrefix, const char *file, int line, const char *message, ...) const
{
    if (level < logLevel) return;
    std::stringstream sstr;

    if (hasPrefix)
    {

        sstr << "[" << getTime() << "] ";

        if (file != 0 && line != 0)
        {
            sstr << "[" << file << ":";
            sstr << line << "] ";
        }

        sstr << "[";
        switch (level)
        {
        case LOG_WARN:
            sstr << "WARN";
            break;
        case LOG_ERROR:
            sstr << "ERR";
            break;
        case LOG_FATAL:
            sstr << "FATAL";
            break;
        default:
            sstr << "INFO";
        }
        sstr << "]: ";

    }

    sstr << message;
    char back = *sstr.str().rbegin();
    if (back != '\n')
        sstr << '\n';
    va_list args;
    va_start(args, message);
    std::vector<char> buf((unsigned long) (vsnprintf(nullptr, 0, sstr.str().c_str(), args) + 1));
    va_end(args);
    va_start(args, message);
    vsnprintf(buf.data(), buf.size(), sstr.str().c_str(), args);
    va_end(args);
    std::cout << buf.data() << std::flush;
}

std::string TimedLog::getFilenameTimestamp()
{
    time_t rawtime = time(0);
    struct tm *timeinfo = localtime(&rawtime);
    char buffer[25];
    strftime(buffer, 25, "%Y-%m-%d-%H_%M_%S", timeinfo);
    std::string timestamp(buffer);
    return timestamp;
}
