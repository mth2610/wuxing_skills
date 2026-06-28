#!/usr/bin/env python3
"""
convert_shaders_to_gles.py

Chuyen shader GLSL (#version 330/430, dung cho desktop OpenGL) sang
GLSL ES 1.00 (dung cho OpenGL ES 2.0 / Android, tuong thich Raylib rlgl).

Cach dung:
    python convert_shaders_to_gles.py <assets_dir> [--dry-run] [--no-backup]

  --dry-run    : chi in ra nhung gi se thay doi, KHONG ghi file.
  --no-backup  : khong tao file .bak truoc khi ghi de (mac dinh CO backup).

QUAN TRONG:
  Script nay co the dung sai trong vai truong hop GLSL phuc tap (xem
  phan WARNING_PATTERNS ben duoi). Voi nhung pattern do, script CHI
  CANH BAO, khong tu sua, vi tu sua sai se tao ra loi am tham (silent
  bug) kho debug hon la khong sua. Luon doc ky output canh bao va
  kiem tra file .bak khi co nghi ngo.
"""

import os
import re
import sys
import shutil

# ---------------------------------------------------------------------------
# Cac pattern GLSL 330/430 khong co tuong duong an toan, tu dong trong ES 100.
# Script chi CANH BAO, khong tu sua, de tranh sua sai am tham.
# ---------------------------------------------------------------------------
# Pattern luon canh bao bat ke vs/fs
WARNING_PATTERNS_COMMON = [
    (r'\btextureLod\s*\(', "textureLod() khong ton tai o GLSL ES 100 core. "
     "Can extension GL_EXT_shader_texture_lod va doi thanh texture2DLodEXT()/textureCubeLodEXT()."),
    (r'\btextureProj\s*\(', "textureProj() khong duoc ho tro san o GLSL ES 100 core, can kiem tra extension."),
    (r'\bgl_VertexID\b', "gl_VertexID khong co san o GLSL ES 100. Can truyen qua 1 vertex attribute rieng."),
    (r'\bgl_InstanceID\b', "gl_InstanceID khong co san o GLSL ES 100. Can mo rong qua GL_EXT_instanced_arrays / attribute rieng."),
    (r'\bsampler2DArray\b|\bsamplerCubeArray\b', "sampler array khong ton tai o GLSL ES 100, can doi kien truc texture."),
    (r'(?<![<>=!&|^])\^(?!\^|=)|(?<![<>=!&|^])\&(?!\&|=)|(?<![<>=!&|^])\|(?!\||=)|<<|>>',
     "Co the dung bitwise operator (&, |, ^, <<, >>) tren int. ES 100 khong dam bao ho tro day du tren moi GPU mobile (Adreno/Mali cu)."),
    (r'\bswitch\s*\(', "switch statement co the khong duoc ho tro tren mot so driver GLSL ES 100 (yeu cau #version 100 voi extension)."),
    (r'\bflat\s+in\b|\bflat\s+out\b|\bnoperspective\b', "Qualifier 'flat'/'noperspective' khong ton tai o GLSL ES 100, "
     "can xoa qualifier nay (interpolation se luon la smooth/varying mac dinh)."),
]

# Pattern chi canh bao trong fragment shader (vi 'out' o vertex shader la binh thuong, khong phai MRT)
WARNING_PATTERNS_FS_ONLY = [
    (r'^\s*out\s+(?!vec4)\w+\s+\w+\s*;', "Co bien 'out' khac ngoai 'out vec4 <fragColor>' trong fragment shader "
     "(co the la MRT - multiple render targets). ES 100 KHONG ho tro nhieu output, "
     "can tach logic hoac gop lai thanh 1 vec4 duy nhat."),
    (r'\bgl_FragData\s*\[', "gl_FragData[] (MRT) khong ho tro o GLSL ES 100. Can gop ve 1 gl_FragColor."),
]


