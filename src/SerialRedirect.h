#ifndef SERIALREDIRECT_H
#define SERIALREDIRECT_H

// Include this as the LAST include of a source file that prints to "Serial": from here on "Serial" is the tee object
// that also fills the buffer of the serial monitor in the web UI. Never include it in headers.
#include "SerialLog.h"
#define Serial SerialLog

#endif
