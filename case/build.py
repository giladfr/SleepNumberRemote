#!/usr/bin/env python3
"""Export the case from OpenSCAD and package it for Bambu Studio.

Writes to case/out/:
  front_body.stl, front_accent.stl, back.stl, fit_test.stl   (print orientation)
  cyd_case.3mf          three filaments, both parts on one plate:
                        1 = front shell, 2 = back plate, 3 = accent ring + moon
  cyd_case_x2d.3mf      the same as a ready Bambu Studio project for the Bambu Lab X2D
                        (0.4 mm, 0.20 mm Standard, 3x PLA Matte), test-sliced; only built
                        when Bambu Studio is installed
  preview_*.png         renders for a quick look

Usage:  python3 case/build.py            (needs `openscad` on PATH; stdlib only)
"""

import json
import pathlib
import shutil
import struct
import subprocess
import tempfile
import uuid
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
SCAD = HERE / "cyd_case.scad"
OUT = HERE / "out"

PARTS = ["front_body", "front_accent", "back", "fit_test"]

BAMBU = pathlib.Path("/Applications/BambuStudio.app/Contents/MacOS/BambuStudio")
PROFILES = BAMBU.parents[1] / "Resources/profiles/BBL"
# Filament slots: 1 = front shell, 2 = back plate, 3 = accent ring + moon.
COLOURS = ["#1F2430", "#D9DEE8", "#E8C27A"]


def openscad(args):
    subprocess.run(["openscad", *args, str(SCAD)], check=True, capture_output=True)


def export_parts():
    OUT.mkdir(exist_ok=True)
    for p in PARTS:
        openscad(["--export-format", "binstl", "-o", str(OUT / f"{p}.stl"), "-D", f'part="{p}"'])


def render_previews():
    views = {
        "front": ("0,0,0,35,0,20,230", "assembly"),
        "back": ("0,0,0,140,0,160,230", "assembly"),
        "parts": ("0,0,0,50,0,0,260", "exploded"),
    }
    for name, (camera, part) in views.items():
        openscad(["-o", str(OUT / f"preview_{name}.png"), "--imgsize=1200,1200", f"--camera={camera}",
                  "--colorscheme=Tomorrow", "--render", "-D", f'part="{part}"'])


def read_stl(path):
    """Binary STL -> (vertices, triangles) with shared vertices merged."""
    data = path.read_bytes()
    (count,) = struct.unpack_from("<I", data, 80)
    index, verts, tris = {}, [], []
    for i in range(count):
        vals = struct.unpack_from("<12f", data, 84 + i * 50)
        tri = []
        for k in range(3):
            v = tuple(round(c, 5) for c in vals[3 + 3 * k: 6 + 3 * k])
            if v not in index:
                index[v] = len(verts)
                verts.append(v)
            tri.append(index[v])
        if len(set(tri)) == 3:
            tris.append(tri)
    return verts, tris


def bounds(verts):
    xs, ys, zs = zip(*verts)
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def mesh_xml(obj_id, name, verts, tris):
    v = "\n".join(f'     <vertex x="{x:.5f}" y="{y:.5f}" z="{z:.5f}"/>' for x, y, z in verts)
    t = "\n".join(f'     <triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in tris)
    return (f'  <object id="{obj_id}" name="{name}" type="model">\n   <mesh>\n    <vertices>\n{v}\n'
            f'    </vertices>\n    <triangles>\n{t}\n    </triangles>\n   </mesh>\n  </object>')


def build_3mf():
    """Two objects, each made of parts; Bambu Studio reads per-part filaments from
    Metadata/model_settings.config."""
    meshes = {p: read_stl(OUT / f"{p}.stl") for p in ("front_body", "front_accent", "back")}
    # Centre each object's footprint at its spot on the bed.
    fb_lo, fb_hi = bounds(meshes["front_body"][0])
    bk_lo, bk_hi = bounds(meshes["back"][0])
    gap = 12
    width = (fb_hi[0] - fb_lo[0]) + gap + (bk_hi[0] - bk_lo[0])
    bed_c = (128.0, 128.0)
    front_at = (bed_c[0] - width / 2 - fb_lo[0], bed_c[1] - (fb_lo[1] + fb_hi[1]) / 2)
    back_at = (bed_c[0] + width / 2 - bk_hi[0], bed_c[1] - (bk_lo[1] + bk_hi[1]) / 2)

    objects = [
        # id, name, [(mesh id, part name, mesh key, filament)], position
        (10, "Front", [(1, "Front shell", "front_body", 1), (2, "Accent inlay", "front_accent", 3)], front_at),
        (11, "Back", [(3, "Back plate", "back", 2)], back_at),
    ]

    res, build, cfg = [], [], []
    for obj_id, obj_name, parts, (tx, ty) in objects:
        comps = []
        cfg.append(f'  <object id="{obj_id}">\n    <metadata key="name" value="{obj_name}"/>\n'
                   f'    <metadata key="extruder" value="{parts[0][3]}"/>')
        for mesh_id, part_name, key, filament in parts:
            verts, tris = meshes[key]
            res.append(mesh_xml(mesh_id, part_name, verts, tris))
            comps.append(f'    <component objectid="{mesh_id}"/>')
            cfg.append(f'    <part id="{mesh_id}" subtype="normal_part">\n'
                       f'      <metadata key="name" value="{part_name}"/>\n'
                       f'      <metadata key="extruder" value="{filament}"/>\n    </part>')
        cfg.append("  </object>")
        res.append(f'  <object id="{obj_id}" name="{obj_name}" type="model">\n   <components>\n'
                   + "\n".join(comps) + "\n   </components>\n  </object>")
        build.append(f'  <item objectid="{obj_id}" transform="1 0 0 0 1 0 0 0 1 {tx:.4f} {ty:.4f} 0"/>')

    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="millimeter" xml:lang="en-US" '
             'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">\n'
             ' <metadata name="Title">CYD Sleep Number remote case</metadata>\n'
             ' <metadata name="Designer">SleepNumberRemote</metadata>\n'
             ' <resources>\n' + "\n".join(res) + '\n </resources>\n'
             ' <build>\n' + "\n".join(build) + '\n </build>\n</model>\n')
    config = '<?xml version="1.0" encoding="UTF-8"?>\n<config>\n' + "\n".join(cfg) + "\n</config>\n"
    content_types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                     '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
                     ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
                     ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n'
                     ' <Default Extension="config" ContentType="text/xml"/>\n'
                     '</Types>\n')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
            f' <Relationship Target="/3D/3dmodel.model" Id="rel-{uuid.uuid4().hex[:8]}" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n'
            '</Relationships>\n')

    path = OUT / "cyd_case.3mf"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", model)
        z.writestr("Metadata/model_settings.config", config)
    return path


