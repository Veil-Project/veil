// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/veil-config.h>
#endif

#include <util/time.h>

#include <atomic>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>
#include <ctime>
#include <thread>

#include <tinyformat.h>

void UninterruptibleSleep(const std::chrono::microseconds& n) { std::this_thread::sleep_for(n); }

static std::atomic<int64_t> nMockTime(0); //!< For unit testing

int64_t GetTime()
{
    int64_t mocktime = nMockTime.load(std::memory_order_relaxed);
    if (mocktime) return mocktime;

    time_t now = time(nullptr);
    assert(now > 0);
    return now;
}

void SetMockTime(int64_t nMockTimeIn)
{
    nMockTime.store(nMockTimeIn, std::memory_order_relaxed);
}

int64_t GetMockTime()
{
    return nMockTime.load(std::memory_order_relaxed);
}

int64_t GetTimeMillis()
{
    int64_t now = (boost::posix_time::microsec_clock::universal_time() -
                   boost::posix_time::ptime(boost::gregorian::date(1970,1,1))).total_milliseconds();
    assert(now > 0);
    return now;
}

int64_t GetTimeMicros()
{
    int64_t now = (boost::posix_time::microsec_clock::universal_time() -
                   boost::posix_time::ptime(boost::gregorian::date(1970,1,1))).total_microseconds();
    assert(now > 0);
    return now;
}

int64_t GetSystemTimeInSeconds()
{
    return GetTimeMicros()/1000000;
}

void MilliSleep(int64_t n)
{

/**
 * Boost's sleep_for was uninterruptible when backed by nanosleep from 1.50
 * until fixed in 1.52. Use the deprecated sleep method for the broken case.
 * See: https://svn.boost.org/trac/boost/ticket/7238
 */
#if defined(HAVE_WORKING_BOOST_SLEEP_FOR)
    boost::this_thread::sleep_for(boost::chrono::milliseconds(n));
#elif defined(HAVE_WORKING_BOOST_SLEEP)
    boost::this_thread::sleep(boost::posix_time::milliseconds(n));
#else
//should never get here
#error missing boost sleep implementation
#endif
}

std::string FormatISO8601DateTime(int64_t nTime) {
    struct tm ts;
    time_t time_val = nTime;
#if defined(_MSC_VER) || defined(_WIN32)
    gmtime_s(&ts, &time_val);
#else
    gmtime_r(&time_val, &ts);
#endif
    return strprintf("%04i-%02i-%02iT%02i:%02i:%02iZ", ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec);
}

std::string FormatISO8601Date(int64_t nTime) {
    struct tm ts;
    time_t time_val = nTime;
#if defined(_MSC_VER) || defined(_WIN32)
    gmtime_s(&ts, &time_val);
#else
    gmtime_r(&time_val, &ts);
#endif
    return strprintf("%04i-%02i-%02i", ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday);
}

std::string FormatISO8601Time(int64_t nTime) {
    struct tm ts;
    time_t time_val = nTime;
#if defined(_MSC_VER) || defined(_WIN32)
    gmtime_s(&ts, &time_val);
#else
    gmtime_r(&time_val, &ts);
#endif
    return strprintf("%02i:%02i:%02iZ", ts.tm_hour, ts.tm_min, ts.tm_sec);
}

uint64_t ISO8601Date_Now()
{
     time_t rawtime;
     time ( &rawtime );

     return (uint64_t) rawtime;
}

uint64_t ISO8601Date_FromString(const std::string& dateString)
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    sscanf(dateString.c_str(), "%d-%d-%d:%d:%d", &year, &month, &day, &hour, &minute);
    tm time;
    time.tm_year = year - 1900; // Year since 1900
    time.tm_mon = month - 1;    // 0-11
    time.tm_mday = day;         // 1-31
    time.tm_hour = hour;        // 0-23
    time.tm_min = minute;       // 0-59
    time.tm_sec = 0;
    time_t rawtime = mktime(&time);
    return (uint64_t)rawtime;
}

bool ISO8601Date_Validate(const std::string& dateString)
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    sscanf(dateString.c_str(), "%d-%d-%d:%d:%d", &year, &month, &day, &hour, &minute);
    if (year < 1900)
        return false;
    if (month < 1 || month > 12)
        return false;
    if (day < 1)
        return false;
    if ( (month == 1 || month == 3 || month == 5 || month == 7 || month == 9 || month == 11) && day > 31 )
        return false;
    if ( (month == 4 || month == 6 || month == 8 || month == 10 || month == 12) && day > 30 )
        return false;
    if ( (month == 2) && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0) && (day != 29) ) )
        return false;
    else if ( month == 2 && day > 28)
        return false;
    if (hour > 24)
        return false;
    if (minute > 60)
        return false;

    return true;
}

