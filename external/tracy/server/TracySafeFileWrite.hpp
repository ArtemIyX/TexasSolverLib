#ifndef __TRACYSAFEFILEWRITE_HPP__
#define __TRACYSAFEFILEWRITE_HPP__

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "TracyFileWrite.hpp"

namespace tracy
{

class SafeFileWrite
{
public:
    static SafeFileWrite* Open( const char* fn, FileCompression comp = FileCompression::Fast, int level = 1, int streams = -1 )
    {
        std::string final( fn );
        std::string tmp = final + ".tmp";
        auto f = FileWrite::Open( tmp.c_str(), comp, level, streams );
        if( !f ) return nullptr;
        return new SafeFileWrite( std::unique_ptr<FileWrite>( f ), std::move( final ), std::move( tmp ) );
    }

    SafeFileWrite( const SafeFileWrite& ) = delete;
    SafeFileWrite& operator=( const SafeFileWrite& ) = delete;

    FileWrite& File() { return *m_file; }
    std::pair<size_t, size_t> GetCompressionStatistics() const { return m_file->GetCompressionStatistics(); }

    bool Commit()
    {
        std::error_code ec;
        std::filesystem::rename( m_tmp, m_final, ec );
        return !ec;
    }

private:
    SafeFileWrite( std::unique_ptr<FileWrite> f, std::string final, std::string tmp )
        : m_file( std::move( f ) )
        , m_final( std::move( final ) )
        , m_tmp( std::move( tmp ) )
    {}

    std::unique_ptr<FileWrite> m_file;
    std::string m_final, m_tmp;
};

}

#endif
