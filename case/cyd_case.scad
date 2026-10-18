// Slim two-part press-fit case for the ESP32-2432S028R ("Cheap Yellow Display").
// Portrait, USB at the bottom. No screws or extra parts: the board drops face-down
// into the front shell (pins locate it in its mounting holes) and the back plate
// snaps in, clamping the PCB between bezel standoffs and back posts.
//
// Print: front face-down (accent inlay = first layers), back plate outer-face-down.
// Export: see build.py (STLs + a two-colour Bambu Studio 3MF).

part = "assembly"; // [assembly, section, exploded, front, front_body, front_accent, back, fit_test, board]

explode = 0;       // assembly view: move the back plate out by this much

/* [Fit] */
clr = 0.20;        // board edge to wall, per side
plate_clr = 0.15;  // back plate to wall, per side
bead = 0.45;       // snap bead height (groove in the front is 0.5 deep, 1.0 tall)
crush = 0.25;      // crush ribs on the back plate's short edges (beyond the wall)
preload = 0.0;     // back posts touch the PCB (positive = press into it)

/* [Shape] */
wall = 2.0;
face = 1.6;          // front plate thickness
back_t = 1.6;        // back plate thickness
outer_r = 6;         // outer corner radius
edge_chamfer = 1.2;  // front edge chamfer
back_chamfer = 0.4;

/* [Features] */
accent = true;       // inlay ring around the screen
moon = true;         // crescent inlay in the bottom bezel
sensor_hole = true;  // window for the light sensor (LDR) on the front
keyholes = true;     // two keyhole slots on the back for wall screws

$fn = 64;

// ---- Board (manufacturer drawing + STEP model) ---------------------------------
// Case frame: x across the board, y up (USB end at -y), z out of the screen.
// PCB back face at z = 0.
pcb = [50, 86, 1.5];
pcb_r = 1.0;                                    // cavity corner radius
holes = [[-21, -39], [21, -39], [-21, 39], [21, 39]];  // 4 mm from the edges
hole_d = 3.2;
module_top = 5.0;                               // display module front
glass_top = 5.1;                                // touch glass front
glass = [45, 59];
glass_c = [0, 2.5];                             // glass is off-centre, away from USB
view = [44.6, 58.6];                            // window: active 43.2 x 57.6 + margin
// USB ports on the bottom edge (micro-USB + USB-C board, measured):
// [centre x, plug overmold width, receptacle width, height]
ports = [[-11.9, 10.8, 7.4, 2.5],                // micro-USB
         [-1.1, 12.6, 9.2, 3.2]];                // USB-C
port_margin = 0.6;                               // extra room around the plugs
plug_top = 2.25;                                 // top of the opening (plugs are ~7 mm tall)
back_depth = 4.7;                               // tallest part on the back (buttons)
sensor = [-14, 39.5];                           // LDR on the front

// ---- Derived ---------------------------------------------------------------------
cav = [pcb.x + 2 * clr, pcb.y + 2 * clr];
outer = [cav.x + 2 * wall, cav.y + 2 * wall];
z_face_in = glass_top + 0.2;          // bezel underside, clear of the glass (board is held by the PCB)
z_front = z_face_in + face;
z_back_in = -back_depth - 0.4;        // back plate inner face
z_back = z_back_in - back_t;
plug_x = [min([for (p = ports) p[0] - p[1] / 2]) - port_margin,
          max([for (p = ports) p[0] + p[1] / 2]) + port_margin];
usb_cx = (plug_x[0] + plug_x[1]) / 2;
plug_w = plug_x[1] - plug_x[0];
plate = [cav.x - 2 * plate_clr, cav.y - 2 * plate_clr];
rim_w = 1.2;
rim_h = 1.2;
rim_len = 60;                          // groove length along the long sides (front shell)
// Rim segments [y0, y1] per side; the right side skips the JST connector at y -19..-11.
rim_segments = [[[-28, 28]], [[-28, -21], [-9, 28]]];  // [left, right]
win_front = view + [1.6, 1.6];         // window at the front face (bevel)
win_c = glass_c;
chin_y = ((win_c.y - win_front.y / 2) + (-outer.y / 2)) / 2;
keyhole_y = [2, -30];             // clear of the ESP32 module (y > 17.6)
boss_h = 2.0;                      // keyhole boss above the plate (top at z -3.1)

// ---- Helpers -----------------------------------------------------------------------
module rr(size, r) {
  r2 = min(r, size.x / 2 - 0.01, size.y / 2 - 0.01);
  offset(r = r2) square([size.x - 2 * r2, size.y - 2 * r2], center = true);
}

