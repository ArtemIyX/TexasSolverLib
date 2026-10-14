#ifndef __CAPTUREOUTPUT_HPP__
#define __CAPTUREOUTPUT_HPP__

#include "CaptureConsole.hpp"

namespace tracy { class Worker; }

int WaitForConnection( tracy::Worker& worker );

void PrintWorkerFailure( tracy::Worker& worker );

#endif