def find_warnings(content, is_fs):
    """Tra ve list (pattern_desc, so_lan_xuat_hien) cho cac pattern nguy hiem."""
    warnings = []
    patterns = WARNING_PATTERNS_COMMON + (WARNING_PATTERNS_FS_ONLY if is_fs else [])
    for pattern, desc in patterns:
        matches = re.findall(pattern, content, flags=re.MULTILINE)
        if matches:
            warnings.append((desc, len(matches)))
    return warnings


def strip_qualifiers_before_in_out(content):
    """
    Xu ly 'in'/'out' co qualifier dung truoc (vd: 'flat in int idx;').
    Doi voi cac qualifier nay, ta van bao o WARNING_PATTERNS (vi flat
    khong ton tai o ES 100), nhung de quy tac 'in'->'attribute'/'varying'
    o duoi van bat dung truong hop khong co qualifier, ta khong dong
    cham gi them o day - chi de ham nay lam tai lieu cho ro logic.
    """
    return content


def convert_in_out(content, is_vs):
    """
    Doi 'in'/'out' thanh 'attribute'/'varying', ho tro:
      - Co the co whitespace/tab truoc 'in'/'out'
      - Cho phep 1 qualifier 'highp'/'mediump'/'lowp' giua 'in'/'out' va kieu du lieu
      - Khong dong vao dong co 'flat'/'noperspective' (qualifier khong tuong duong o ES100)
        de tranh sinh code sai; truong hop nay da duoc canh bao rieng.
    """
    if is_vs:
        # layout(location = X) -> xoa, GLSL ES 100 khong dung layout qualifier nay
        content = re.sub(r'layout\s*\(\s*location\s*=\s*\d+\s*\)\s*', '', content)
        content = re.sub(r'^(\s*)in(\s+)', r'\1attribute\2', content, flags=re.MULTILINE)
        content = re.sub(r'^(\s*)out(\s+)', r'\1varying\2', content, flags=re.MULTILINE)
    else:
        content = re.sub(r'^(\s*)in(\s+)', r'\1varying\2', content, flags=re.MULTILINE)
    return content


def convert_fs_output(content):
    """
    Doi 'out vec4 <name>;' (duy nhat, dung cho fragColor) thanh xoa khai bao
    va doi moi cho su dung <name> thanh gl_FragColor.
    Neu co nhieu hon 1 'out vec4' (MRT), KHONG tu sua - da canh bao o
    WARNING_PATTERNS, vi gop nhieu output thanh 1 gl_FragColor se sai logic.
    """
    out_vec4_matches = list(re.finditer(r'^\s*out\s+vec4\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*;', content, re.MULTILINE))

    if len(out_vec4_matches) == 0:
        return content, None
    if len(out_vec4_matches) > 1:
        # Nhieu output -> khong an toan de tu dong sua, de nguyen va canh bao
        return content, "multiple_out_vec4"

    match = out_vec4_matches[0]
    out_var = match.group(1)
    content = content[:match.start()] + content[match.end():]
    content = re.sub(r'\b' + re.escape(out_var) + r'\b', 'gl_FragColor', content)
    return content, None


def strip_uniform_initializers(content):
    """
    Xoa gia tri khoi tao mac dinh cua uniform (GLSL ES 100 khong ho tro).
    Ho tro ca truong hop khoi tao xuong nhieu dong (vd mang vec3[3](...)),
    bang cach cho phep [^;]+ chay qua nhieu dong (DOTALL) nhung van neo
    vao dong bat dau bang 'uniform'.
    """
    pattern = re.compile(
        r'(^[ \t]*uniform\s+[\w]+(?:\s*\[\s*\d*\s*\])?\s+[a-zA-Z_][a-zA-Z0-9_]*(?:\s*\[\s*\d*\s*\])?)\s*=\s*[^;]+;',
        flags=re.MULTILINE | re.DOTALL
    )
    return pattern.sub(r'\1;', content)


