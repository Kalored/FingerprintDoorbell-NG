#ifndef GPIOCONFIG_H
#define GPIOCONFIG_H

#include <Arduino.h>

// Which GPIOs can be assigned to which function on a classic ESP32 (ESP32-WROOM-32, 30 pin DevKit).
// Never offered:
//   0, 2, 12, 15   strapping pins, they decide how the chip boots
//   1, 3           USB serial (flashing and the serial monitor)
//   6 - 11         connected to the flash chip
//   14, 5          output a signal during boot (not offered for outputs, a door opener must not click at boot)
//   34, 35, 36, 39 input only, no output and no internal pull resistor
//   16, 17         free on WROOM modules, but used by the PSRAM of WROVER modules
enum class PinRole : uint8_t
{
  sensorRx = 0, // ESP32 receives (connected to TX of the sensor)
  sensorTx,     // ESP32 sends (connected to RX of the sensor)
  touchRing,    // wake-up / touch signal of the sensor (input with pull-down)
  doorbell,     // output to the doorbell
  doorOpener,   // output to the door opener
  count
};

struct PinSet
{
  int8_t sensorRx = 16;
  int8_t sensorTx = 17;
  int8_t touchRing = 5;
  int8_t doorbell = 19;
  int8_t doorOpener = 23;

  int8_t get(PinRole role) const
  {
    switch (role)
    {
    case PinRole::sensorRx: return sensorRx;
    case PinRole::sensorTx: return sensorTx;
    case PinRole::touchRing: return touchRing;
    case PinRole::doorbell: return doorbell;
    default: return doorOpener;
    }
  }
  void set(PinRole role, int8_t pin)
  {
    switch (role)
    {
    case PinRole::sensorRx: sensorRx = pin; break;
    case PinRole::sensorTx: sensorTx = pin; break;
    case PinRole::touchRing: touchRing = pin; break;
    case PinRole::doorbell: doorbell = pin; break;
    default: doorOpener = pin; break;
    }
  }
  bool operator==(const PinSet &o) const
  {
    return sensorRx == o.sensorRx && sensorTx == o.sensorTx && touchRing == o.touchRing && doorbell == o.doorbell && doorOpener == o.doorOpener;
  }
  bool operator!=(const PinSet &o) const { return !(*this == o); }
};

namespace GpioConfig
{
inline bool inList(int pin, const int8_t *list, size_t count)
{
  for (size_t i = 0; i < count; i++)
    if (list[i] == pin)
      return true;
  return false;
}

static const int8_t inputPins[] = {4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39};
static const int8_t pulldownInputPins[] = {4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
static const int8_t outputPins[] = {4, 13, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};

inline bool isAllowed(PinRole role, int pin)
{
  switch (role)
  {
  case PinRole::sensorRx:
    return inList(pin, inputPins, sizeof(inputPins));
  case PinRole::touchRing:
    return inList(pin, pulldownInputPins, sizeof(pulldownInputPins));
  default: // sensorTx, doorbell, doorOpener are outputs
    return inList(pin, outputPins, sizeof(outputPins));
  }
}

inline const char *roleName(PinRole role)
{
  switch (role)
  {
  case PinRole::sensorRx: return "sensor RX";
  case PinRole::sensorTx: return "sensor TX";
  case PinRole::touchRing: return "touch ring";
  case PinRole::doorbell: return "doorbell output";
  default: return "door opener output";
  }
}

// checks a pin assignment: every pin must be allowed for its function and no pin may be used twice.
// extraUsed: other pins that are already taken (radar sensor, fixed custom pins), -1 = unused entry
inline bool validate(const PinSet &pins, const int8_t *extraUsed, size_t extraCount, String &error)
{
  for (uint8_t i = 0; i < (uint8_t)PinRole::count; i++)
  {
    PinRole role = (PinRole)i;
    if (!isAllowed(role, pins.get(role)))
    {
      error = String("GPIO ") + pins.get(role) + " cannot be used for " + roleName(role);
      return false;
    }
    for (uint8_t j = i + 1; j < (uint8_t)PinRole::count; j++)
    {
      if (pins.get(role) == pins.get((PinRole)j))
      {
        error = String("GPIO ") + pins.get(role) + " is selected for " + roleName(role) + " and " + roleName((PinRole)j);
        return false;
      }
    }
    for (size_t k = 0; k < extraCount; k++)
    {
      if (extraUsed[k] >= 0 && extraUsed[k] == pins.get(role))
      {
        error = String("GPIO ") + pins.get(role) + " for " + roleName(role) + " is already used by another function (radar sensor)";
        return false;
      }
    }
  }
  return true;
}
} // namespace GpioConfig

#endif
