#!/usr/bin/env python3
import os
import sys

html_path = "/Users/mth2610/Downloads/Model viewer · player.glb + 146 animation.html"
with open(html_path, "r", encoding="utf-8") as f:
    text = f.read()

# Read the libraries from /tmp/fbx_libs/
with open("/tmp/fbx_libs/fflate.min.js", "r", encoding="utf-8") as f:
    fflate_src = f.read()

with open("/tmp/fbx_libs/three.min.js", "r", encoding="utf-8") as f:
    three_src = f.read()

with open("/tmp/fbx_libs/FBXLoader_0128.js", "r", encoding="utf-8") as f:
    fbx_loader_src = f.read()

lib_bundle = f"""
<!-- FBX & Inflatation Libraries for Mixamo FBX Support -->
<script>
{fflate_src}
</script>
<script>
{three_src}
</script>
<script>
{fbx_loader_src}
</script>
"""

# Check target for library injection: right before the last <script>
target_script = "<script>\n'use strict';\nconst A=MixamoAnim"
assert target_script in text, "target_script not found"
text = text.replace(target_script, lib_bundle + "\n" + target_script, 1)

# Update UI button label in #bar
old_btn = '📂 Nạp Anim (.bvh)<input type="file" id="anim_file" accept=".bvh"'
new_btn = '📂 Nạp Anim (.bvh, .fbx)<input type="file" id="anim_file" accept=".bvh,.fbx"'
assert old_btn in text, "old_btn not found"
text = text.replace(old_btn, new_btn, 1)

# Update sample logic in poseModel & add loadFBXBuffer / sampleFBX
old_pose = "function poseModel(id,t,gfix){const f=UPLOADED_ANIMS[id]?sampleBVH(UPLOADED_ANIMS[id],t):A.sample(id,t)"

fbx_sample_code = """function sampleFBX(fbxAnim, t) {
    const dur = fbxAnim.dur;
    const sampleT = dur > 0 ? (t % dur) : 0;
    fbxAnim.mixer.setTime(sampleT);
    const rot = new Float32Array(NM * 4);
    for (let i = 0; i < NM; i++) rot[i * 4 + 3] = 1.0;
    let hips = [0, 0.95, 0];
    for (let i = 0; i < fbxAnim.boneMap.length; i++) {
        const item = fbxAnim.boneMap[i];
        const q = item.bone.quaternion;
        rot.set([q.x, q.y, q.z, q.w], item.libIdx * 4);
        if (item.isHips) {
            const p = item.bone.position;
            let px = p.x, py = p.y, pz = p.z;
            if (py > 10.0) { px *= 0.01; py *= 0.01; pz *= 0.01; }
            hips = [px, py, pz];
        }
    }
    return { id: fbxAnim.name, t, u: dur > 0 ? (sampleT / dur) : 0, rot, hips, root: { x: 0, y: 0, z: 0, yaw: 0 } };
}

function loadFBXBuffer(buf, filename) {
    try {
        const name = filename.replace(/\\.[^/.]+$/, '').replace(/[^a-zA-Z0-9_]/g, '_').toLowerCase();
        const loader = new THREE.FBXLoader();
        const group = loader.parse(buf, '');
        if (!group.animations || !group.animations.length) {
            throw new Error('File FBX không chứa track animation nào');
        }
        const clip = group.animations[0];
        const mixer = new THREE.AnimationMixer(group);
        const action = mixer.clipAction(clip);
        action.play();
        
        const boneMap = [];
        group.traverse(obj => {
            if (obj.isBone || obj.type === 'Bone') {
                const rawName = obj.name || '';
                const shortName = rawName.replace(/^.*:/, '').replace(/^mixamorig5?/i, '');
                const libIdx = B.findIndex(libB => libB.short.toLowerCase() === shortName.toLowerCase());
                if (libIdx >= 0) {
                    boneMap.push({ bone: obj, libIdx, isHips: /Hips$/i.test(rawName) });
                }
            }
        });
        
        const dur = clip.duration || 1.0;
        const parsed = {
            name,
            dur,
            isFBX: true,
            mixer,
            boneMap,
            rawBuffer: buf,
            meta: {
                id: name,
                vi: '📂 ' + name + ' (Mixamo FBX)',
                category: 'uploaded',
                dur,
                loop: true,
                weapon: 'none',
                aliases: [name],
                tags: ['uploaded', 'fbx', 'mixamo'],
                events: []
            }
        };
        UPLOADED_ANIMS[name] = parsed;
        buildList($('q').value);
        pick(name);
        $('msg').style.display = 'block';
        $('msg').textContent = '✅ Đã nạp Mixamo FBX: ' + name + ' (' + dur.toFixed(2) + 's)! Đang phát thử!';
        setTimeout(() => $('msg').style.display = 'none', 4500);
    } catch(err) {
        alert('Lỗi nạp file FBX: ' + err.message);
    }
}

function sampleUploaded(anim, t) {
    return anim.isFBX ? sampleFBX(anim, t) : sampleBVH(anim, t);
}

function poseModel(id,t,gfix){const f=UPLOADED_ANIMS[id]?sampleUploaded(UPLOADED_ANIMS[id],t):A.sample(id,t)"""

