#ifdef _WIN32
#  include <io.h>
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#include <cstdarg>
#include <cstdio>

#include "CaptureConsole.hpp"
#include "../server/TracyMemory.hpp"
#include "../server/TracyPrint.hpp"
#include "../server/TracyWorker.hpp"

static bool s_isTerminal = false;

void InitTerminalDetection()
{
#ifdef _WIN32
    s_isTerminal = _isatty( fileno( stdout ) );
#else
    s_isTerminal = isatty( fileno( stdout ) );
#endif
}

bool IsTerminal()
{
    return s_isTerminal;
}

void AnsiPrintf( const char* ansiEscape, const char* format, ... )
{
    if( IsTerminal() )
    {
        char buf[256];
        va_list args;
        va_start( args, format );
        vsnprintf( buf, sizeof buf, format, args );
        va_end( args );
        printf( "%s%s" ANSI_RESET, ansiEscape, buf );
    }
    else
    {
        va_list args;
        va_start( args, format );
        vfprintf( stdout, format, args );
        va_end( args );
    }
}

void PrintCaptureProgress( tracy::Worker& worker, int64_t firstTime, int64_t memoryLimit )
{
    if( !IsTerminal() ) return;

    auto& lock = worker.GetMbpsDataLock();
    lock.lock();
    const auto& mbpsData = worker.GetMbpsData();
    if( mbpsData.empty() )
    {
        lock.unlock();
        return;
    }
    const auto mbps = mbpsData.back();
    const auto compRatio = worker.GetCompRatio();
    const auto netTotal = worker.GetDataTransferred();
    const auto queueSize = worker.GetSendQueueSize();
    lock.unlock();

    const char* unit = "Mbps";
    float unitsPerMbps = 1.f;
    if( mbps < 0.1f )
    {
        unit = "Kbps";
        unitsPerMbps = 1000.f;
    }
    AnsiPrintf( ANSI_ERASE_LINE ANSI_CYAN ANSI_BOLD, "\r%7.2f %s", mbps * unitsPerMbps, unit );
    printf( " /" );
    AnsiPrintf( ANSI_CYAN ANSI_BOLD, "%5.1f%%", compRatio * 100.f );
    printf( " =" );
    AnsiPrintf( ANSI_YELLOW ANSI_BOLD, "%7.2f Mbps", mbps / compRatio );
    printf( " | " );
    AnsiPrintf( ANSI_YELLOW, "Tx: " );
    AnsiPrintf( ANSI_GREEN, "%s", tracy::MemSizeToString( netTotal ) );
    printf( " | " );
    AnsiPrintf( ANSI_RED ANSI_BOLD, "%s", tracy::MemSizeToString( tracy::memUsage.load( std::memory_order_relaxed ) ) );
    if( memoryLimit > 0 )
    {
        printf( " / " );
        AnsiPrintf( ANSI_BLUE ANSI_BOLD, "%s", tracy::MemSizeToString( memoryLimit ) );
    }
    printf( " | " );
    AnsiPrintf( ANSI_RED, "%s", tracy::TimeToString( worker.GetLastTime() - firstTime ) );
    printf( " | " );
    AnsiPrintf( ANSI_RED ANSI_BOLD, "%s query backlog", tracy::RealToString( queueSize ) );
    fflush( stdout );
}
