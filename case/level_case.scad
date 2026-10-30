// RV-C leveling sensor case: SK Pang ESP32 CAN-Bus board + GY-521 (MPU-6050).
//
// The ESP32 pocket (standoffs, connector slot, LED light tube, lid tab over the USB-C) reproduces
// rubillos' RV-Bridge case (github.com/rubillos/RV-Bridge/tree/main/3D), measured from its STLs.
// Added here: a GY-521 bay on the side opposite the connectors, a thicker floor, four screw-down
// ears on the long sides, two M3 lid screws, and FRONT / label engraving on the lid.
// The connector end of the lid snaps in as on RV-Bridge: its two L-shaped corner blocks have a
// groove on their outside faces that clicks over a small bead on the inside of the walls.
//
// Installation: mount flat, lid up, with the FRONT arrow pointing to the front of the coach.
// Fit the GY-521 with its mounting holes toward the ESP32 and its VCC pin toward the "VCC" mark
// on the floor. The firmware's default axis map (AXIS_FORWARD = +X, AXIS_DRIVER = +Y) then applies.
//
// Hardware: 6 x M2.5 x 5 mm pan-head screws (boards; 6 mm can bottom out in the 4.2 mm pilots), 2 x M3 x 10 mm flat-head screws (lid),
//           4 x #6 or #8 countersunk wood screws (mounting).
// Print: PETG, 0.2-0.3 mm layers, 4 walls, no supports. Use `part` to export each piece.

part = "both";  // "bottom", "lid", "both" (print layout), "assembly" (preview)

/* [General] */
$fn = 48;
wall = 2;         // side wall thickness
floor_t = 2;      // floor thickness (RV-Bridge: 1; thicker keeps the sensor plane flat)
inner_h = 13;     // floor top to wall top (as RV-Bridge)
corner_r = 3;     // outside corner radius
fit = 0.2;        // printing clearance for mating parts

/* [ESP32 board: measured from RV-Bridge case] */
esp_pocket = [64, 46];                                  // pocket for the SK Pang board
esp_holes = [[3.5, 4.5], [60.5, 4.5], [3.5, 41.5], [60.5, 41.5]];  // 57 x 37 mm pattern, pocket-relative
standoff_h = 3;
standoff_d = 6;
standoff_base_d = 7;                                    // small fillet at the floor
pilot_d = 2.2;                                          // M2.5 self-tapping pilot (2.0 splits PETG layers)
slot_y = [7.5, 38.5];                                   // connector slot in the +X wall, pocket-relative
led_xy = [49, 42.5];                                    // RGB LED, pocket-relative
led_tube_od = 4.6;
led_tube_id = 3.0;
led_tube_bottom = 7.2;                                  // tube end height above the floor top
usb_tab_y = [25.0, 38.4];                               // lid tab closing the slot above the USB-C
usb_tab_depth = 3;
locator = 5.9;                                          // lid corner locators (ESP end)
locator_gap = 0.6;
locator_depth = 3.7;
locator_arm = 2;                                        // L-shaped locators flex over the retainer
retainer_r = 1.0;                                       // retainer bead radius (how far it stands out of the wall)
retainer_drop = 2.1;                                    // bead center below the wall top

/* [GY-521] */
gy_size = [15.9, 20.8];     // [along case X (toward its holes), along case Y]
gy_hole_pitch = 15.5;         // hole center-to-center, along case Y
gy_hole_inset = 2.5;        // hole center from the board's hole-side edge
gy_standoff_h = 3;          // clears the solder joints under the pin row
gy_wire_gap = 6;            // room behind the pin row for the soldered wires
gy_bay_len = 24;            // bay length along case X
gy_rail_from_edge = 3.5;    // rest rail distance from the pin-row edge (clears pin solder)
gy_rail_w = 2;

/* [Lid] */
lid_t = 2;
lid_boss_d = 7;
lid_boss_inset = 3.2;       // boss center from the inside wall faces (overlaps the walls so they fuse)
lid_pilot_d = 2.5;          // M3 self-tapping pilot
lid_pilot_depth = 10;
lid_screw_clear_d = 3.4;
lid_head_d = 6.2;           // M3 flat-head countersink
label = "RV-C LEVEL";
term_labels = ["12V", "GND", "CAN H", "CAN L"];  // terminal block, in +Y order
term_y0 = 14.25;            // 12V terminal center (outer coordinates), measured from a photo
term_pitch = 3.5;           // terminal block pitch
term_label_size = 2.4;      // caps 0.97 x size tall: keeps ~1 mm between rows after the bolding below
term_label_bold = 0.1;      // strokes widened per side, to ~0.6 mm, so the face-down first layer can resolve them
term_label_spacing = 1.25;  // letter spacing, so the bolded, first-layer-flared letters don't run together
term_label_margin = 3;      // label right edge in from the lid's connector-end edge
engrave = 0.8;
lid_chamfer = 0.4;          // 45-degree chamfer round the top-face edge (the bed side when printed)
foot_flare = 0.1;           // engraving widened per side over the first layer, against elephant's foot
foot_flare_h = 0.2;         // one first layer

