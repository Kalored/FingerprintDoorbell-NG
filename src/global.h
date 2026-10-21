#ifndef GLOBAL_H
#define GLOBAL_H

#include <WString.h>

extern void notifyClients(String message);
extern String getTimestampString();
extern String htmlEscape(const String &text);

#endif