def bambu_project(src, printer="X2D", nozzle="0.4"):
    """Turn the plain 3MF into a native Bambu Studio project for one printer and slice it
    once as a check. The CLI keeps the per-part filaments but leaves most per-filament
    settings at one entry and the purge table at its default size; both are expanded.
    The export is sliced so Bambu Studio fills in the nozzle setup; that (single-filament)
    G-code is dropped again."""
    machine = PROFILES / "machine" / f"Bambu Lab {printer} {nozzle} nozzle.json"
    process = PROFILES / "process" / f"0.20mm Standard @BBL {printer}.json"
    filament = PROFILES / "filament" / f"Bambu PLA Matte @BBL {printer}.json"
    n = len(COLOURS)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        subprocess.run([str(BAMBU), "--load-settings", f"{machine};{process}",
                        "--load-filaments", ";".join([str(filament)] * n), "--slice", "1",
                        "--export-3mf", "project.3mf", "--outputdir", str(tmp), str(src)],
                       check=True, capture_output=True, cwd=tmp)
        with zipfile.ZipFile(tmp / "project.3mf") as z:
            files = {name: z.read(name) for name in z.namelist() if ".gcode" not in name}
        files["Metadata/model_settings.config"] = files["Metadata/model_settings.config"].replace(
            b'value="Metadata/plate_1.gcode"', b'value=""')
        cfg = json.loads(files["Metadata/project_settings.config"])
        for key, value in cfg.items():
            if key.startswith(("filament_", "nozzle_temperature", "hot_plate", "textured_plate", "cool_plate",
                               "eng_plate", "supertack_plate", "fan_", "slow_down", "overhang_fan",
                               "close_fan", "full_fan", "reduce_fan", "additional_cooling",
                               "temperature_vitrification", "required_nozzle_HRC", "activate_air_filtration",
                               "during_print_exhaust", "complete_print_exhaust", "default_filament_colour",
                               "chamber_temperatures", "volumetric_speed_coefficients")) \
                    and isinstance(value, list) and len(value) == 1:
                cfg[key] = value * n
        cfg["filament_colour"] = COLOURS
        cfg["default_filament_colour"] = [""] * n
        # One n x n purge table per nozzle (the X2D has two); the CLI leaves a single one.
        nozzles = len(cfg.get("nozzle_diameter", ["0.4"]))
        cfg["flush_multiplier"] = ["1"] * nozzles
        cfg["flush_volumes_matrix"] = ["0" if i == j else "280" for i in range(n) for j in range(n)] * nozzles
        cfg["flush_volumes_vector"] = ["140", "140"] * n
        files["Metadata/project_settings.config"] = json.dumps(cfg, indent=4).encode()
        out = OUT / f"cyd_case_{printer.lower()}.3mf"
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            for name, data in files.items():
                z.writestr(name, data)

        # Check: it must slice, and use all three filaments.
        check = tmp / "check"
        check.mkdir()
        subprocess.run([str(BAMBU), "--slice", "1", "--outputdir", str(check), str(out)],
                       capture_output=True, cwd=tmp)
        result = json.loads((check / "result.json").read_text())
        gcode = next(check.glob("*.gcode")).read_text(errors="ignore")
        used = next((l for l in gcode.splitlines() if l.startswith("; filament: ")), "")
        minutes = result["sliced_plates"][0]["total_predication"] / 60
        if result["error_string"] != "Success." or used.strip() != "; filament: 1,2,3":
            raise SystemExit(f"{out.name}: slice check failed ({result['error_string']}, {used})")
    return out, minutes


if __name__ == "__main__":
    export_parts()
    render_previews()
    plain = build_3mf()
    print("wrote", plain)
    if BAMBU.exists():
        path, minutes = bambu_project(plain)
        print(f"wrote {path} (slices on all 3 filaments, ~{minutes:.0f} min)")