def convert_texture_calls(content):
    """
    Doi 'texture(' -> 'texture2D(' va 'textureCube(' duoc giu nguyen ten
    rieng cho sampler cube (GLSL ES 100 dung textureCube(), khong doi).
    Luu y: textureLod / textureProj duoc canh bao rieng, KHONG tu doi o day.
    """
    # Chi doi loi goi 'texture(' (sampler2D), khong dong vao textureCube/textureLod/textureProj/textureSize...
    # \b dam bao 'texture' khong phai la phan cuoi cua mot ten ham dai hon (textureCube, textureLod,...)
    content = re.sub(r'(?<![a-zA-Z0-9_])texture\(', 'texture2D(', content)
    return content


def convert_to_gles(filepath, dry_run=False, make_backup=True):
    with open(filepath, 'r', encoding='utf-8') as f:
        original = f.read()

    content = original
    is_vs = filepath.endswith('.vs')
    is_fs = filepath.endswith('.fs')

    # 1) #version 330/430 -> #version 100
    content = re.sub(r'#version\s+\d+.*', '#version 100', content)

    # 2) in/out -> attribute/varying (truoc khi xu ly precision/fragColor)
    content = convert_in_out(content, is_vs)

    mrt_warning = None
    if is_fs:
        # 3) precision: uu tien highp de tranh artifact tren mobile (vd: water/tornado bi "be khoi")
        if 'precision mediump float;' in content:
            content = content.replace('precision mediump float;', 'precision highp float;')
        elif 'precision highp float;' not in content:
            content = content.replace('#version 100', '#version 100\nprecision highp float;')

        # 4) out vec4 fragColor -> gl_FragColor (chi khi DUY NHAT 1 bien out vec4)
        content, mrt_warning = convert_fs_output(content)

    # 5) Xoa initializer cua uniform (ho tro multiline)
    content = strip_uniform_initializers(content)

    # 6) texture(...) -> texture2D(...)
    content = convert_texture_calls(content)

    # 7) Tim cac pattern nguy hiem de canh bao (khong tu sua)
    warnings = find_warnings(original, is_fs)  # quet tren ban GOC de bao chinh xac vi tri/nguyen nhan
    if mrt_warning:
        warnings.append(("Phat hien nhieu 'out vec4' trong fragment shader (MRT) - "
                          "KHONG tu dong gop thanh gl_FragColor vi co the sai logic. Can sua tay.", 1))

    changed = content != original

    if not dry_run and changed:
        if make_backup:
            backup_path = filepath + '.bak'
            shutil.copy2(filepath, backup_path)
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)

    return changed, warnings


def main():
    args = sys.argv[1:]
    if not args:
        print("Usage: python convert_shaders_to_gles.py <assets_dir> [--dry-run] [--no-backup]")
        sys.exit(1)

    dry_run = '--dry-run' in args
    make_backup = '--no-backup' not in args
    assets_dir = next((a for a in args if not a.startswith('--')), None)

    if not assets_dir or not os.path.isdir(assets_dir):
        print(f"Loi: '{assets_dir}' khong phai mot thu muc hop le.")
        sys.exit(1)

    total_files = 0
    total_changed = 0
    total_warnings = 0

    for root, dirs, files in os.walk(assets_dir):
        for file in files:
            if file.endswith('.fs') or file.endswith('.vs'):
                filepath = os.path.join(root, file)
                total_files += 1
                changed, warnings = convert_to_gles(filepath, dry_run=dry_run, make_backup=make_backup)

                if changed:
                    total_changed += 1
                    tag = "[DRY-RUN] Se chuyen:" if dry_run else "Converted to GLES:"
                    print(f"{tag} {filepath}")
                else:
                    print(f"[Khong doi] {filepath} (da la ES 100 hoac khong khop pattern nao)")

                for desc, count in warnings:
                    total_warnings += 1
                    print(f"   CANH BAO ({count}x): {desc}")

    print()
    print(f"Tong: {total_files} file, {total_changed} file duoc chuyen, {total_warnings} canh bao can xem tay.")
    if dry_run:
        print("(Dry-run: khong co file nao bi ghi de.)")
    elif make_backup and total_changed > 0:
        print("Da tao file .bak cho moi file duoc sua. Neu can rollback: doi ten .bak ve lai ten goc.")


if __name__ == '__main__':
    main()