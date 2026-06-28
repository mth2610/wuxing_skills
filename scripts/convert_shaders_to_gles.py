import os
import re
import sys

def convert_to_gles(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    is_vs = filepath.endswith('.vs')
    is_fs = filepath.endswith('.fs')

    # Replace #version 330 or 430 with 100
    content = re.sub(r'#version\s+\d+.*', '#version 100', content)

    if is_vs:
        # Remove layout(location = X)
        content = re.sub(r'layout\s*\(\s*location\s*=\s*\d+\s*\)\s*', '', content)
        # Change 'in ' to 'attribute '
        content = re.sub(r'^\s*in\s+', 'attribute ', content, flags=re.MULTILINE)
        # Change 'out ' to 'varying '
        content = re.sub(r'^\s*out\s+', 'varying ', content, flags=re.MULTILINE)
        
    if is_fs:
        # Change 'in ' to 'varying '
        content = re.sub(r'^\s*in\s+', 'varying ', content, flags=re.MULTILINE)
        
        # Thêm precision highp float cho Android để tránh lỗi bể khối (water deformed)
        if 'precision mediump float;' in content:
            content = content.replace('precision mediump float;', 'precision highp float;')
        elif 'precision highp float;' not in content:
            content = content.replace('#version 100', '#version 100\nprecision highp float;')
            
        # Handle 'out vec4 fragColor' or similar
        out_match = re.search(r'^\s*out\s+vec4\s+([a-zA-Z0-9_]+)\s*;', content, re.MULTILINE)
        if out_match:
            out_var = out_match.group(1)
            content = content[:out_match.start()] + content[out_match.end():]
            content = re.sub(r'\b' + out_var + r'\b', 'gl_FragColor', content)
            
    # XÓA khởi tạo giá trị mặc định của uniform (VD: uniform float a = 1.0; -> uniform float a;)
    # GLSL 100 không hỗ trợ khởi tạo uniform lúc khai báo.
    content = re.sub(r'(^\s*uniform\s+[\w\s]+\s+[a-zA-Z0-9_]+)\s*=[^;]+;', r'\1;', content, flags=re.MULTILINE)

    # Replace 'texture(' with 'texture2D('
    content = re.sub(r'\btexture\(', 'texture2D(', content)

    with open(filepath, 'w') as f:
        f.write(content)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python convert_shaders_to_gles.py <assets_dir>")
        sys.exit(1)
        
    assets_dir = sys.argv[1]
    for root, dirs, files in os.walk(assets_dir):
        for file in files:
            if file.endswith('.fs') or file.endswith('.vs'):
                filepath = os.path.join(root, file)
                convert_to_gles(filepath)
                print(f"Converted to GLES: {filepath}")
