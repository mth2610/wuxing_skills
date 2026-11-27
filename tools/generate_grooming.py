#!/usr/bin/env python3
"""
generate_grooming.py — Modular, Gender-Agnostic Wuxia Grooming Collection Generator

Generates separate, mix-and-match engine-ready GLB 2.0 assets for the 3 grooming slots:
  - hair     -> assets/characters/equipment/hair/<id>.glb      (28 bones w/ cloth_hair_* springs)
  - eyebrows -> assets/characters/equipment/eyebrows/<id>.glb  (20 bones, 6 styles)
  - beard    -> assets/characters/equipment/beard/<id>.glb     (20 bones, 6 styles)

EXAMPLES:
  python3 tools/generate_grooming.py --collection --install
  python3 tools/generate_grooming.py --part all --profile male --install
  python3 tools/generate_grooming.py --part eyebrows --style lieu_diep_mi --profile female --body female.glb
"""

import argparse
from pathlib import Path
from grooming.core import (
    FACE_PROFILES,
    HeadSurfaceSampler,
    MeshBuilder,
    build_skeleton,
    export_glb,
)
from grooming import hair as hair_mod
from grooming import eyebrows as brow_mod
from grooming import beard as beard_mod


DEFAULT_OUTPUTS = {
    # Hair
    ("hair", "kiem_hiep_ponytail"):   ("toc_co_trang_kiem_hiep.glb", "assets/characters/equipment/hair/toc_co_trang_kiem_hiep.glb"),
    ("hair", "nu_hiep_long"):         ("toc_nu_hiep_truong_phat.glb", "assets/characters/equipment/hair/toc_nu_hiep_truong_phat.glb"),
    # Eyebrows + Eyelashes collection (6 styles)
    ("eyebrows", "kiem_mi"):          ("long_may_kiem_mi.glb", "assets/characters/equipment/eyebrows/long_may_kiem_mi.glb"),
    ("eyebrows", "tuong_quan_mi"):    ("long_may_tuong_quan.glb", "assets/characters/equipment/eyebrows/long_may_tuong_quan.glb"),
    ("eyebrows", "thu_sinh_mi"):      ("long_may_thu_sinh.glb", "assets/characters/equipment/eyebrows/long_may_thu_sinh.glb"),
    ("eyebrows", "dao_nhan_mi"):      ("long_may_dao_nhan.glb", "assets/characters/equipment/eyebrows/long_may_dao_nhan.glb"),
    ("eyebrows", "lieu_diep_mi"):     ("long_may_lieu_diep.glb", "assets/characters/equipment/eyebrows/long_may_lieu_diep.glb"),
    ("eyebrows", "phuong_vi_mi"):     ("long_may_phuong_vi.glb", "assets/characters/equipment/eyebrows/long_may_phuong_vi.glb"),
    # Beard collection (6 styles)
    ("beard", "kiem_khach_trim"):     ("rau_kiem_khach.glb", "assets/characters/equipment/beard/rau_kiem_khach.glb"),
    ("beard", "lang_tu_light"):       ("rau_lang_tu.glb", "assets/characters/equipment/beard/rau_lang_tu.glb"),
    ("beard", "tam_chom_quan_tu"):    ("rau_tam_chom.glb", "assets/characters/equipment/beard/rau_tam_chom.glb"),
    ("beard", "quai_non_vo_tuong"):   ("rau_quai_non.glb", "assets/characters/equipment/beard/rau_quai_non.glb"),
    ("beard", "tien_ong_truong_tu"):  ("rau_tien_ong.glb", "assets/characters/equipment/beard/rau_tien_ong.glb"),
    ("beard", "son_duong_hiep_si"):   ("rau_son_duong.glb", "assets/characters/equipment/beard/rau_son_duong.glb"),
}


def build_part(part: str, style: str, body_glb: Path, profile_name: str, out_path: Path, install_path: Path = None):
    head = HeadSurfaceSampler(body_glb)
    prof = FACE_PROFILES[profile_name]
    use_springs = (part == "hair")
    bones, bidx = build_skeleton(head.landmarks, include_hair_springs=use_springs)
    mb = MeshBuilder(num_bones=len(bones))

    if part == "hair":
        hair_mod.STYLE_BUILDERS[style](mb, head, prof, bidx)
    elif part == "eyebrows":
        brow_mod.STYLE_BUILDERS[style](mb, head, prof, bidx)
    elif part == "beard":
        beard_mod.STYLE_BUILDERS[style](mb, head, prof, bidx)
    else:
        raise ValueError(f"Unknown part: {part}")

    V, N, UV, COL, J4, W4, F, M = mb.finalize()
    export_glb(out_path, bones, V, N, UV, COL, J4, W4, F, M, mesh_name=f"Wuxia_{part}_{style}")
    if install_path is not None:
        install_path.parent.mkdir(parents=True, exist_ok=True)
        install_path.write_bytes(out_path.read_bytes())
        print(f"  Installed -> {install_path}")


def main():
    here = Path(__file__).resolve().parent
    repo = here.parent
    parser = argparse.ArgumentParser(description="Modular, gender-agnostic Wuxia Grooming Collection Generator.")
    parser.add_argument("--part", choices=["hair", "eyebrows", "beard", "all"], default="all")
    parser.add_argument("--collection", action="store_true", help="Generate the full eyebrows (6) + beard (6) collection")
    parser.add_argument("--profile", choices=list(FACE_PROFILES.keys()), default="male")
    parser.add_argument("--style", type=str, default=None, help="Style name for single-part generation")
    parser.add_argument("--body", type=Path, default=repo / "assets/characters/bodies/male_body.glb")
    parser.add_argument("--out", type=Path, default=None, help="Custom output GLB path (single-part mode)")
    parser.add_argument("--install", action="store_true", help="Install into assets/characters/equipment/<slot>/")
    args = parser.parse_args()

    body = args.body.resolve()
    if args.collection:
        plan = (
            [("eyebrows", s) for s in brow_mod.STYLE_BUILDERS.keys()]
            + [("beard", s) for s in beard_mod.STYLE_BUILDERS.keys()]
        )
    elif args.part == "all":
        if args.profile == "male":
            plan = [
                ("hair", "kiem_hiep_ponytail"),
                ("eyebrows", "kiem_mi"),
                ("beard", "kiem_khach_trim"),
            ]
        else:
            plan = [
                ("hair", "nu_hiep_long"),
                ("eyebrows", "lieu_diep_mi"),
            ]
    else:
        default_styles = {
            "male": {"hair": "kiem_hiep_ponytail", "eyebrows": "kiem_mi", "beard": "kiem_khach_trim"},
            "female": {"hair": "nu_hiep_long", "eyebrows": "lieu_diep_mi", "beard": "kiem_khach_trim"},
        }
        plan = [(args.part, args.style or default_styles[args.profile][args.part])]

    for part, style in plan:
        def_file, def_rel = DEFAULT_OUTPUTS.get(
            (part, style),
            (f"{part}_{style}.glb", f"assets/characters/equipment/{part}/{part}_{style}.glb"),
        )
        out_path = args.out.resolve() if (args.out and len(plan) == 1) else (here / def_file)
        inst_path = (repo / def_rel) if args.install else None
        build_part(part, style, body, args.profile, out_path, inst_path)


if __name__ == "__main__":
    main()