module slab(size, r, z0, z1) { translate([0, 0, z0]) linear_extrude(z1 - z0) rr(size, r); }

// ---- Front shell -----------------------------------------------------------------
module shell_outer() {
  hull() {
    slab(outer - 2 * [back_chamfer, back_chamfer], outer_r - back_chamfer, z_back, z_back + 0.01);
    slab(outer, outer_r, z_back + back_chamfer, z_front - edge_chamfer);
    slab(outer - 2 * [edge_chamfer, edge_chamfer], outer_r - edge_chamfer, z_front - 0.01, z_front);
  }
}

module window_cut() {
  translate([win_c.x, win_c.y, 0]) {
    slab(view, 1.0, z_face_in - 1, z_front + 1);
    hull() {  // 45 degree bevel into the front face
      slab(view, 1.0, z_front - 0.8, z_front - 0.79);
      slab(win_front, 1.8, z_front, z_front + 0.01);
    }
  }
}

module accent_ring_2d() {
  translate(win_c) difference() {
    offset(r = 1.8) rr(win_front, 1.8);
    offset(r = 1.0) rr(win_front, 1.8);
  }
}

module moon_2d() {
  translate([0, chin_y]) difference() {
    circle(r = 3.2);
    translate([1.5, 1.1]) circle(r = 2.8);
  }
}

module accent_2d() {
  if (accent) accent_ring_2d();
  if (moon) moon_2d();
}

module accent_inlay() {  // second colour, flush with the front face
  translate([0, 0, z_front - 0.6]) linear_extrude(0.6) accent_2d();
}

module usb_cut() {
  // Open to the back: the back plate's tab closes it from below.
  w = plug_w; r = 1.5;
  translate([usb_cx, -cav.y / 2 + 0.5, 0]) rotate([90, 0, 0])
    linear_extrude(wall + 2) hull() {
      for (sx = [-1, 1]) translate([sx * (w / 2 - r), plug_top - r]) circle(r = r);
      translate([-w / 2, z_back - 1]) square([w, 1]);
    }
}

module snap_grooves() {
  for (sx = [-1, 1])
    translate([sx * (cav.x / 2 + 0.25), 0, z_back_in + rim_h / 2])
      cube([0.5 + 0.02, rim_len, 1.0], center = true);
}

module standoffs() {
  for (h = holes) translate([h.x, h.y, 0]) {
    translate([0, 0, pcb.z]) cylinder(d = 5.5, h = z_face_in - pcb.z + 0.01);
    // Tapered locating pin: self-centres in the hole and is strong at the base.
    translate([0, 0, pcb.z - 1.2]) cylinder(d1 = 2.2, d2 = hole_d - 0.2, h = 1.21);
  }
}

module front_body() {
  difference() {
    shell_outer();
    slab(cav, pcb_r, z_back - 1, z_face_in);
    window_cut();
    usb_cut();
    snap_grooves();
    if (sensor_hole) translate([sensor.x, sensor.y, z_face_in - 1]) cylinder(d = 2.2, h = face + 2);
    translate([0, 0, z_front - 0.6]) linear_extrude(1) accent_2d();
  }
  standoffs();
}

// ---- Back plate --------------------------------------------------------------------
module keyhole_2d(slot) {  // screw head enters at the bottom circle, hangs at the top
  if (slot) { circle(d = 7.5); translate([-1.8, 0]) square([3.6, 7]); }
  else hull() { circle(d = 7.5); translate([0, 7]) circle(d = 7.5); }
}

