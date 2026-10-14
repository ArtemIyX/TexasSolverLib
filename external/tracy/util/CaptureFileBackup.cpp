#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "CaptureFileBackup.hpp"

namespace tracy
{

static std::filesystem::path s_backupPath, s_backupOf;

static char s_errorBuf[512];

OutputPrep PrepareOutputFile( const char* path, bool overwrite, const char** error )
{
    std::error_code ec;
    const std::filesystem::path out( path );

    const auto st = std::filesystem::symlink_status( out, ec );
    const bool absent = st.type() == std::filesystem::file_type::not_found ||
                        ec == std::errc::no_such_file_or_directory;
    if( ec && !absent )
    {
        snprintf( s_errorBuf, sizeof( s_errorBuf ), "cannot access output path %s: %s", path, ec.message().c_str() );
        *error = s_errorBuf;
        return OutputPrep::Unusable;
    }
    ec.clear();
    const bool exists = !absent;
    if( exists && !std::filesystem::is_regular_file( st ) )
    {
        snprintf( s_errorBuf, sizeof( s_errorBuf ), "output path %s exists and is not a regular file", path );
        *error = s_errorBuf;
        return OutputPrep::Unusable;
    }
    if( exists && !overwrite )
    {
        *error = nullptr;
        return OutputPrep::Exists;
    }

    FILE* probe = fopen( path, "ab" );
    if( !probe )
    {
        snprintf( s_errorBuf, sizeof( s_errorBuf ), "cannot open output file %s for writing", path );
        *error = s_errorBuf;
        return OutputPrep::Unusable;
    }
    fclose( probe );

    if( exists )
    {
        auto backup = out;
        backup += "~";
        std::filesystem::rename( out, backup, ec );
        if( ec )
        {
            snprintf( s_errorBuf, sizeof( s_errorBuf ), "cannot move existing %s out of the way: %s", path, ec.message().c_str() );
            *error = s_errorBuf;
            return OutputPrep::Unusable;
        }
        s_backupOf = out;
        s_backupPath = backup;
        atexit( RestoreOutputBackup );
    }
    else
    {
        std::filesystem::remove( out, ec );
    }
    *error = nullptr;
    return OutputPrep::Ok;
}

void RestoreOutputBackup()
{
    if( s_backupOf.empty() ) return;
    std::error_code ec;
    std::filesystem::rename( s_backupPath, s_backupOf, ec );
    if( ec ) fprintf( stderr, "Could not restore %s from %s: %s\n", s_backupOf.string().c_str(), s_backupPath.string().c_str(), ec.message().c_str() );
    s_backupOf.clear();
}

void DiscardOutputBackup()
{
    if( s_backupOf.empty() ) return;
    std::error_code ec;
    std::filesystem::remove( s_backupPath, ec );
    s_backupOf.clear();
}

}
