#include "SerialLog.h"

SerialTee SerialLog;

void SerialTee::begin(unsigned long baud)
{
  Serial.begin(baud);
}

SerialTee::operator bool()
{
  return (bool)Serial;
}

size_t SerialTee::write(uint8_t c)
{
  size_t written = Serial.write(c);
  store(&c, 1);
  return written;
}

size_t SerialTee::write(const uint8_t *data, size_t size)
{
  size_t written = Serial.write(data, size);
  store(data, size);
  return written;
}

void SerialTee::store(const uint8_t *data, size_t size)
{
  unsigned long ms = millis();
  portENTER_CRITICAL(&mux);
  for (size_t i = 0; i < size; i++)
  {
    char c = (char)data[i];
    if (c == '\r')
      continue;
    if (atLineStart && c != '\n')
    {
      char prefix[16];
      int n = snprintf(prefix, sizeof(prefix), "[%lu.%03lu] ", ms / 1000, ms % 1000);
      for (int k = 0; k < n && k < (int)sizeof(prefix); k++)
        buffer[(total++) % Capacity] = prefix[k];
      atLineStart = false;
    }
    buffer[(total++) % Capacity] = c;
    if (c == '\n')
      atLineStart = true;
  }
  portEXIT_CRITICAL(&mux);
}

String SerialTee::readFrom(uint32_t from, uint32_t &next, size_t maxBytes, bool &more)
{
  char *copy = (char *)malloc(maxBytes + 1); // allocated outside of the critical section
  if (copy == nullptr)
  {
    next = from;
    more = false;
    return String();
  }
  size_t count = 0;
  portENTER_CRITICAL(&mux);
  uint32_t oldest = total > Capacity ? total - Capacity : 0;
  if (oldest < start)
    oldest = start;
  if (from < oldest)
    from = oldest;
  if (from > total) // position from before a restart of the device: start again with the oldest text
    from = oldest;
  count = total - from;
  if (count > maxBytes)
    count = maxBytes;
  for (size_t i = 0; i < count; i++)
    copy[i] = buffer[(from + i) % Capacity];
  next = from + count;
  more = next < total;
  portEXIT_CRITICAL(&mux);
  copy[count] = 0;
  String text(copy);
  free(copy);
  return text;
}

void SerialTee::clear()
{
  portENTER_CRITICAL(&mux);
  start = total;
  portEXIT_CRITICAL(&mux);
}
