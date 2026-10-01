# 1975 Corvette — Wiring Notes for the Console Taps

What the console touches in the car, circuit by circuit. **1975 factory wiring diagram**
(`docs/1975-corvette-wiring-schematic.pdf`: engine compartment sheet, instrument panel and body
sheet, printed-circuit cluster, power windows, key buzzer, power seats). Colours below marked
**diagram** are read off that sheet; the remaining **verify** items are things the sheet does not
show (the A/C harness, the fuse block labels, the radio) and must be read on the car.

The diagram is the base heater car. It does not include the factory A/C (C60) harness, so if the
car has A/C the blower and compressor wiring comes from the separate A/C diagram, not this one.

## Facts that change the plan

1. **The oil pressure gauge is electric.** 1975 was the first year: 1968–74 used a mechanical
   Bourdon-tube gauge with an oil line and no sender. The 1975–81 sender (GM electric type,
   sold as "75-81 oil pressure gauge sender", e.g. Standard PS-157) screws into the block and
   feeds the dash gauge a variable resistance. The spec's resistive-sender assumption holds.
2. **The charging gauge is an ammeter, not a voltmeter.** Corvettes before 1976 have an
   ammeter in series with the charging circuit. Do not tap it. System voltage is measured
   directly (spec §3.3, 0x49 A2 through the 47k/10k divider), which is what the design already
   does. Never put anything in series with the ammeter shunt wiring.
3. **The headlamp doors are vacuum, and the vacuum valve is inside the headlight switch.**
   Energising the lamp filaments through our parallel relay does *not* open the doors. Door
   control from the screen therefore means a 12 V vacuum solenoid valve teed into the door
   circuit in parallel with the switch's own valve, driven by the door relay, and the factory
   manual override "pull-down" valve stays in place. This is the same shape as the spec's
   lamp rule (parallel, never in series) applied to vacuum. The "R/L HDLT DOOR SOLENOID" boxes
   on the engine sheet are the door-position switches for the dash HEADLIGHT DOOR IND lamp
   (white/dark-green wire), not actuators; leave that circuit alone.
4. **The temperature sender curve in the spec is a poor starting point.** A C3 gauge
   troubleshooting paper gives a typical GM sender as ~410 Ω at 100 °F, 123 Ω at 180 °F and
   83.5 Ω at 210 °F (table below), not the 1365 Ω at 100 °F in spec §3.1. Both are hypotheses;
   the bench sweep (spec §9.2) decides. Expect the piggyback node voltage swing to be smaller
   than planned at the hot end.

## Sender circuits (the three piggyback taps)

| Channel | Sender | Wire at the sender | Resistance | Where to tap |
|---|---|---|---|---|
| Fuel | Float rheostat in the tank, grounded through the tank; **maximum resistance when full** on C1/C3 | **Tan** to the tank unit, **black** tank ground strap; at the dash the FUEL GAUGE CONN. is **tan** (sender) and **pink** (ignition feed) — diagram | GM standard 0 Ω empty → ~90 Ω full (spec §3.1); verify on the bench | Tan at the fuel gauge connector behind the console, or at the tank harness; minus input to the tank ground strap |
| Coolant | Thermistor in the intake manifold, grounded through its threads | **Dark green** at the TEMP SENDER, through bulkhead cavity **3**, to the WATER TEMP GAUGE (dk green + black) — diagram | Typical table: 75 °F 569 Ω · 100 °F 410 · 120 °F 300 · 140 °F 187 · 160 °F 150 · 180 °F 123 · 200 °F 94 · 210 °F 83.5 Ω. Gauge reads ~180 °F at ~125 Ω | Dark green at the sender connector; minus input to a bolt on the intake manifold, not the console ground (spec §3.3) |
| Oil pressure | Electric sender in the block (1975–81 only), grounded through its threads; drawn as OIL PRESS. SW. on the engine sheet | **Dark blue** at the sender, through bulkhead cavity **10**, to the OIL PRESSURE GAUGE (dk blue + pink + red) — diagram | ~0 Ω at 0 psi, ~45 Ω at 40 psi, ~90 Ω at 80 psi (owner forum figure for the 1975–81 sender; matches spec §3.1); confirm with the hand pump (spec §9.2) | Dark blue at the sender connector; minus input to the block |

Sender grounds are the block and the tank. Ground offset between them and the console is the
reason for differential measurement (spec §3.3); the coolant and oil minus inputs go to the
engine, the fuel minus input to the tank ground strap.

## Lamps and doors (spec §7)

- Headlight switch: the console harness plugs onto the original switch; the switch carries both
  the electrical contacts and the door vacuum valve, with a **black** and a **blue** vacuum hose
  on its ports (confirmed, all C3).
- LIGHT SWITCH terminals on the diagram: **red** (battery feed), **dark green** (headlamp
  output to the BEAM SELECT SW.), **brown** ×2 (park and tail), **white** and **orange**
  (courtesy lamp circuit through the switch's rotary end), **light blue** (headlight door
  indicator), **gray** (instrument lamps). Dimmer outputs **light green** (high beam, bulkhead
  13/14) and **tan** (low beam, bulkhead 15/16). The earlier "yellow to dimmer" guess was wrong.
- Parallel lamp relay: contacts across the switch's headlamp contact, i.e. **red to dark green**
  at the light switch connector. Confirm with a meter before wiring: dark green is 12 V only with
  the headlamps on, red is always live.
