#!/usr/bin/env python3
import sys

html_path = "/Users/mth2610/Downloads/Model viewer · player.glb + 146 animation.html"
with open(html_path, "r", encoding="utf-8") as f:
    text = f.read()

# 1. Update UI in #bar
bar_old = """<label class="f">Nạp model khác (.glb)<input type="file" id="file" accept=".glb" style="display:none"></label>"""
bar_new = """<label class="f" title="Nạp file animation .bvh vào xem thử" style="cursor:pointer">📂 Nạp Anim (.bvh)<input type="file" id="anim_file" accept=".bvh" style="display:none"></label>
<button id="btn_export_bvh" title="Xuất animation đang chọn ra file .bvh cho Game Engine" style="font-weight:bold;background:var(--sw);color:#000;cursor:pointer">⬇️ Xuất BVH</button>
<button id="btn_copy_cmd" title="Copy lệnh terminal để nạp vào Wuxing Engine" style="cursor:pointer">📋 Copy lệnh Engine</button>
<label class="f">Nạp model khác (.glb)<input type="file" id="file" accept=".glb" style="display:none"></label>"""

assert bar_old in text, "bar_old not found"
text = text.replace(bar_old, bar_new, 1)

# 2. Add CATLABEL.uploaded and UPLOADED_ANIMS support
cat_old = "const CATLABEL={locomotion:"
cat_new = 'const CATLABEL={uploaded:"📂 ĐÃ NẠP / EXTERNAL",locomotion:'
assert cat_old in text, "cat_old not found"
text = text.replace(cat_old, cat_new, 1)

# 3. Add BVH Parser & handler code before poseModel
pose_old = "function poseModel(id,t,gfix){const f=A.sample(id,t)"
bvh_logic = """const UPLOADED_ANIMS = {};
function parseBVH(text, name) {
    const lines = text.split(/\\r?\\n/).map(l => l.trim()).filter(l => l.length > 0);
    let i = 0;
    if (lines[i++] !== 'HIERARCHY') throw new Error('Không phải file BVH hợp lệ');
    const bones = [], stack = [];
    let channelsTotal = 0;
    while (i < lines.length && lines[i] !== 'MOTION') {
        const line = lines[i++];
        const parts = line.split(/\\s+/);
        if (parts[0] === 'ROOT' || parts[0] === 'JOINT') {
            const bName = parts[1].replace(/^.*:/, '');
            const parent = stack.length > 0 ? stack[stack.length - 1] : -1;
            const boneIdx = bones.length;
            bones.push({ name: bName, parent, channels: [], channelOffset: channelsTotal });
            stack.push(boneIdx);
        } else if (parts[0] === 'End' && parts[1] === 'Site') {
            stack.push(-1);
        } else if (parts[0] === 'CHANNELS') {
            const count = parseInt(parts[1], 10);
            const chs = parts.slice(2, 2 + count);
            const curBone = stack[stack.length - 1];
            if (curBone >= 0) {
                bones[curBone].channels = chs;
                channelsTotal += count;
            }
        } else if (parts[0] === '}') {
            stack.pop();
        }
    }
    while (i < lines.length && lines[i] !== 'MOTION') i++;
    i++;
    let frames = 0, frameTime = 0.033333;
    while (i < lines.length && !lines[i].match(/^[\\d\\-\\.]/)) {
        if (lines[i].startsWith('Frames:')) frames = parseInt(lines[i].split(':')[1], 10);
        if (lines[i].startsWith('Frame Time:')) frameTime = parseFloat(lines[i].split(':')[1]);
        i++;
    }
    const motionData = [];
    while (i < lines.length) {
        const row = lines[i++].split(/\\s+/).map(Number);
        if (row.length >= channelsTotal) motionData.push(row);
    }
    return { name, bones, frames, frameTime, channelsTotal, motionData, dur: frames * frameTime };
}

function sampleBVH(bvh, t) {
    const fTotal = bvh.frames;
    const dur = bvh.dur || (fTotal * bvh.frameTime);
    const frameIdx = Math.min(Math.floor((t % dur) / bvh.frameTime), fTotal - 1);
    const row = bvh.motionData[frameIdx];
    const rot = new Float32Array(NM * 4);
    for (let i = 0; i < NM; i++) rot[i * 4 + 3] = 1.0;
    let hips = [0, 0.95, 0];
    bvh.bones.forEach(b => {
        const libIdx = B.findIndex(libB => libB.short === b.name);
        const off = b.channelOffset;
        if (b.name === 'Hips') {
            let px = 0, py = 0.95, pz = 0;
            b.channels.forEach((ch, chIdx) => {
                const val = row[off + chIdx];
                if (ch === 'Xposition') px = val;
                else if (ch === 'Yposition') py = val;
                else if (ch === 'Zposition') pz = val;
            });
            if (py > 10.0) { px *= 0.01; py *= 0.01; pz *= 0.01; }
            hips = [px, py, pz];
        }
        let rx = 0, ry = 0, rz = 0;
        b.channels.forEach((ch, chIdx) => {
            const val = row[off + chIdx] * Math.PI / 180.0;
            if (ch === 'Xrotation') rx = val;
            else if (ch === 'Yrotation') ry = val;
            else if (ch === 'Zrotation') rz = val;
        });
        const qz = Q.axis([0, 0, 1], rz);
        const qx = Q.axis([1, 0, 0], rx);
        const qy = Q.axis([0, 1, 0], ry);
        const q = Q.mul(qz, Q.mul(qx, qy));
        if (libIdx >= 0) {
            rot.set(q, libIdx * 4);
        }
    });
    return { id: bvh.name, t, u: dur > 0 ? (t % dur) / dur : 0, rot, hips, root: { x: 0, y: 0, z: 0, yaw: 0 } };
}

const getMeta = id => UPLOADED_ANIMS[id] ? UPLOADED_ANIMS[id].meta : A.get(id);

function poseModel(id,t,gfix){const f=UPLOADED_ANIMS[id]?sampleBVH(UPLOADED_ANIMS[id],t):A.sample(id,t)"""