module back_plate() {
  difference() {
    union() {
      hull() {
        slab(plate - 2 * [back_chamfer, back_chamfer], pcb_r, z_back, z_back + 0.01);
        slab(plate, pcb_r, z_back + back_chamfer, z_back_in);
      }
      // Tab filling the bottom of the USB opening; also keys the orientation.
      translate([plug_x[0] + plate_clr, -outer.y / 2 + back_chamfer, z_back])
        cube([plug_w - 2 * plate_clr, outer.y / 2 - plate.y / 2 + 0.5, back_t]);
      // Stiff rim with one-way snap beads along the long sides: a gentle ramp to push in,
      // a near-vertical catch against the groove's edge to keep it in.
      for (side = [0, 1]) let (sx = side == 0 ? -1 : 1) for (seg = rim_segments[side]) {
        len = seg[1] - seg[0];
        translate([sx * (plate.x / 2 - rim_w / 2), (seg[0] + seg[1]) / 2, z_back_in + rim_h / 2])
          cube([rim_w, len, rim_h], center = true);
        translate([sx * plate.x / 2, (seg[0] + seg[1]) / 2, z_back_in + rim_h / 2])
          rotate([90, 0, 0]) linear_extrude(len, center = true)
            polygon([[0, -0.45], [sx * bead, -0.35], [sx * bead, -0.25], [0, 0.45]]);
      }
      // Crush ribs on the short edges: they squeeze against the wall for friction.
      for (rib = [[-12, 1], [0, 1], [12, 1], [-21, -1], [12, -1], [20, -1]])
        if (rib[1] > 0 || rib[0] < plug_x[0] - 2 || rib[0] > plug_x[1] + 2)
          translate([rib[0], rib[1] * (plate.y / 2 + plate_clr + crush - 0.6), z_back + back_chamfer])
            cylinder(r = 0.6, h = back_t - back_chamfer, $fn = 24);  // reaches `crush` past the wall
      // Posts press the PCB back at the mounting holes; flared base so they can't snap off.
      for (h = holes) translate([h.x, h.y, z_back_in - 0.01]) {
        cylinder(d = 5.5, h = -z_back_in + preload);
        cylinder(d1 = 7.5, d2 = 5.5, h = 1.2);
      }
      if (keyholes) for (y = keyhole_y)
        translate([0, y, z_back_in - 0.01]) linear_extrude(boss_h) offset(r = 1.6) keyhole_2d(false);
    }
    if (keyholes) for (y = keyhole_y) translate([0, y, 0]) {
      translate([0, 0, z_back - 1]) linear_extrude(back_t + 1.01) keyhole_2d(true);
      // Head pocket above a 0.8 mm retaining lip.
      translate([0, 0, z_back + 0.8]) linear_extrude(z_back_in + boss_h - 0.8 - (z_back + 0.8)) keyhole_2d(false);
    }
  }
}

// ---- Fit test: just the face and standoffs, to check window, pins and outline -------
module fit_test() {
  intersection() {
    front_body();
    slab(outer + [2, 2], outer_r, 0.2, z_front + 1);
  }
}

// ---- Board stand-in for the assembly view --------------------------------------------
module board() {
  color("#2E7D32") difference() {
    slab(pcb, 1, 0, pcb.z);
    for (h = holes) translate([h.x, h.y, -1]) cylinder(d = hole_d, h = 4);
  }
  color("#222") translate([0, 0, pcb.z]) slab([50, 69], 0.5, 0, module_top - pcb.z);
  color("#0B1426") translate([glass_c.x, glass_c.y, module_top]) slab(glass, 0.5, 0, glass_top - module_top);
  for (p = ports) color("#9AA") translate([p[0] - p[2] / 2, -43.6, -p[3]]) cube([p[2], 6, p[3]]);
  color("#3A3A3A") translate([-9, 17.6, -3]) cube([18, 25.4, 3]);
}

// ---- Output ------------------------------------------------------------------------
// Print orientation for the exported parts: front face on the bed.
module print_front(what) {
  translate([0, 0, z_front]) rotate([0, 180, 0])
    if (what == "body") front_body(); else accent_inlay();
}

if (part == "assembly") {
  color("#1F2430") front_body();
  color("#E8C27A") accent_inlay();
  color("#D9DEE8") translate([0, 0, -explode]) back_plate();
  board();
} else if (part == "section") {
  // Cut through the USB connector to check the plug opening, tab and clamp.
  intersection() {
    union() {
      color("#1F2430") front_body();
      color("#E8C27A") accent_inlay();
      color("#D9DEE8") back_plate();
      board();
    }
    translate([usb_cx, 0, 0]) cube([200, 200, 100], center = true);
  }
} else if (part == "exploded") {
  // Board | front shell (inside up) | back plate (inside up)
  translate([-62, 0, 0]) board();
  rotate([0, 180, 0]) { color("#1F2430") front_body(); color("#E8C27A") accent_inlay(); }
  translate([62, 0, 0]) rotate([0, 180, 0]) color("#D9DEE8") back_plate();
} else if (part == "front") {
  print_front("body");
  print_front("accent");
} else if (part == "front_body") {
  print_front("body");
} else if (part == "front_accent") {
  print_front("accent");
} else if (part == "back") {
  translate([0, 0, -z_back]) back_plate();
} else if (part == "fit_test") {
  translate([0, 0, z_front]) rotate([0, 180, 0]) fit_test();
} else if (part == "board") {
  board();
}