- Doors: 12 V 3-way vacuum solenoid valve teed into the door circuit, driven by the door relay
  output (`pins.h` kDoorRelay); the factory pull-down override valve under the column stays.
  Hose routing is in the C3 headlight vacuum functional diagrams (corvette-restoration.com).
- Headlight-door warning lamp exists 1969–76 (a switch on the door mechanism); leave it alone.

## Reverse sense (frame `rev`, spec §4.1)

- Backup lamp switch: on the transmission for 4-speed (left side, two wires down the tunnel);
  combined neutral-safety/backup switch on the column for automatic (1974–75 part).
- Wires: **light green** from the switch through bulkhead cavity **24** to the BACK-UP LIGHTs
  (diagram); the feed into the switch is an ignition-switched wire, **pink** per the 68-76
  harness instructions (the diagram shows it unlabelled).
- Tap the **light green** side: +12 V only in reverse. Through the opto to `pins.h`
  kReverseSense (active low with the pull-up).

## HVAC (spec §6)

**Heater-only car (what the factory diagram shows).** HEATER SWITCH CONN.: **brown** (feed
from the heater fuse), **yellow** and **light blue** (to the HEATER RESISTOR taps, with **orange**
as the resistor's third lead), **orange** out to the BLOWER MOTOR through bulkhead cavity **20**;
the motor grounds **black** at the engine side. Three speeds, no relay: the console needs three
relays here, and high speed is the orange lead direct.

**A/C car (C60, not on this diagram).** From a 1975 owner's trace: resistor taps **light blue**
(low), **yellow** (med 1), **green** (med 2); **dark blue** to the high-blower relay; relay
**orange** 12 V, **dark blue** trigger, **purple** to the motor, **black** ground. Four console
relays: three on the taps (motor current, 30 A relays) and one on the high relay's dark-blue
trigger (coil current only). Verify against the A/C diagram before wiring.

Check the car first: the A/C compressor and the "A/C" position on the dash control are the tell.
- A/C: **tan** from the switch to the pressure switch, then on to the compressor clutch and the
  idle solenoid. The console clutch relay goes in series with nothing: it parallels the switch
  contact upstream of the pressure switch so the low-pressure cut-out stays in the loop.
- Mode doors are vacuum on this car; the two mode solenoids in `relays.h` replace or parallel the
  vacuum switching valve (spec §6.1). Defrost keeps a mechanical path (spec §6.3).

## Power (spec §8)

- IGNITION SWITCH CONN. on the diagram: **red** (battery), **pink** (ignition, also feeds the
  gauges and bulkhead cavity **7**), **purple/white** (start), **brown/white**, **brown**,
  **orange**. GM convention puts accessory on **brown**; the diagram does not label functions,
  so identify ACC with a meter: live in ACC and RUN, dead in START and OFF.
- Fuse block feeds, GM convention confirmed by the 68-76 harness: **orange** BAT (unswitched;
  courtesy lamps and clock on this car), **pink** IGN, **tan** ACCY, **yellow** accessory.
- Master relay trigger (spec §8 pin 86): the **radio** fuse output, which is ACC-switched, so the
  console lives and dies with the key like the factory radio. The radio is not on this diagram;
  find the RADIO fuse on the block and confirm ACC behaviour with the meter.
- Ignition sense for the Pi shutdown opto: **pink** IGN feed.
- Bulkhead connector (engine side numbering, useful for a clean tap point behind the dash):
  3 dk green temp sender · 7 pink ignition · 10 dk blue oil sender · 13/14 lt green high beams ·
  15/16 tan low beams · 20 orange blower · 24 lt green back-up lamps.
- Grounds: the C3 has a fibreglass body, so every ground is a wire to the frame or the engine.
  The star point on the steel birdcage in spec §8 is the right answer; the gauge cluster's own
  ground is a known weak point (a poor cluster ground makes the temp gauge read high).

## Sources

- **1975 factory wiring diagram**, `docs/1975-corvette-wiring-schematic.pdf` (6 sheets): the
  authority for every colour marked "diagram" above.
- C3 gauge troubleshooting paper (temperature table, fuel sender wiring, mechanical oil gauge
  on earlier cars, ammeter): https://www.metroli.org/pdf/Troubleshooting%20Gauges.pdf
- American Autowire 510717 (1968–76 Corvette) instructions: backup switch pink/light green,
  fuse-block feed colours, 1975 heater/A/C harness, "68-73 oil pressure gauge was mechanical":
  https://api.americanautowire.com/shopify/instructions/92972159_510717_IN_5.0.pdf
- 75-81 oil pressure sender listings: Corvette Central 303110, Standard Motor Products PS-157;
  resistance 0/45/90 Ω at 0/40/80 psi: https://www.corvetteforum.com/forums/c3-tech-performance/2521855-testing-oil-pressure-sending-unit.html
- Ammeter before 1976: https://www.zip-corvette.com/68-82-c3/interior/corvette-gauges/ammeter-voltmeter-gauge.html
- C3 headlight vacuum functional diagrams: http://corvette-restoration.com/wp-content/uploads/technical_papers/vacuum.pdf
- 1975 blower/A/C wire colours (owner trace): https://www.corvetteforum.com/forums/c3-tech-performance/4847548-75-c3-ac-wiring.html
- Factory diagram to buy: https://www.zip-corvette.com/75-wiring-diagram.html (E-414, $13) or
  Lectric Limited VWD7500; the 1975 Assembly Instruction Manual for connector locations.