/* [Mounting ears] */
ear_x = [16, 76];           // ear centers along X (outer coordinates)
ear_w = 16;
ear_len = 12;               // how far each ear sticks out from the wall
ear_t = 4;
ear_hole_d = 4.5;           // #6 / #8 wood screw clearance
ear_csk_d = 9;
ear_hole_off = 6.5;         // hole center from the outside wall face

// ---------------------------------------------------------------- derived
inner = [gy_bay_len + esp_pocket[0], esp_pocket[1]];
outer = [inner[0] + 2 * wall, inner[1] + 2 * wall];
wall_top = floor_t + inner_h;
esp0 = [wall + gy_bay_len, wall];                                // ESP pocket origin
gy0 = [wall + gy_wire_gap, wall + (inner[1] - gy_size[1]) / 2];  // GY-521 board origin
lid_screws = [[wall + lid_boss_inset, wall + lid_boss_inset],
              [wall + lid_boss_inset, outer[1] - wall - lid_boss_inset]];

// ---------------------------------------------------------------- helpers
// Connector-end lid locators, as [x, y] origins of their square footprint.
locator_x = outer[0] - wall - locator_gap - locator;
locator_ys = [wall + locator_gap, outer[1] - wall - locator_gap - locator];

// Retainer rods along the inside wall faces where the locators land, centered on the faces at
// z = 0 (the wall top). r = retainer_r gives the beads; r = retainer_r + fit cuts the grooves.
module retainer_rods(r) {
  // End wall: one per locator, along Y.
  for (y = [wall, outer[1] - wall - locator_gap - locator])
    translate([outer[0] - wall, y, -retainer_drop]) rotate([-90, 0, 0])
      cylinder(r = r, h = locator_gap + locator);
  // Side walls: along X, out to the end wall.
  for (y = [wall, outer[1] - wall])
    translate([locator_x, y, -retainer_drop]) rotate([0, 90, 0])
      cylinder(r = r, h = locator + locator_gap);
}

module rounded_rect(size, r) {
  offset(r = r) offset(delta = -r) square(size);
}

module standoff(h) {
  cylinder(d = standoff_d, h = h);
  cylinder(d1 = standoff_base_d, d2 = standoff_d, h = 0.8);
}

module countersunk_hole(d, head_d, depth) {
  // Hole going down from z = 0, countersink opening at the top.
  translate([0, 0, -depth - 1]) cylinder(d = d, h = depth + 2);
  translate([0, 0, -(head_d - d) / 2]) cylinder(d1 = d, d2 = head_d, h = (head_d - d) / 2 + 0.01);
  translate([0, 0, -0.01]) cylinder(d = head_d, h = 1);
}

// ---------------------------------------------------------------- bottom
module ears() {
  // Each ear overlaps only the wall (not the cavity) and is square where it meets the wall.
  for (x = ear_x, side = [0, 1])
    translate([x - ear_w / 2, side == 0 ? 0 : outer[1], 0])
      mirror([0, side == 0 ? 1 : 0, 0])
        translate([0, -wall, 0])
          linear_extrude(ear_t) union() {
            rounded_rect([ear_w, ear_len + wall], 3);
            square([ear_w, wall + 3]);
          }
}

module ear_holes() {
  for (x = ear_x, y = [-ear_hole_off, outer[1] + ear_hole_off])
    translate([x, y, ear_t]) countersunk_hole(ear_hole_d, ear_csk_d, ear_t);
}

module gy_hole_positions() {
  for (s = [-1, 1])
    translate([gy0[0] + gy_size[0] - gy_hole_inset, gy0[1] + gy_size[1] / 2 + s * gy_hole_pitch / 2])
      children();
}