assert pose_old in text, "pose_old not found"
text = text.replace(pose_old, bvh_logic, 1)

# 4. Update buildList, pick, frame, loop, seek to use getMeta
old_buildList = """function buildList(filter){const box=$('items');box.innerHTML='';const q=(filter||'').toLowerCase().trim();Object.keys(CATLABEL).forEach(c=>{const ids=A.list({category:c}).filter(id=>{const m=A.get(id);return!q||(id+' '+m.vi+' '+m.tags.join(' ')+' '+(m.element||'')+' '+m.weapon+' '+m.aliases.join(' ')).toLowerCase().includes(q)});if(!ids.length)return;
  const h=document.createElement('h4');h.textContent=CATLABEL[c]+' ('+ids.length+')';box.appendChild(h);ids.forEach(id=>{const b=document.createElement('button');b.className='mv'+(id===cur?' on':'');b.dataset.n=id;b.textContent=id;b.title=A.get(id).vi+' — '+A.get(id).desc;b.onclick=()=>pick(id);box.appendChild(b)})})}"""

new_buildList = """function buildList(filter){const box=$('items');box.innerHTML='';const q=(filter||'').toLowerCase().trim();Object.keys(CATLABEL).forEach(c=>{const ids=(c==='uploaded')?Object.keys(UPLOADED_ANIMS).filter(id=>!q||id.toLowerCase().includes(q)):A.list({category:c}).filter(id=>{const m=A.get(id);return!q||(id+' '+m.vi+' '+m.tags.join(' ')+' '+(m.element||'')+' '+m.weapon+' '+m.aliases.join(' ')).toLowerCase().includes(q)});if(!ids.length)return;
  const h=document.createElement('h4');h.textContent=CATLABEL[c]+' ('+ids.length+')';box.appendChild(h);ids.forEach(id=>{const b=document.createElement('button');b.className='mv'+(id===cur?' on':'');b.dataset.n=id;b.textContent=id;const m=getMeta(id);b.title=m.vi+(m.desc?' — '+m.desc:'');b.onclick=()=>pick(id);box.appendChild(b)})})}"""

assert old_buildList in text, "old_buildList not found"
text = text.replace(old_buildList, new_buildList, 1)

old_pick = "function pick(id){cur=id;t=0;lastU=-1;lastId=null;document.querySelectorAll('#items button').forEach(b=>b.classList.toggle('on',b.dataset.n===id));const m=A.get(id);"
new_pick = "function pick(id){cur=id;t=0;lastU=-1;lastId=null;document.querySelectorAll('#items button').forEach(b=>b.classList.toggle('on',b.dataset.n===id));const m=getMeta(id);"
assert old_pick in text, "old_pick not found"
text = text.replace(old_pick, new_pick, 1)

old_frame = "const m=A.get(cur),P=poseModel(cur,t,$('gfix').checked)"
new_frame = "const m=getMeta(cur),P=poseModel(cur,t,$('gfix').checked)"
assert old_frame in text, "old_frame not found"
text = text.replace(old_frame, new_frame, 1)

