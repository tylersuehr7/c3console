// Pin assignments for the ESP32-S3 DevKitC-1 on the bench board.
//
// BENCH ASSIGNMENTS. Nothing here comes from the spec; confirm every line against the relay
// board and the carrier PCB before the loom is made, then freeze this file. All relay and
// solenoid outputs go through driver transistors on the relay board and are active-high.
//
// Avoided on purpose: GPIO0/3/45/46 (strapping), GPIO19/20 (USB), GPIO26-37 (flash and PSRAM on
// the N8R8 module), GPIO38 and GPIO48 (onboard RGB LED, board revision dependent), GPIO43/44
// (UART0 on USB). The ADC channel table is spec §3.3; the relay outputs follow c3fw::Output.
#pragma once

#include <cstdint>

namespace pins {

// UART1 to the Pi (spec §4: 115200 8N1). UART0 stays on USB for logs.
constexpr int kPiTx = 17;
constexpr int kPiRx = 18;

// I2C to both ADS1115 (0x48, 0x49). DevKitC-1 defaults.
constexpr int kI2cSda = 8;
constexpr int kI2cScl = 9;

// Warning path (spec §4.3, invariant 2): driven directly from here, never gated on UART.
constexpr int kWarnLed = 4;
constexpr int kBuzzer = 5;

// Reverse-lamp sense through the opto: pulled up, low while reversing.
constexpr int kReverseSense = 6;

// Relay board outputs, in c3fw::Output order.
constexpr int kBlower1 = 10;
constexpr int kBlower2 = 11;
constexpr int kBlower3 = 12;
constexpr int kBlower4 = 13;
constexpr int kAcClutch = 14;
constexpr int kFloorSolenoid = 40;
constexpr int kDefrostSolenoid = 41;
constexpr int kHeadlampRelay = 21;  // PARALLEL to the factory switch (spec §7)
constexpr int kHighBeamRelay = 15;  // PARALLEL to the dimmer's high-beam output (spec §7)
constexpr int kDoorRelay = 42;

constexpr std::uint8_t kAdcFuelOil = 0x48;  // A0-A1 fuel, A2-A3 oil
constexpr std::uint8_t kAdcCltVolt = 0x49;  // A0-A1 coolant, A2 single-ended volts

}  // namespace pins
