# 1975 C3 Corvette Dashboard Blueprint & Dimensions
*Reference guide for 3D modeling and custom digital component design.*

---

## 1. Global Component Dimensions

### Upper Dash Pad (1970–1976 Framework)
*   **Dimensions:** 50.00" L × 12.00" W × 3.00" D *(Note: Component boxes tightly at 50" × 6" × 4" depending on structural reproduction framework).*
*   **Design Note:** Features integrated factory 4" × 6" speaker cutouts on the driver and passenger sides near the windshield intersection.

### Adjacent Center Parking Brake Console
*   **Length:** 18.50"
*   **Width:** 5.00"
*   **Maximum Height:** 5.875" (at its highest asymmetrical crest).

---

## 2. Center Instrument Gauge Cluster Bezel

### Outer Geometry & Dimensions
*   **Vertical Height:** 11.50" (measured perpendicular to the gauge slant).
*   **Top Width:** 7.25" (at the upper dash pad intersection).
*   **Upper Gauge Body Width:** 9.25" (widest flared section housing original gauges; tapers internally to 8.375").
*   **Lower Radio Body Width:** 8.875" (base width at transmission tunnel connection).
*   **Installation Angle:** Sits tilted backward at a **~15-degree angle** relative to the vehicle floorboard.

### Component & Screen Integration Limits
*   **Maximum Screen Width (Recessed/Flush):** 8.25" maximum chassis width to clear structural bezel side limits.
*   **Radio Cutout (Factory Dual-Shaft/Retrofit):** 7.00" W × 2.00" H (Standard single DIN width footprint).
*   **Behind-the-Bezel Depth Clearance:** 4.50" to 5.50" absolute maximum depth before impacting factory HVAC ducting and sub-dash infrastructure. Keep heavy components in the upper two-thirds to clear the rising transmission tunnel.

### Center Cluster Internal Gauge Spacing
The factory center cluster holds 5 smaller analog gauges (Water Temp, Oil Pressure, Clock, Fuel, Volts). If you are mapping UI graphics or physical button breakouts to line up with the original cluster layout, use this grid alignment:
*   **Individual Apertures:** Each of the 5 gauge holes has a clear diameter of **2.0625" (2-1/16 inches)**.
*   **The 2×2 Grid Structure:** The top 4 gauges are stacked in a perfect square grid. 
    *   **Horizontal Center-to-Center Spacing:** 3.125" between left and right gauge pairs.
    *   **Vertical Center-to-Center Spacing:** 2.75" between top and bottom pairs.
*   **Bottom Gauge Alignment:** The 5th aperture (traditionally the clock) sits isolated directly on the center horizontal axis line of the bezel, exactly 3.00" (center-to-center) below the bottom pair of gauges.

---

## 3. Driver-Side Speedometer & Tachometer Housing

### Outer Geometry & Dimensions
*   **Total Panel Size:** 15.50" W × 11.25" H.
*   **Gauge Openings:** Two circular cutouts measuring exactly 5.25" in diameter.
*   **Steering Column Cutout:** A tight semi-circular arch contoured for a 2.25" diameter steering column tube.
*   **Behind-the-Panel Depth Clearance:** ~5.50" of open depth cavity before reaching the firewall and primary steering brackets.

---

## 4. Passenger-Side Dash Panel & Map Pocket

Because C3 Corvettes of this generation lacked a traditional glove box, the lower right-hand panel houses a flexible, spring-loaded map pocket mechanism.

### Outer Geometry & Dimensions
*   **Total Panel Size:** Evaluated symmetrically to the driver's panel, measuring roughly **15.25" W × 11.25" H**. It contours inward along the right side kick panel and tapers smoothly down to meet the center transmission tunnel wall.
*   **Map Pocket Aperture Size:** The recessed cavity opening built into the dash pad substrate measures **11.75" W × 5.50" H**.
*   **Depth Clearance:** Directly behind the pocket sits the rigid fiberglass passenger firewall/cowl structure and the HVAC blend-air chamber. Your maximum modeling clearance depth behind this panel surface is constrained to **3.75" to 4.25"**.

---

## 5. Blender Mesh Blocking Reference Table

| Component Landmark | Dimension (Inches) | Blender Object Type (Suggested) | Modeling Notes / Modifiers |
| :--- | :--- | :--- | :--- |
| **Upper Dash Pad Span** | 50.00 × 12.00 × 3.00 | Extruded Curve / Plane | Overhangs the center bezel; check driver sightlines. |
| **Center Bezel Height** | 11.50 | Plane | Rotate **15 degrees** on X-axis for realistic ergonomics. |
| **Center Bezel Width Max**| 9.25 | Plane | Flared midpoint; tapers down to 8.875" at the base. |
| **Screen Width Max** | 8.25 | Cube / Boundary Box | Hard stop boundary box for any flush touch screen. |
| **Behind-Dash HVAC Depth**| 4.50 – 5.00 | Cube / Non-render Box | Depth layout constraint zone; use for component collision checking. |
| **Driver Pod Perimeter** | 15.50 × 11.25 | Plane | Mirror or scale along steering column center axis. |
| **Driver Gauge Openings** | 5.25 (Diameter) | Cylinder Target | Apply `Boolean (Difference)` modifier to flat driver panel plane. |
| **Steering Column Arch** | 2.25 (Diameter) | Cylinder Target | Semi-circle cut at the bottom center of driver-side panel. |
| **Center Cluster Holes** | 2.0625 (Diameter) | Array of Cylinders | Space at 3.125" (X) and 2.75" (Y) for factory analog representation. |
| **Passenger Pod Perimeter**| 15.25 × 11.25 | Plane | Flared right boundary to follow car's door weatherstripping curvature. |
| **Map Pocket Recess** | 11.75 × 5.50 × 4.00 | Box Object | Pocket structure; parent to passenger plane before extrusion. |