assert old_pose in text, "old_pose not found"
text = text.replace(old_pose, fbx_sample_code, 1)

# Update file drag & drop and input handlers to support .fbx
old_anim_input = """$('anim_file').onchange = async e => {
    const f = e.target.files[0];
    if (f) {
        const text = await f.text();
        loadBVHText(text, f.name);
    }
};"""

new_anim_input = """$('anim_file').onchange = async e => {
    const f = e.target.files[0];
    if (!f) return;
    const lower = f.name.toLowerCase();
    if (lower.endsWith('.fbx')) {
        const ab = await f.arrayBuffer();
        loadFBXBuffer(ab, f.name);
    } else if (lower.endsWith('.bvh')) {
        const text = await f.text();
        loadBVHText(text, f.name);
    }
};"""

assert old_anim_input in text, "old_anim_input not found"
text = text.replace(old_anim_input, new_anim_input, 1)

old_drop = """    if (file.name.toLowerCase().endsWith('.bvh')) {
        const text = await file.text();
        loadBVHText(text, file.name);
    } else if (file.name.toLowerCase().endsWith('.glb')) {
        await loadBuffer(await file.arrayBuffer(), file.name);
    } else {
        alert('Vui lòng kéo thả file .bvh hoặc .glb vào viewer!');
    }"""

new_drop = """    const lower = file.name.toLowerCase();
    if (lower.endsWith('.fbx')) {
        const ab = await file.arrayBuffer();
        loadFBXBuffer(ab, file.name);
    } else if (lower.endsWith('.bvh')) {
        const text = await file.text();
        loadBVHText(text, file.name);
    } else if (lower.endsWith('.glb')) {
        await loadBuffer(await file.arrayBuffer(), file.name);
    } else {
        alert('Vui lòng kéo thả file .fbx, .bvh hoặc .glb vào viewer!');
    }"""

assert old_drop in text, "old_drop not found"
text = text.replace(old_drop, new_drop, 1)

# Update btn_export_bvh to handle FBX conversion to BVH
old_export = """        if (UPLOADED_ANIMS[cur]) {
            bvhText = UPLOADED_ANIMS[cur].rawText;
        } else {"""

new_export = """        if (UPLOADED_ANIMS[cur]) {
            if (UPLOADED_ANIMS[cur].isFBX) {
                const fbx = UPLOADED_ANIMS[cur];
                const fps = 30;
                const F = Math.round(fbx.dur * fps) + 1;
                const rotArr = new Float32Array(F * NM * 4);
                for (let i = 0; i < F * NM; i++) rotArr[i * 4 + 3] = 1.0;
                const hipsArr = new Float32Array(F * 3);
                const rootArr = new Float32Array(F * 4);
                for (let f = 0; f < F; f++) {
                    const s = sampleFBX(fbx, Math.min(f / fps, fbx.dur));
                    rotArr.set(s.rot, f * NM * 4);
                    hipsArr.set(s.hips, f * 3);
                }
                const baked = {
                    id: fbx.name,
                    frames: F,
                    fps,
                    dur: fbx.dur,
                    rot: rotArr,
                    hips: hipsArr,
                    root: rootArr,
                    loop: true,
                    weapon: 'none'
                };
                bvhText = A.toBVH(baked, { units: 'm' });
            } else {
                bvhText = UPLOADED_ANIMS[cur].rawText;
            }
        } else {"""

assert old_export in text, "old_export not found"
text = text.replace(old_export, new_export, 1)

with open(html_path, "w", encoding="utf-8") as f:
    f.write(text)

print("SUCCESS: Mixamo FBX support successfully added to Model Viewer!")