old_loop = "function loop(now){const dt=Math.min(.05,(now-last)/1000);last=now;const m=A.get(cur);"
new_loop = "function loop(now){const dt=Math.min(.05,(now-last)/1000);last=now;const m=getMeta(cur);"
assert old_loop in text, "old_loop not found"
text = text.replace(old_loop, new_loop, 1)

old_seek = "$('seek').oninput=e=>{t=+e.target.value/1000*A.get(cur).dur;playing=false;$('play').textContent='▶'};"
new_seek = "$('seek').oninput=e=>{t=+e.target.value/1000*getMeta(cur).dur;playing=false;$('play').textContent='▶'};"
assert old_seek in text, "old_seek not found"
text = text.replace(old_seek, new_seek, 1)

# 5. Add Export BVH, Copy Command, and File Drag & Drop events near the end of script
old_file_onchange = "$('file').onchange=async e=>{const f=e.target.files[0];if(f)await loadBuffer(await f.arrayBuffer(),f.name)};"
new_file_events = """function loadBVHText(text, filename) {
    try {
        const name = filename.replace(/\\.[^/.]+$/, '').replace(/[^a-zA-Z0-9_]/g, '_').toLowerCase();
        const parsed = parseBVH(text, name);
        parsed.rawText = text;
        parsed.meta = {
            id: name,
            vi: '📂 ' + name,
            category: 'uploaded',
            dur: parsed.dur,
            loop: true,
            weapon: 'none',
            aliases: [name],
            tags: ['uploaded', 'external'],
            events: []
        };
        UPLOADED_ANIMS[name] = parsed;
        buildList($('q').value);
        pick(name);
        $('msg').style.display = 'block';
        $('msg').textContent = '✅ Đã nạp anim: ' + name + ' (' + parsed.frames + ' frames, ' + parsed.dur.toFixed(2) + 's). Đang phát thử!';
        setTimeout(() => $('msg').style.display = 'none', 4500);
    } catch(err) {
        alert('Lỗi nạp BVH: ' + err.message);
    }
}

$('btn_export_bvh').onclick = () => {
    try {
        let bvhText = '';
        if (UPLOADED_ANIMS[cur]) {
            bvhText = UPLOADED_ANIMS[cur].rawText;
        } else {
            const baked = A.bake(cur, 30);
            bvhText = A.toBVH(baked, { units: 'm' });
        }
        const blob = new Blob([bvhText], { type: 'text/plain;charset=utf-8' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = cur + '.bvh';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(a.href);
        $('msg').style.display = 'block';
        $('msg').textContent = '✅ Đã xuất: ' + cur + '.bvh! Dùng lệnh: python3 scripts/import_character_anim.py ' + cur + '.bvh';
        setTimeout(() => $('msg').style.display = 'none', 5000);
    } catch(err) {
        alert('Lỗi xuất BVH: ' + err.message);
    }
};

$('btn_copy_cmd').onclick = () => {
    const cmd = `python3 scripts/import_character_anim.py character/raw_model/animations/${cur}.bvh`;
    navigator.clipboard.writeText(cmd).then(() => {
        $('msg').style.display = 'block';
        $('msg').textContent = '📋 Đã copy lệnh: ' + cmd;
        setTimeout(() => $('msg').style.display = 'none', 4000);
    }).catch(() => {
        prompt('Lệnh import vào game engine:', cmd);
    });
};

$('anim_file').onchange = async e => {
    const f = e.target.files[0];
    if (f) {
        const text = await f.text();
        loadBVHText(text, f.name);
    }
};

window.addEventListener('dragover', e => { e.preventDefault(); cv.style.outline = '3px dashed var(--sw)'; });
window.addEventListener('dragleave', e => { cv.style.outline = 'none'; });
window.addEventListener('drop', async e => {
    e.preventDefault();
    cv.style.outline = 'none';
    const files = e.dataTransfer.files;
    if (!files.length) return;
    const file = files[0];
    if (file.name.toLowerCase().endsWith('.bvh')) {
        const text = await file.text();
        loadBVHText(text, file.name);
    } else if (file.name.toLowerCase().endsWith('.glb')) {
        await loadBuffer(await file.arrayBuffer(), file.name);
    } else {
        alert('Vui lòng kéo thả file .bvh hoặc .glb vào viewer!');
    }
});

$('file').onchange=async e=>{const f=e.target.files[0];if(f)await loadBuffer(await f.arrayBuffer(),f.name)};"""

assert old_file_onchange in text, "old_file_onchange not found"
text = text.replace(old_file_onchange, new_file_events, 1)

with open(html_path, "w", encoding="utf-8") as f:
    f.write(text)

print("SUCCESS: Model viewer HTML upgraded successfully!")