module bottom() {
  difference() {
    union() {
      difference() {
        linear_extrude(wall_top) rounded_rect(outer, corner_r);
        translate([wall, wall, floor_t]) cube([inner[0], inner[1], inner_h + 1]);
      }
      ears();
      for (h = esp_holes) translate([esp0[0] + h[0], esp0[1] + h[1], floor_t]) standoff(standoff_h);
      gy_hole_positions() translate([0, 0, floor_t]) standoff(gy_standoff_h);
      translate([gy0[0] + gy_rail_from_edge, gy0[1] + 1, floor_t])
        cube([gy_rail_w, gy_size[1] - 2, gy_standoff_h]);
      for (p = lid_screws) translate([p[0], p[1], 0]) cylinder(d = lid_boss_d, h = wall_top);
      // Lid retainer beads at the connector end.
      intersection() {
        translate([0, 0, wall_top]) retainer_rods(retainer_r);
        linear_extrude(wall_top) rounded_rect(outer, corner_r);
      }
    }
    // Connector slot (USB-C + terminal block), from the board's underside up.
    translate([outer[0] - wall - 1, esp0[1] + slot_y[0], floor_t + standoff_h])
      cube([wall + 2, slot_y[1] - slot_y[0], inner_h]);
    // Board screw pilots (stop 0.8 mm above the underside so the floor stays closed).
    for (h = esp_holes) translate([esp0[0] + h[0], esp0[1] + h[1], 0.8]) cylinder(d = pilot_d, h = wall_top);
    gy_hole_positions() translate([0, 0, 0.8]) cylinder(d = pilot_d, h = wall_top);
    // Lid screw pilots.
    for (p = lid_screws) translate([p[0], p[1], wall_top - lid_pilot_depth]) cylinder(d = lid_pilot_d, h = lid_pilot_depth + 1);
    ear_holes();
    // Floor mark: the GY-521's VCC pin goes on this side.
    translate([gy0[0] + gy_size[0] / 2, gy0[1] + gy_size[1] + 3, floor_t - 0.6])
      linear_extrude(1) text("VCC", size = 3, halign = "center", valign = "center",
                             font = "Liberation Sans:style=Bold");
  }
}

// ---------------------------------------------------------------- lid
module front_arrow() {
  // Points toward +X, the front of the coach.
  polygon([[0, -4], [7, 0], [0, 4]]);
  translate([-12, -1.25]) square([12, 2.5]);
}

// Top-face engraving, 2D in lid coordinates: label on the lower row, "FRONT ->" on the upper row,
// terminal names beside the terminal block.
module lid_engraving() {
  translate([outer[0] / 2, 15])
    text(label, size = 6, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
  translate([outer[0] - 30, 33]) {
    text("FRONT", size = 5, halign = "right", valign = "center", font = "Liberation Sans:style=Bold");
    translate([15, 0]) front_arrow();
  }
  for (i = [0 : len(term_labels) - 1])
    translate([outer[0] - term_label_margin, term_y0 + i * term_pitch - 0.97 * term_label_size / 2])  // capitals centered on the terminal
      offset(r = term_label_bold)
        text(term_labels[i], size = term_label_size, halign = "right", valign = "baseline",
             spacing = term_label_spacing, font = "Liberation Sans:style=Bold");
}

module lid() {
  difference() {
    union() {
      // Plate, chamfered round the top-face edge so the first-layer squish fills the chamfer
      // instead of standing out as a lip.
      hull() {
        linear_extrude(lid_t - lid_chamfer) rounded_rect(outer, corner_r);
        translate([lid_chamfer, lid_chamfer, lid_t - 0.01])
          linear_extrude(0.01) rounded_rect(outer - [2, 2] * lid_chamfer, corner_r - lid_chamfer);
      }
      // Corner locators at the ESP end: L-shaped, arms along the end wall and the side wall.
      for (i = [0, 1])
        translate([locator_x, locator_ys[i], -locator_depth]) difference() {
          cube([locator, locator, locator_depth + lid_t / 2]);  // overlaps the plate so they fuse
          translate([-1, i == 0 ? locator_arm : -1, -1])
            cube([locator - locator_arm + 1, locator - locator_arm + 1, locator_depth + 1]);
        }
      // LED light tube.
      translate([esp0[0] + led_xy[0], esp0[1] + led_xy[1], -(inner_h - led_tube_bottom)])
        cylinder(d = led_tube_od, h = inner_h - led_tube_bottom);
      // Tab closing the slot above the USB-C.
      translate([outer[0] - wall, esp0[1] + usb_tab_y[0], -usb_tab_depth])
        cube([wall, usb_tab_y[1] - usb_tab_y[0], usb_tab_depth]);
    }
    translate([esp0[0] + led_xy[0], esp0[1] + led_xy[1], -inner_h])
      cylinder(d = led_tube_id, h = inner_h + lid_t + 1);
    // Retainer grooves in the locators' outside faces.
    retainer_rods(retainer_r + fit);
    for (p = lid_screws) translate([p[0], p[1], lid_t]) countersunk_hole(lid_screw_clear_d, lid_head_d, lid_t);
    // Engraving on the top face, flared wider over the first layer (the top face prints on the bed)
    // so the squished first layer fills the flare instead of narrowing the letters.
    translate([0, 0, lid_t - engrave]) linear_extrude(engrave + 0.1) lid_engraving();
    translate([0, 0, lid_t - foot_flare_h]) linear_extrude(foot_flare_h + 0.1) offset(r = foot_flare) lid_engraving();
  }
}

// ---------------------------------------------------------------- output
if (part == "bottom") {
  bottom();
} else if (part == "lid") {
  translate([0, outer[1], lid_t]) rotate([180, 0, 0]) lid();  // top face down on the bed
} else if (part == "assembly") {
  bottom();
  translate([0, 0, wall_top]) lid();
} else {
  bottom();
  translate([0, 2 * outer[1] + 2 * ear_len + 10, lid_t]) rotate([180, 0, 0]) lid();
}
