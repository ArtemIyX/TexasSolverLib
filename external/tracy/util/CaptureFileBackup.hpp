#ifndef __CAPTUREFILEBACKUP_HPP__
#define __CAPTUREFILEBACKUP_HPP__

#include <stdint.h>

namespace tracy
{

enum class OutputPrep : uint8_t
{
    Ok,
    Exists,
    Unusable
};

OutputPrep PrepareOutputFile( const char* path, bool overwrite, const char** error );

void RestoreOutputBackup();
void DiscardOutputBackup();

}

#endif
