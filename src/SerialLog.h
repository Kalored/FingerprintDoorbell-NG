#ifndef SERIALLOG_H
#define SERIALLOG_H

#include <Arduino.h>

// Replacement for "Serial" in the firmware sources (see SerialRedirect.h): everything is still written to the real serial
// port, and a copy of the last bytes is kept in a ring buffer so the web UI can show it as a serial monitor.
// Each new line gets a time prefix [seconds since boot] in the copy (not on the real serial port).
class SerialTee : public Print
{
public:
  static const size_t Capacity = 8192;

  void begin(unsigned long baud);
  explicit operator bool();
  using Print::write;
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // returns the stored text from byte position 'from' on (at most maxBytes), 'next' is the position to ask for next time,
  // 'more' is true if there is still older unread text
  String readFrom(uint32_t from, uint32_t &next, size_t maxBytes, bool &more);
  void clear();

private:
  char buffer[Capacity];
  uint32_t total = 0; // bytes stored since boot (only grows)
  uint32_t start = 0; // position of the oldest byte that may be shown (moved by clear())
  bool atLineStart = true;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  void store(const uint8_t *data, size_t size);
};

extern SerialTee SerialLog;

#endif
