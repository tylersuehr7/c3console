# C3 Corvette Console

Beautiful slick embedded touch-enabled GUI console for the C3 Chevrolet Corvette (target 1975 model). Built in C++20 from scratch for a Raspberry Pi CM4 and ESP32 sensor/control board. Integrates with engine using piggyback taps on existing electronic sensors to ensure nothing is irreversible.

## What Exactly Is This?

A large portrait touchscreen display in the center console dashboard of the car. Yes, it requires physically modifying the dashboard of car and connecting to or modifying existing electronics. It shows auxiliary gauges, supports controlling HVAC/lamps/audio, and enables CarPlay so that iPhone/Android can be connected. The ESP32 (MCU) owns sensor acquisition, warning outputs, and relay controls. The Pi owns the interface and CarPlay, camera, and audio integrations. Factory gauges and the headlamp switch stay connected and defrost must remain reachable with software for safety.

> Copyright © 2026 Tyler R. Suehr

![Suehr Studios Logo](assets/suehr-studios-full-logo.svg)
