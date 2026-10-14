#include <chrono>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <thread>

#include "CaptureOutput.hpp"
#include "../public/common/TracyProtocol.hpp"
#include "../public/common/TracyStackFrames.hpp"
#include "../server/TracyWorker.hpp"

int WaitForConnection( tracy::Worker& worker )
{
    while( !worker.HasData() )
    {
        const auto handshake = worker.GetHandshakeStatus();
        if( handshake == tracy::HandshakeProtocolMismatch )
        {
            printf( "\nThe client you are trying to connect to uses incompatible protocol version.\nMake sure you are using the same Tracy version on both client and server.\n" );
            return 1;
        }
        if( handshake == tracy::HandshakeNotAvailable )
        {
            printf( "\nThe client you are trying to connect to is no longer able to sent profiling data,\nbecause another server was already connected to it.\nYou can do the following:\n\n  1. Restart the client application.\n  2. Rebuild the client application with on-demand mode enabled.\n" );
            return 2;
        }
        if( handshake == tracy::HandshakeDropped )
        {
            printf( "\nThe client you are trying to connect to has disconnected during the initial\nconnection handshake. Please check your network configuration.\n" );
            return 3;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
    return 0;
}

void PrintWorkerFailure( tracy::Worker& worker )
{
    const auto& failure = worker.GetFailureType();
    if( failure == tracy::Worker::Failure::None ) return;

    AnsiPrintf( ANSI_RED ANSI_BOLD, "\nInstrumentation failure: %s", tracy::Worker::GetFailureString( failure ) );
    auto& fd = worker.GetFailureData();
    if( !fd.message.empty() )
    {
        printf( "\nContext: %s", fd.message.c_str() );
    }
    if( fd.callstack != 0 )
    {
        AnsiPrintf( ANSI_BOLD, "\nFailure callstack:\n" );
        auto& cs = worker.GetCallstack( fd.callstack );
        int fidx = 0;
        for( auto& entry : cs )
        {
            auto frameData = worker.GetCallstackFrame( entry );
            if( !frameData )
            {
                printf( "%3i. %p\n", fidx++, (void*)worker.GetCanonicalPointer( entry ) );
            }
            else
            {
                const auto fsz = frameData->size;
                for( uint8_t f = 0; f < fsz; f++ )
                {
                    const auto& frame = frameData->data[f];
                    auto txt = worker.GetString( frame.name );

                    if( fidx == 0 && f != fsz - 1 )
                    {
                        auto test = tracy::s_tracyStackFrames;
                        bool match = false;
                        do
                        {
                            if( strcmp( txt, *test ) == 0 )
                            {
                                match = true;
                                break;
                            }
                        }
                        while( *++test );
                        if( match ) continue;
                    }

                    if( f == fsz - 1 )
                    {
                        printf( "%3i. ", fidx++ );
                    }
                    else
                    {
                        AnsiPrintf( ANSI_BLACK ANSI_BOLD, "inl. " );
                    }
                    AnsiPrintf( ANSI_CYAN, "%s  ", txt );
                    txt = worker.GetString( frame.file );
                    if( frame.line == 0 )
                    {
                        AnsiPrintf( ANSI_YELLOW, "(%s)", txt );
                    }
                    else
                    {
                        AnsiPrintf( ANSI_YELLOW, "(%s:%" PRIu32 ")", txt, frame.line );
                    }
                    if( frameData->imageName.Active() )
                    {
                        AnsiPrintf( ANSI_MAGENTA, " %s\n", worker.GetString( frameData->imageName ) );
                    }
                    else
                    {
                        printf( "\n" );
                    }
                }
            }
        }
    }
}
