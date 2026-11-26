#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Asian Dragon V12 -> animated GLB (glTF 2.0).

Standalone source: no HTML, external mesh, texture, or Blender input required.
Dependencies: Python 3.9+, NumPy, Node.js (to execute the embedded original
procedural geometry / spline animation code). No npm packages required.

    python -m pip install numpy
    python dragon_v12.py
    python dragon_v12.py --quality mobile --variant jade --output thanh_long.glb
    python dragon_v12.py --quality hero --fps 60 --output hero.glb
    python dragon_v12.py --clips idle_float,swim_s --in-place

Edit SOURCE_JS below for geometry/rig/animation, or --palette for color overrides.
All source positions are Y-up; --scale changes meters per source unit.
WebGL procedural shader gradients are baked into vertex colors; view-dependent
lighting, bump, fog, shadows and VFX are not reproduced by standard glTF PBR.
"""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import numpy as np

# ============================================================================
# ORIGINAL V12 PROCEDURAL SOURCE (embedded; edit here to redesign the dragon)
# ============================================================================
SOURCE_JS = r"""
'use strict';
const fs = require('fs');
const opts = JSON.parse(process.env.DRAGON_OPTIONS);
const qualityKey = opts.quality;
const quality = ({
  hero:   { grid: .78, scale: [5, 4], shadow: 2048, pcf: 9, dpr: 1.75, lights: 4 },
  game:   { grid: .54, scale: [3, 3], shadow: 1024, pcf: 4, dpr: 1.35, lights: 3 },
  mobile: { grid: .35, scale: [2, 2], shadow: 0,    pcf: 0, dpr: 1.0,  lights: 1 }
})[qualityKey] || { grid: .54, scale: [3, 3], shadow: 1024, pcf: 4, dpr: 1.35, lights: 3 };

const PI = Math.PI, TAU = PI * 2;
const V = (x = 0, y = 0, z = 0) => [x, y, z];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const len = a => Math.hypot(a[0], a[1], a[2]);
const norm = a => mul(a, 1 / (len(a) || 1));
const mix = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const lerp = (a, b, t) => a + (b - a) * t;
const smooth = t => { t = clamp(t, 0, 1); return t * t * (3 - 2 * t); };
const smoother = t => { t = clamp(t, 0, 1); return t * t * t * (t * (t * 6 - 15) + 10); };

let seed = 1047;
function rnd() { seed = (1664525 * seed + 1013904223) >>> 0; return seed / 4294967296; }
const range = (a, b) => lerp(a, b, rnd());

function cat(points, t) {
  let q = clamp(t, 0, 1) * (points.length - 1);
  let i = Math.min(points.length - 2, Math.floor(q));
  let s = q - i;
  let a = points[Math.max(0, i - 1)];
  let b = points[i];
  let c = points[i + 1];
  let d = points[Math.min(points.length - 1, i + 2)];
  return b.map((x, k) => 0.5 * ((2 * x) + (-a[k] + c[k]) * s + (2 * a[k] - 5 * x + 4 * c[k] - d[k]) * s * s + (-a[k] + 3 * x - 3 * c[k] + d[k]) * s * s * s));
}

function frame(fn, t, ref = V(0, 1, 0)) {
  const p = fn(t);
  const T = norm(sub(fn(Math.min(1, t + .001)), fn(Math.max(0, t - .001))));
  let U = sub(ref, mul(T, dot(ref, T)));
  if (len(U) < .08) U = sub(V(0, 0, 1), mul(T, T[2]));
  U = norm(U);
  let R = norm(cross(T, U));
  return { p, T, U, R };
}

function basis(p, X, Y, Z) {
  return v => add(p, add(mul(X, v[0]), add(mul(Y, v[1]), mul(Z, v[2]))));
}

const hex = h => {
  let n = parseInt(h, 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255].map(x => Math.pow(x, 2.2));
};
const mat = (h, m = .1, r = .55, e = 0) => ({ c: hex(h), m, r, e });

// Semantic material channels (0..12) + fixed anatomical materials (-1)
const skin = mat('523117', .14, .62), deep = mat('211109', .05, .78), red = mat('b88834', .42, .42);
const gold = mat('c9a76a', .28, .44), darkGold = mat('5c3d1e', .15, .62), ivory = mat('c8ab7c', .06, .58);
const cream = mat('e2cfa9', .02, .68), maneRed = mat('8a491c', .04, .66), claw = mat('d9c49e', .12, .38);
const eye = mat('ffa41c', .04, .10, .48), fin = mat('8c3b15', .10, .52), finTip = mat('dfa855', .14, .44), nose = mat('422414', .08, .54);
const mouth = mat('14070d', .0, .78), tongue = mat('7d2531', .02, .42), pupil = mat('080404', .0, .10);
const tooth = mat('e4d8c0', .04, .30), glint = mat('fff9ee', .0, .06, 1.45);

const roles = ['skin','deep','scale','horn','hornDetail','belly','mane','maneAccent','claw','eye','fin','finTip','nose'];
[skin, deep, red, gold, darkGold, ivory, cream, maneRed, claw, eye, fin, finTip, nose].forEach((m, i) => m.slot = i);

// Grounded, rich ancient-bronze / organic-keratin / silk-mane PBR palettes (zero candy-plastic glare)
const variants = {
  gold: {
    name: 'KIM LONG',
    colors: ['765024','302010','c69b48','d0b27e','78512a','cbb083','e5c48a','9d4b1d','e0cfad','ffb32e','9f461b','edba62','53321d'],
    metal:  [.09,.03,.25,.13,.08,.035,.02,.03,.08,.02,.05,.07,.05],
    rough:  [.66,.79,.51,.55,.68,.62,.56,.62,.43,.17,.57,.48,.64]
  },
  fire: {
    name: 'HỎA LONG',
    colors: ['4a1513','1f080b','9e2f1d','bfa070','54311a','c79e78','e6c8a8','8f2a14','d8b78c','ff9a1c','781c14','e89344','3a1213'],
    metal:  [.08,.04,.18,.22,.12,.05,.02,.04,.12,.04,.08,.12,.06],
    rough:  [.64,.78,.46,.46,.60,.60,.70,.66,.36,.12,.52,.44,.54]
  },
  jade: {
    name: 'THANH LONG',
    colors: ['153d38','0a1f1b','2c785f','aebd98','3e523f','b1c498','d2e6d3','1b5557','b4cab2','9ef25c','114a48','72c99e','143330'],
    metal:  [.10,.04,.22,.20,.12,.05,.02,.04,.10,.04,.10,.14,.06],
    rough:  [.60,.76,.42,.46,.58,.58,.68,.66,.38,.12,.50,.42,.54]
  },
  white: {
    name: 'BẠCH LONG',
    colors: ['6e7e8c','334152','cdd7de','c4d0de','5d6f85','dfd6c7','f0ede2','7d99b0','d4dbe0','76d8ff','6b87a0','d8f2fa','566878'],
    metal:  [.10,.04,.18,.24,.14,.05,.02,.04,.12,.04,.08,.12,.06],
    rough:  [.62,.76,.46,.44,.58,.60,.70,.66,.38,.12,.52,.44,.54]
  },
  black: {
    name: 'HẮC LONG',
    colors: ['1a1f29','090c12','313947','748296','343a4a','706f74','9ea2b5','333a4f','8c9aad','ff7828','25243b','7e78b0','1c1f29'],
    metal:  [.14,.06,.30,.28,.16,.06,.02,.05,.16,.04,.10,.14,.08],
    rough:  [.58,.76,.40,.42,.58,.60,.68,.66,.36,.12,.50,.44,.52]
  }
};

function presetData(key) {
  let p = variants[key];
  if (!p) throw Error('Unknown dragon variant: ' + key);
  return Object.fromEntries(roles.map((r, i) => [r, { color: '#' + p.colors[i], metallic: p.metal[i], roughness: p.rough[i] }]));
}

let activeVariant = 'gold', currentPalette = presetData('gold');
let paletteRGB = new Float32Array(roles.length * 3), paletteFinish = new Float32Array(roles.length * 2);
function rebuildPalette() {
  roles.forEach((r, i) => {
    let p = currentPalette[r];
    paletteRGB.set(hex(p.color.replace('#', '')), i * 3);
    paletteFinish.set([p.metallic, p.roughness], i * 2);
  });
}
rebuildPalette();

const luminance = c => c[0] * .2126 + c[1] * .7152 + c[2] * .0722;
let verts = [], inds = [], skinData = [], styleData = [], binding = () => [0, 0, 1, 0];

// styleData per vertex = [slot, style, ex, ey]. For scales: style = position along the scale (0.05..1),
// ex = across-scale coordinate u, ey = dorsal factor (0 = flank/belly side, 1 = spine ridge).
function vertex(p, n, m, c = m.c, w = null, style = 0, ex = 0, ey = 0) {
  let i = verts.length / 12, slot = m.slot ?? -1;
  let shade = slot < 0 ? 1 : clamp(luminance(c) / Math.max(.00001, luminance(m.c)), .10, 2.0);
  verts.push(...p, ...norm(n), ...(slot < 0 ? c : [shade, shade, shade]), m.m, m.r, m.e);
  skinData.push(...(w || binding(p)));
  styleData.push(slot, style, ex, ey);
  return i;
}

function grid(nu, nv, fn, flip = false, exact = false) {
  if (!exact) {
    nu = Math.max(2, Math.round(nu * quality.grid));
    nv = Math.max(2, Math.round(nv * quality.grid));
  }
  let off = verts.length / 12;
  for (let j = 0; j <= nv; j++) {
    for (let i = 0; i <= nu; i++) {
      const u = i / nu, v = j / nv, { p, n, m, c, w, style, ex, ey } = fn(u, v);
      vertex(p, n, m, c || m.c, w, style || 0, ex || 0, ey || 0);
    }
  }
  for (let j = 0; j < nv; j++) {
    for (let i = 0; i < nu; i++) {
      let a = off + j * (nu + 1) + i, b = a + 1, c = a + nu + 1, d = c + 1;
      if (flip) inds.push(a, c, b, b, c, d);
      else inds.push(a, b, c, b, d, c);
    }
  }
}

function surface(nu, nv, fn, m, col, flip = false, styleFn = null, exact = false) {
  grid(nu, nv, (u, v) => {
    let p = fn(u, v);
    let du = sub(fn(Math.min(1, u + .0006), v), fn(Math.max(0, u - .0006), v));
    let dv = sub(fn(u, Math.min(1, v + .0006)), fn(u, Math.max(0, v - .0006)));
    let n = norm(cross(du, dv));
    if (flip) n = mul(n, -1);
    let st = styleFn ? styleFn(u, v) : 0;
    if (!Array.isArray(st)) st = [st, 0, 0];
    return { p, n, m, c: col ? col(u, v) : m.c, style: st[0], ex: st[1], ey: st[2] };
  }, flip, exact);
}

function ell(center, rad, m, axes = [V(1,0,0), V(0,1,0), V(0,0,1)], seg = 22, rings = 14, tintMul = 1) {
  const [X, Y, Z] = axes;
  grid(seg, rings, (u, v) => {
    let a = u * TAU, b = v * PI;
    let d = [Math.sin(b) * Math.cos(a), Math.cos(b), Math.sin(b) * Math.sin(a)];
    return {
      p: add(center, add(mul(X, d[0] * rad[0]), add(mul(Y, d[1] * rad[1]), mul(Z, d[2] * rad[2])))),
      n: add(mul(X, d[0] / rad[0]), add(mul(Y, d[1] / rad[1]), mul(Z, d[2] / rad[2]))),
      m,
      c: tintMul === 1 ? m.c : mul(m.c, tintMul)
    };
  }, true);
}

// Polar-oriented anatomical eyeball (+Z = outward cornea center) with crisp limbal ring & iris shading
function dragonEyeball(center, rad, m, axes, seg = 28, rings = 18) {
  const [X, Y, Z] = axes;
  grid(seg, rings, (u, v) => {
    let a = u * TAU, b = v * PI; // b=0 at cornea apex (+Z), b=PI/2 at equator
    let sx = Math.sin(b) * Math.cos(a), sy = Math.sin(b) * Math.sin(a), sz = Math.cos(b);
    let rIris = Math.sin(b); // 0 at pupil center, 1 at equator
    // Natural fiery iris gradient: glowing golden core, warm amber mid-iris, crisp dark limbal ring near edge
    let radialFibers = .94 + .06 * Math.sin(a * 18.0);
    let coreGlow = 1.24 - .28 * smooth((rIris - .18) / .45);
    let limbalDark = 1.0 - .62 * smooth((rIris - .62) / .24);
    let upperShadow = 1.0 - .18 * smooth((sy - .25) / .65);
    let shade = clamp(radialFibers * coreGlow * limbalDark * upperShadow, .22, 1.35);
    return {
      p: add(center, add(mul(X, sx * rad[0]), add(mul(Y, sy * rad[1]), mul(Z, sz * rad[2])))),
      n: add(mul(X, sx / rad[0]), add(mul(Y, sy / rad[1]), mul(Z, sz / rad[2]))),
      m,
      c: mul(m.c, shade),
      style: clamp(rIris, 0, 1)
    };
  }, false, true);
}

function tube(points, rad, m, seg = 20, sides = 12, ref = V(0,1,0), ellip = 1, col = null, styleFn = null) {
  let fn = t => cat(points, t);
  grid(sides, seg, (u, t) => {
    const f = frame(fn, t, ref), a = u * TAU, r = typeof rad === 'function' ? rad(t) : rad;
    const dr = add(mul(f.U, Math.cos(a)), mul(f.R, Math.sin(a) * ellip));
    const n = add(mul(f.U, Math.cos(a)), mul(f.R, Math.sin(a) / ellip));
    return { p: add(f.p, mul(dr, r)), n, m, c: col ? col(u, t) : m.c, style: styleFn ? styleFn(u, t) : 0 };
  });
}

// Soft tapered silk-flame hair lock with natural root-to-tip maneAccent gradient & subtle twist
function strand(points, w, m, ref = V(0,0,1), flat = .36, grad = 0, twist = 0, gp = 1.15) {
  let fn = t => cat(points, t);
  surface(10, 18, (u, t) => {
    let f = frame(fn, t, ref), a = u * TAU + twist * t;
    let profile = Math.pow(Math.sin(PI * (.24 + .76 * t)), .46) * Math.pow(Math.max(0, 1 - t), .30);
    let rr = w * (.012 + 1.12 * profile) + .0007;
    let flute = 1 + .075 * Math.cos(u * TAU * 3.0);
    return add(f.p, mul(add(mul(f.U, Math.cos(a)), mul(f.R, Math.sin(a) * flat)), rr * flute));
  }, m, (u, t) => mul(m.c, .72 + .10 * Math.cos(u * TAU * 3.0) + .24 * Math.pow(t, .65)), false,
  grad > 0 ? (u, t) => grad * Math.pow(1 - t, gp) : null);
}

// Majestic S-coiled spine (proud elevated chest & neck, sleek sweeping coils)
const anchors = [
  V(-7.15, 2.32, 1.00), V(-6.45, 2.20, .68), V(-5.55, 1.72, .06),
  V(-4.35, .70, -.62), V(-2.90, -.16, -.58), V(-1.35, -.24, .20),
  V(.25, .45, 1.00), V(1.85, 1.32, .92), V(3.35, 1.45, -.10),
  V(4.70, .80, -1.00), V(5.85, -.15, -1.00), V(6.85, -.60, -.25),
  V(7.80, -.35, .72), V(8.75, .40, 1.08), V(9.65, 1.04, .75)
];

let samples = [], total = 0;
for (let i = 0; i <= 1100; i++) {
  let p = cat(anchors, i / 1100);
  if (i) total += len(sub(p, samples[i - 1].p));
  samples.push({ p, d: total });
}
function spine(t) {
  let d = clamp(t, 0, 1) * total, l = 0, h = samples.length - 1;
  while (h - l > 1) {
    let k = (h + l) >> 1;
    if (samples[k].d < d) l = k; else h = k;
  }
  return mix(samples[l].p, samples[h].p, (d - samples[l].d) / (samples[h].d - samples[l].d || 1));
}

let frames = [];
for (let i = 0; i <= 660; i++) {
  let t = i / 660, p = spine(t);
  let T = norm(sub(spine(Math.min(1, t + .002)), spine(Math.max(0, t - .002))));
  let U = norm(sub(V(0, 1, 0), mul(T, T[1])));
  let R = norm(cross(T, U));
  frames.push({ p, T, U, R });
}
function F(t) {
  let q = clamp(t, 0, 1) * 660, i = Math.min(659, Math.floor(q)), a = frames[i], b = frames[i+1];
  let T = norm(mix(a.T,b.T,q-i)), U = norm(mix(a.U,b.U,q-i));
  U = norm(sub(U,mul(T,dot(T,U))));
  return { p: spine(t), T, U, R: norm(cross(T,U)) };
}

function radius(t) {
  let neckToChest = .47 + .27 * Math.exp(-Math.pow((t - .19) / .18, 2));
  let midSwell = .14 * Math.exp(-Math.pow((t - .53) / .20, 2));
  let taper = Math.pow(Math.max(.001, 1 - Math.pow(t, 1.78)), 1.04);
  return (neckToChest + midSwell) * taper + .018;
}

function bodyShape(a) {
  let c = Math.cos(a), s = Math.sin(a);
  let rx = 1.0 + .05 * Math.sin(PI * Math.max(0, c));
  let ry = c > 0 ? (1.06 + .05 * Math.pow(c, 4)) : (.90 + .06 * (1 + c));
  return [ry * c, rx * s];
}
function radial(f, a) { return add(mul(f.U, Math.cos(a)), mul(f.R, Math.sin(a))); }
function bodyPoint(t, a, offset = 0) {
  let f = F(t), r = radius(t), [cy, cx] = bodyShape(a);
  let dir = add(mul(f.U, cy), mul(f.R, cx));
  let n = norm(dir);
  return add(f.p, add(mul(dir, r), mul(n, offset)));
}

// Named hierarchical bind skeleton (38 bones)
const bones = [], NS = 24;
function bone(name, parent, p, axis = V(0, 1, 0), kind = 'spine') {
  let id = bones.length;
  bones.push({ name, parent, p, axis: norm(axis), kind });
  return id;
}
for (let i = 0; i < NS; i++) {
  let f = F(i / (NS - 1));
  bone(i === 0 ? 'neck_00' : i < 5 ? 'neck_' + String(i).padStart(2, '0') : i < 17 ? 'spine_' + String(i - 5).padStart(2, '0') : 'tail_' + String(i - 17).padStart(2, '0'), i - 1, f.p, f.U, 'spine');
}

const hf = F(0);
const rawHZ = mul(hf.T, -1);
const HZ = norm(add(mul(rawHZ, .88), V(rawHZ[0] * .22, -.14, rawHZ[2] * .22)));
const HY = norm(sub(V(0, 1, 0), mul(HZ, HZ[1])));
const HX = norm(cross(HY, HZ));
const headBase = basis(hf.p, HX, HY, HZ), HA = [HX, HY, HZ];
const H = v => headBase(V(v[0]*1.13, v[1]*1.12, v[2] > 0 ? v[2]*.89 : v[2]));

const HEAD = bone('head', 0, H(V(0, .03, -.18)), HY, 'head');
const JAW  = bone('jaw', HEAD, H(V(0, -.21, -.04)), HX, 'jaw');

function rigid(i) { return [i, i, 1, 0]; }
function blend(i, j, t) { t = clamp(t, 0, 1); return [i, j, 1 - t, t]; }
function spineW(t) { let q = clamp(t, 0, 1) * (NS - 1), i = Math.min(NS - 2, Math.floor(q)); return blend(i, i + 1, q - i); }
function nearSpine(p, t) {
  let k = Math.round(t * 660), best = k, dd = Infinity;
  for (let i = Math.max(0, k - 14); i <= Math.min(660, k + 14); i++) {
    let d = sub(p, frames[i].p), d2 = dot(d, d);
    if (d2 < dd) { best = i; dd = d2; }
  }
  return spineW(best / 660);
}

// Base torso hide (deep matte shadow crevices under scales for grounded armor depth)
grid(60, 360, (u, t) => {
  let a = u * TAU, f = F(t), n = radial(f, a);
  let belly = Math.max(0, (-Math.cos(a) - .38) / .62);
  let dorsalAO = .82 - .16 * Math.pow(Math.max(0, Math.cos(a)), 6);
  return { p: bodyPoint(t, a), n, m: skin, c: mul(skin.c, (.60 + .18 * belly) * dorsalAO), w: spineW(t) };
});

// Sculpted keeled dragon scales with deep root AO and satin rim highlight
function scaleWidth(v) {
  // Buried broad root, broad shoulder, then a pointed leaf-shaped trailing edge.
  return Math.max(.008, (.89 + .11 * Math.sin(PI * Math.min(1,v*2))) * Math.pow(Math.max(0,1-Math.pow(v,3.0)),.63));
}
function scaleRelief(u, v) {
  let x = 2*u-1, side = Math.max(0,1-x*x);
  return .002 + .022 * smooth(v/.72) + .023 * Math.pow(v,1.4)*side + .009*Math.exp(-Math.pow(x/.35,2))*Math.sin(PI*v);
}
function scaleShade(u, v, variation) {
  let x = Math.abs(2*u-1);
  return variation * (.77 + .23*smooth(v/.50)) * (1.01 - .085*Math.pow(x,3) - .08*Math.pow(v,12));
}

function scale(p, X, Y, Z, w, h, m = red, variation = 1, dors = .5) {
  const base = basis(p, X, Y, Z);
  surface(...quality.scale, (u, v) => {
    let x = 2 * u - 1, side = scaleWidth(v), relief = scaleRelief(u, v) * w;
    return base(V(x * w * side * .5, (v - .42) * h, relief));
  }, m, (u, v) => mul(m.c, scaleShade(u, v, variation)), false, (u, v) => [.05 + .95 * v, u, dors], true);
}

// Body scales along arc length
let rows = [], d = .08;
while (d < total * .995) {
  let t = d / total, r = radius(t), step = clamp(.255 * Math.sqrt(r / .62), .070, .270);
  rows.push({ t, step });
  d += step;
}
for (let j = 0; j < rows.length; j++) {
  let { t, step } = rows[j], r = radius(t);
  if (r < .032) continue;
  let count = Math.max(7, Math.round(TAU * r / clamp(.265 * Math.sqrt(r / .62), .078, .280)));
  let dt = step / total * 1.76;
  binding = p => nearSpine(p, t);
  for (let k = 0; k < count; k++) {
    let a = TAU * (k + (j % 2) * .5) / count;
    if (Math.cos(a) < -.51) continue;
    let width = TAU / count * 1.39;
    let dorsalTint = .88 + .15 * Math.cos(a);
    let variation = range(.96, 1.04) * dorsalTint;
    let dors = clamp((Math.cos(a) + .56) / 1.56, 0, 1);
    surface(...quality.scale, (u, v) => {
      let tt = clamp(t + (v - .40) * dt, 0, 1);
      let aa = a + (2 * u - 1) * width * .5 * scaleWidth(v);
      let lift = scaleRelief(u, v) * Math.min(r, .70) * .82;
      return bodyPoint(tt, aa, lift);
    }, red, (u, v) => mul(red.c, scaleShade(u, v, variation)), false, (u, v) => [.05 + .95 * v, u, dors], true);
  }
}

// Overlapping armored ventral scutes with crisp shaded grooves
let bellyD = .12;
while (bellyD < total * .985) {
  let t = bellyD / total, r = radius(t), pitch = clamp(.34 * Math.sqrt(r / .60), .075, .36);
  let dt = pitch / total * 1.16, shade = range(.94, 1.06);
  binding = p => nearSpine(p, t);
  surface(20, 7, (u, v) => {
    let a = PI + (u - .5) * 2.18 * (.95 + .05 * Math.sin(PI * v));
    let tt = clamp(t + (v - .45) * dt, 0, 1);
    let side = Math.pow(Math.sin(PI * u), .34);
    let lift = radius(tt) * (.012 + .014 * Math.sin(PI * v) + .026 * v) * side;
    return bodyPoint(tt, a, lift);
  }, ivory, (u, v) => mul(ivory.c, shade * (.72 + .28 * smooth(v / .48)) * (.88 + .12 * Math.sin(PI * u))));
  bellyD += pitch;
}

// Swept organic dorsal crest fin
function finProfile(t) {
  let q = clamp((t - .014) / .970, 0, 1);
  let env = Math.pow(Math.max(0, Math.sin(PI * Math.pow(q, .72))), .42);
  let napeBoost = 1.0 + .52 * Math.exp(-Math.pow((q - .13) / .14, 2)) + .16 * Math.exp(-Math.pow((q - .55) / .18, 2));
  let r = radius(t);
  let cycle = q * 29;
  let phase = cycle - Math.floor(cycle);
  let wave = Math.pow(Math.sin(PI * Math.pow(phase, .47)), 1.6);
  let sub = .08 * Math.sin(q * 11 * PI);
  return Math.min(.78, r * 1.12 * napeBoost) * env * (.16 + .88 * wave + sub*.5);
}
function finPoint(t, h, side) {
  let f = F(t), height = finProfile(t), q = clamp((t - .014) / .970, 0, 1);
  let phase = (q * 29) % 1;
  let root = bodyPoint(t, 0, -.016);
  let sweep = height * (1.30 + .20 * Math.sin(PI * phase)) * Math.pow(h, 1.50);
  let rib = .72 + .28 * Math.pow(Math.sin(PI * Math.pow(phase, .68)), 6);
  let thick = (.023 + .013 * rib) * Math.pow(Math.max(0, 1 - h), .72) + .002;
  return add(root, add(mul(f.U, height * h), add(mul(f.T, sweep), mul(f.R, side * thick))));
}
for (let side of [-1, 1]) {
  binding = () => spineW(0);
  grid(10, 780, (u, v) => {
    let t = .014 + .970 * v, p = finPoint(t, u, side);
    let du = sub(finPoint(t, Math.min(1, u + .001), side), finPoint(t, Math.max(0, u - .001), side));
    let dv = sub(finPoint(Math.min(.984, t + .00018), u, side), finPoint(Math.max(.014, t - .00018), u, side));
    let n = mul(norm(cross(du, dv)), -side);
    let phase = (v * 29) % 1;
    let ridge = Math.pow(Math.sin(PI * Math.pow(phase, .68)), 8);
    let shade = .68 + .24 * u + .14 * ridge;
    return {
      p, n, m: fin, c: mul(fin.c, shade),
      w: spineW(clamp(t + finProfile(t) * 1.30 * Math.pow(u, 1.50) / total, 0, 1)),
      style: smooth((u - .18) / .82)
    };
  }, side === 1);
}
grid(2, 780, (u, v) => {
  let t = .014 + .970 * v, p = mix(finPoint(t, 1, -1), finPoint(t, 1, 1), u);
  return { p, n: F(t).U, m: fin, c: mul(fin.c, .95), style: 1, w: spineW(clamp(t + finProfile(t) * 1.30 / total, 0, 1)) };
}, false);

// ============================================================================
// NOBLE IMPERIAL DRAGON HEAD (+Z snout, +Y dorsal, ±X sides)
// ============================================================================
binding = () => rigid(HEAD);
function he(p, r, m, axes = HA, s = 22) { ell(H(p), r, m, axes, s, 15); }
function ht(ps, r, m, seg = 22, side = 11, ref = HY, e = 1, col = null, sf = null) { tube(ps.map(H), r, m, seg, side, ref, e, col, sf); }
function hs(ps, w, m, ref = HX, flat = .38, grad = 0, tw = 0) { strand(ps.map(H), w, m, ref, flat, grad, tw); }

function skullLoft(sections, m, detail = 48, sculptFn = null) {
  let fn = t => cat(sections.map(q => q.slice(0, 3)), t);
  let rr = t => cat(sections.map(q => [q[3], q[4], 0]), t);
  surface(64, detail, (u, t) => {
    let [z, y, w] = fn(t), [up, down] = rr(t);
    let a = u * TAU, c = Math.cos(a), s = Math.sin(a);
    let x = s * Math.max(.001, w);
    let yy = y + c * (c >= 0 ? Math.max(.001, up) : Math.max(.001, down));
    if (sculptFn) {
      let res = sculptFn(x, yy, z, c, s, t);
      x = res[0]; yy = res[1]; z = res[2];
    }
    return H(V(x, yy, z));
  }, m, (u, t) => {
    let c = Math.cos(u * TAU);
    let ridgeHi = .10 * Math.pow(Math.max(0, c), 4);
    let underAO = c < -.15 ? -.22 * (-c - .15) : 0;
    return mul(m.c, .74 + .20 * Math.max(0, c) + ridgeHi + underAO + .06 * Math.sin(PI * t));
  }, true);
}

// Sculpted aristocratic cranium, lean nasal bridge & noble lion-dragon muzzle
const upperSections = [
  [-.92, .02, .006, .006, .006],
  [-.72, .06, .35,  .40,  .32],
  [-.36, .12, .56,  .56,  .35],
  [ .06, .14, .64,  .64,  .32],
  [ .42, .12, .53,  .48,  .25],
  [ .76, .07, .40,  .30,  .19],
  [1.12, .06, .38,  .25,  .17],
  [1.44, .09, .45,  .25,  .16],
  [1.68, .12, .37,  .21,  .12],
  [1.81, .13, .18,  .12,  .07],
  [1.86, .13, .005, .005, .005]
];

skullLoft(upperSections, red, 52, (x, y, z, c, s) => {
  let absS = Math.abs(s);
  let snoutMask = smooth((z - .34) / .42) * (1 - smooth((z - 1.65) / .20));
  // Chiseled flat-topped nasal bridge
  if (c > .18) {
    x *= 1 - .14 * Math.pow(c, 2) * snoutMask;
    y += .036 * Math.pow(c, 4) * snoutMask;
  }
  // Clean orbital recess so the bold eye sits naturally inside the skull
  let eyeMask = Math.exp(-Math.pow((z - .42) / .22, 2));
  if (absS > .52 && c > -.08 && c < .72) {
    let pocket = Math.sin(PI * clamp((c + .08) / .80, 0, 1)) * smooth((absS - .52) / .35);
    x *= 1 - .18 * eyeMask * pocket;
  }
  // Subtle hollowing along mid-snout sides
  let cheekHollow = Math.exp(-Math.pow((z - .94) / .28, 2));
  if (absS > .75 && Math.abs(c) < .45) {
    x *= 1 - .09 * cheekHollow * (1 - Math.abs(c) / .45);
  }
  // Subtle fierce maxillary lip curve
  if (c < .05 && absS > .45) {
    let lipWeight = smooth((absS - .45) / .45) * (1 - smooth((-.65 - c) / .35));
    let snarlLift = .042 * Math.exp(-Math.pow((z - 1.15) / .24, 2));
    let fangDrop  = -.030 * Math.exp(-Math.pow((z - 1.47) / .14, 2));
    y += (snarlLift + fangDrop) * lipWeight;
  }
  // Lion-nose central cleft at snout tip
  if (z > 1.54 && c > 0 && absS < .42) {
    let tipMask = smooth((z - 1.54) / .24);
    let cleft = (1 - absS / .42) * Math.pow(c, 1.5);
    y -= .034 * tipMask * cleft;
    z -= .030 * tipMask * cleft;
  }
  return [x, y, z];
});

// Upper palate
skullLoft([
  [.02, -.14, .06, .02, .03],
  [.36, -.15, .39, .04, .04],
  [.84, -.11, .31, .035, .035],
  [1.36, -.05, .34, .03, .035],
  [1.62, -.02, .09, .01, .01]
], mouth, 22);

// Leathery sculpted nose pad & dorsal forehead crest
he(V(0, .135, 1.72), V(.23, .095, .125), nose);
hs([V(0, .33, 1.64), V(0, .38, 1.36), V(0, .34, 1.00), V(0, .57, .54), V(0, .72, .06), V(0, .62, -.46)], .135, skin, HX, .56);

// Sleek interlocking nasal bridge armor plates
for (let k = 0; k < 8; k++) {
  let z = .54 + k * .142;
  let y = .345 - .065 * Math.sin(k / 7 * PI * .85) + (k > 5 ? (k - 5) * .026 : 0);
  let w = .235 - .038 * Math.sin(k / 7 * PI) + (k > 5 ? .035 : 0);
  let tilt = k < 2 ? -.26 : (k > 5 ? .14 : -.04);
  let Z = norm(add(HY, mul(HZ, tilt))), Y = norm(sub(mul(HZ, -1), mul(Z, dot(mul(HZ, -1), Z)))), X = norm(cross(Y, Z));
  scale(H(V(0, y, z)), X, Y, Z, w, .185, red, .94 + .05 * (k % 2), 1);
}

// Lower jaw (lean armored mandible)
binding = () => rigid(JAW);
skullLoft([
  [-.14, -.24, .05, .04, .04],
  [ .12, -.32, .40, .11, .17],
  [ .48, -.33, .38, .10, .16],
  [ .88, -.28, .30, .09, .13],
  [1.28, -.21, .29, .08, .12],
  [1.56, -.14, .22, .065, .085],
  [1.67, -.11, .005, .005, .005]
], red, 38, (x, y, z, c, s) => {
  if (c < -.3) y += .028 * (-c - .3);
  if (Math.abs(s) > .7 && c < .1) x *= 1.04;
  return [x, y, z];
});
skullLoft([
  [.14, -.22, .05, .01, .02],
  [.48, -.23, .33, .04, .04],
  [.98, -.19, .24, .035, .035],
  [1.42, -.12, .18, .025, .025],
  [1.55, -.10, .01, .005, .005]
], mouth, 20);
ht([V(0, -.19, .35), V(0, -.18, .82), V(0, -.14, 1.24), V(0, -.11, 1.44)], t => .15 * Math.sin(PI * (.15 + .85 * t)) * (1 - .45 * t) + .012, tongue, 18, 10, HY, .42);

// Armored chin scutes along lower mandible
for (let k = 0; k < 5; k++) {
  let z = .36 + k * .26, y = -.43 + k * .058, w = .35 - k * .042;
  let Z = norm(add(mul(HY, -1), mul(HZ, .22))), Y = norm(sub(mul(HZ, -1), mul(Z, dot(mul(HZ, -1), Z)))), X = norm(cross(Y, Z));
  scale(H(V(0, y, z)), X, Y, Z, w, .24, ivory, .94 + .05 * (k % 2));
}

// Bilateral head features
binding = () => rigid(HEAD);
for (const s of [-1, 1]) {
  // ========================================================================
  // 1. NOBLE, WIDE-OPEN, BLAZING IMPERIAL DRAGON EYE (Long Mục — ZERO squint!)
  // ========================================================================
  const outward = norm(add(mul(HX, s * .76), add(mul(HZ, .64), mul(HY, .03))));
  const eyeUp = norm(sub(HY, mul(outward, dot(HY, outward))));
  const eyeH = norm(cross(eyeUp, outward));
  const backSign = dot(eyeH, mul(HZ, -1)) >= 0 ? 1 : -1;
  const eyeAxes = [eyeH, eyeUp, outward];
  const ec = H(V(s * .465, .285, .43));

  // Deep velvety socket rim
  ell(ec, V(.225, .151, .079), deep, eyeAxes, 26, 14);
  // Full, commanding, luminous amber-gold eyeball with radial iris shading & dark limbal ring
  const eyeb = add(ec, mul(outward, .040));
  dragonEyeball(eyeb, V(.151, .113, .086), eye, eyeAxes, 32, 20);
  // Dignified sharp dark almond-vertical slit pupil (bold, intense, focused)
  const pupilPos = add(eyeb, mul(outward, .081));
  ell(pupilPos, V(.028, .077, .010), pupil, eyeAxes, 20, 14);
  // Dual corneal specular glints (gives instant life, soul, and intensity)
  ell(add(pupilPos, add(mul(eyeUp, .036), mul(eyeH, -.026 * backSign))), V(.019, .016, .008), glint, eyeAxes, 12, 8);
  ell(add(pupilPos, add(mul(eyeUp, -.028), mul(eyeH, .022 * backSign))), V(.009, .008, .006), glint, eyeAxes, 10, 6);

  // Clean, taut, aristocratic eyelids framing the eye wide open (NO thick pinching tubes!)
  function eyelid(top) {
    let pts = [];
    for (let i = 0; i <= 14; i++) {
      let q = i / 14;
      let tSym = (q - .5) * backSign; // -0.5 inner corner (nose), +0.5 outer corner (temple)
      let xOff = (q - .5) * .36;
      // Gentle dignified almond tilt: slightly anchored inner corner, proud swept outer corner
      let slant = tSym * .055;
      // Open vertical aperture: top arch stays cleanly above iris center (+0.106), bottom sits cleanly below (-0.102)
      let arch = top
        ? (.091 * Math.sin(PI * Math.pow(q, .90)) + .004)
        : (-.090 * Math.sin(PI * q) - .004);
      let outPop = (top ? .060 : .058) + .055 * Math.sin(PI * q);
      pts.push(add(ec, add(mul(eyeH, xOff), add(mul(eyeUp, arch + slant), mul(outward, outPop)))));
    }
    tube(pts, t => (top ? .024 : .018) + (top ? .012 : .008) * Math.sin(PI * t), top ? skin : red, 24, 9, outward);
  }
  eyelid(true);
  eyelid(false);

  // 2. Sculpted noble supraorbital brow ridge (mi cốt) hovering cleanly ABOVE the eye
  ht([
    V(s * .14, .32, 1.00),
    V(s * .35, .44, .74),
    V(s * .57, .47, .38),
    V(s * .66, .54, -.04),
    V(s * .56, .45, -.42)
  ], t => .105 * Math.sin(PI * (.14 + .82 * t)) + .020, skin, 26, 10, HY, .78);

  // Organic windswept brow plume (varied lengths & curves, NOT stiff parallel pins)
  const browLocks = [
    { z: .70, x: .36, y: .46, l: .78, flair: .16, lift: .16, w: .052, tw: .4 },
    { z: .58, x: .42, y: .50, l: 1.02, flair: .24, lift: .22, w: .064, tw: -.3 },
    { z: .44, x: .48, y: .53, l: 1.24, flair: .32, lift: .26, w: .072, tw: .5 },
    { z: .28, x: .54, y: .54, l: 1.12, flair: .30, lift: .18, w: .062, tw: -.4 }
  ];
  for (let bl of browLocks) {
    hs([
      V(s * bl.x, bl.y, bl.z),
      V(s * (bl.x + bl.flair * .45), bl.y + bl.lift * .60, bl.z - bl.l * .30),
      V(s * (bl.x + bl.flair * .95), bl.y + bl.lift * .95, bl.z - bl.l * .66),
      V(s * (bl.x + bl.flair * .85), bl.y + bl.lift * .45, bl.z - bl.l)
    ], bl.w, cream, HX, .46, .78, bl.tw * s);
  }

  // 3. Sculpted cheekbone ridge & flush armor cheek plates
  ht([
    V(s * .34, .06, .88),
    V(s * .54, .07, .46),
    V(s * .63, .05, .02),
    V(s * .55, -.03, -.40)
  ], t => .068 * Math.sin(PI * (.15 + .85 * t)) + .018, red, 20, 9, HY, .72);

  for (let j = 0; j < 5; j++) {
    for (let k = 0; k < 3; k++) {
      let z = -.36 + j * .22, y = -.08 + k * .11;
      if (z > .18 && z < .68 && k === 2) continue; // keep eye socket clean & wide
      let x = s * (z > .65 ? (.36 - .04 * k) : (.56 - .06 * Math.abs(y)));
      let Z = norm(add(mul(HX, s * .92), add(mul(HY, y * .55), mul(HZ, .15))));
      let Y = norm(sub(mul(HZ, -1), mul(Z, dot(mul(HZ, -1), Z))));
      let X = norm(cross(Y, Z));
      scale(H(V(x, y, z)), X, Y, Z, .13, .18, red, .88 + .12 * ((j + k) % 2));
    }
  }

  // 4. Sculpted cloud-scroll nostrils (long tỵ)
  he(V(s * .25, .20, 1.67), V(.105, .055, .055), mouth);
  ht([
    V(s * .10, .22, 1.72),
    V(s * .24, .27, 1.66),
    V(s * .40, .20, 1.50),
    V(s * .37, .12, 1.32)
  ], t => .032 + .026 * Math.sin(PI * t), skin, 20, 10, HY, .85);

  // 5. Lip ridge & clean interlocking saber fangs
  ht([
    V(s * .34, -.01, 1.66),
    V(s * .43, -.035, 1.38),
    V(s * .39, -.01, 1.06),
    V(s * .42, -.10, .64),
    V(s * .51, -.14, .18)
  ], t => .032 + .016 * Math.sin(PI * t), skin, 24, 9);

  const upperTeeth = [
    { z: 1.54, x: .26, y: -.01, l: .11, r: .032 },
    { z: 1.41, x: .37, y: -.03, l: .25, r: .048 }, // main saber fang
    { z: 1.24, x: .36, y: -.02, l: .10, r: .032 },
    { z: 1.08, x: .34, y: -.02, l: .09, r: .030 },
    { z: .92,  x: .35, y: -.05, l: .12, r: .034 },
    { z: .76,  x: .37, y: -.08, l: .14, r: .036 },
    { z: .58,  x: .40, y: -.11, l: .10, r: .031 },
    { z: .42,  x: .41, y: -.13, l: .08, r: .028 }
  ];
  for (let tk of upperTeeth) {
    binding = () => rigid(HEAD);
    let x = s * tk.x;
    ht([
      V(x, tk.y, tk.z),
      V(x * .97, tk.y - tk.l * .58, tk.z + .015),
      V(x * .86, tk.y - tk.l, tk.z - .034)
    ], t => tk.r * Math.pow(1 - t, .72) + .002, tooth, 11, 9);
  }
  const lowerTeeth = [
    { z: 1.48, x: .21, y: -.12, l: .10, r: .028 },
    { z: 1.31, x: .29, y: -.14, l: .19, r: .040 },
    { z: 1.06, x: .28, y: -.18, l: .09, r: .028 },
    { z: .82,  x: .30, y: -.21, l: .10, r: .030 },
    { z: .58,  x: .33, y: -.23, l: .08, r: .026 }
  ];
  for (let tk of lowerTeeth) {
    binding = () => rigid(JAW);
    let x = s * tk.x;
    ht([
      V(x, tk.y, tk.z),
      V(x * .96, tk.y + tk.l * .58, tk.z + .010),
      V(x * .88, tk.y + tk.l, tk.z - .022)
    ], t => tk.r * Math.pow(1 - t, .75) + .002, tooth, 10, 8);
  }

  // 6. Majestic branching antlers: dark coronet root -> golden shaft -> polished ivory tips, swept back with a graceful curl
  binding = () => rigid(HEAD);
  ht([
    V(s * .33, .45, -.10),
    V(s * .39, .53, -.22),
    V(s * .46, .60, -.34)
  ], t => .215 * (1 - .24 * t) * (1 + .14 * Math.sin(PI * t)), darkGold, 10, 14, HX, .92);

  const horn = [
    V(s * .37, .49, -.18),
    V(s * .60, .88, -.54),
    V(s * .86, 1.28, -1.12),
    V(s * 1.00, 1.66, -1.82),
    V(s * 1.13, 2.02, -2.36),
    V(s * 1.08, 2.52, -2.78)
  ];
  const hornR = t => .215 * Math.pow(1 - t, .74) + .005;
  ht(horn, hornR, gold, 48, 16, HX, .88,
    (u, t) => mul(gold.c, .80 + .22 * smooth(t / .55) + .05 * Math.cos(u * TAU * 4)),
    (u, t) => Math.max(.002, t));

  const tines = [
    { t: .20, mid: V(s * .16, .34, .22),  tip: V(s * .32, .74, .37),  r: .112 },
    { t: .44, mid: V(s * .22, .38, .04),  tip: V(s * .46, 1.04, -.06),  r: .098 },
    { t: .66, mid: V(s * .14, .30, -.06), tip: V(s * .28, .66, -.18), r: .078 },
    { t: .82, mid: V(s * .06, .22, .04),  tip: V(s * .14, .52, .14),  r: .058 }
  ];
  for (let tn of tines) {
    let p = cat(horn, tn.t);
    ht([p, add(p, tn.mid), add(p, tn.tip)], q => tn.r * Math.pow(1 - q, .74) + .005, gold, 20, 12, HX, .88,
      (u, t) => mul(gold.c, .86 + .20 * t),
      (u, q) => 2 + clamp(.30 + .70 * q, 0, 1));
  }
  for (let k = 0; k < 6; k++) {
    let t = .04 + k * .075, p = cat(horn, t), a = frame(q => cat(horn, q), t, V(1, 0, 0));
    let r = hornR(t), pp = [];
    for (let q = 0; q <= 14; q++) pp.push(add(p, mul(radial(a, q / 14 * TAU), r * 1.010)));
    ht(pp, .0105 * (1 - .5 * t), darkGold, 18, 6, HX);
  }

  // 7. Sleek swept ear fins
  for (let k = 0; k < 3; k++) {
    hs([
      V(s * .49, .24 - k * .05, -.16),
      V(s * (.82 + k * .04), .29 + k * .04, -.58),
      V(s * (1.14 + k * .09), .32 + k * .08, -1.04 - k * .08)
    ], .145 - k * .020, fin, HX, .25);
  }

  // 8. Flowing silk-gold whiskers: long, fine, S-waved and trailing back instead of a stiff loop
  he(V(s * .39, .06, 1.46), V(.072, .052, .072), skin);
  const wave = (pts, ax, ay, f) => pts.map((q, i) => V(q[0] + s * ax * Math.sin(i * f) * Math.min(1, i / 3), q[1] + ay * Math.cos(i * f * .8), q[2]));
  const whisk = wave([
    V(s * .39, .06, 1.48),
    V(s * .66, .08, 1.76),
    V(s * 1.00, -.04, 1.74),
    V(s * 1.36, -.30, 1.48),
    V(s * 1.68, -.68, 1.02),
    V(s * 1.90, -1.08, .46),
    V(s * 1.96, -1.44, -.16),
    V(s * 1.84, -1.74, -.74)
  ], .08, .05, 1.25);
  ht(whisk, t => .050 * Math.pow(1 - t, .85) + .004, gold, 96, 9, HY);
  const whisk2 = wave([
    V(s * .45, -.07, 1.10),
    V(s * .82, -.20, 1.16),
    V(s * 1.18, -.36, .84),
    V(s * 1.46, -.56, .30),
    V(s * 1.58, -.80, -.30),
    V(s * 1.50, -1.02, -.84)
  ], .08, .05, 1.6);
  ht(whisk2, t => .026 * Math.pow(1 - t, .85) + .003, cream, 64, 8, HY, 1, null, (u, t) => .70 * Math.pow(1 - t, 1.2));
  // fine short chin whiskers
  ht(wave([
    V(s * .30, -.20, 1.30),
    V(s * .62, -.40, 1.10),
    V(s * .98, -.62, .62),
    V(s * 1.18, -.84, .02)
  ], .05, .04, 1.4), t => .016 * Math.pow(1 - t, .85) + .002, cream, 36, 7, HY, 1, null, (u, t) => .55 * Math.pow(1 - t, 1.1));

  // 9. Organic clustered windswept mane (varied twist, curl, length — ZERO stiff comb rows!)
  const sideMane = [
    // Upper temple sweeping plumes
    { p: [.44, .46, -.18], l: 1.55, fl: .42, up: .38, w: .155, tw: .7,  g: .84 },
    { p: [.52, .34, -.24], l: 1.85, fl: .58, up: .24, w: .170, tw: -.6, g: .88 },
    { p: [.56, .18, -.28], l: 1.95, fl: .66, up: .08, w: .165, tw: .8,  g: .86 },
    { p: [.54, .00, -.26], l: 1.65, fl: .54, up: -.08,w: .145, tw: -.7, g: .90 },
    { p: [.46,-.16, -.20], l: 1.38, fl: .40, up: -.18,w: .125, tw: .5,  g: .88 },
    // Deeper neck-hugger under-layer for lush silhouette without comb gaps
    { p: [.40, .40, -.44], l: 1.35, fl: .32, up: .28, w: .140, tw: -.5, g: .94 },
    { p: [.48, .22, -.48], l: 1.55, fl: .42, up: .10, w: .150, tw: .6,  g: .95 },
    { p: [.46, .02, -.46], l: 1.45, fl: .36, up: -.10,w: .135, tw: -.4, g: .95 },
    { p: [.38,-.18, -.42], l: 1.20, fl: .28, up: -.22,w: .115, tw: .5,  g: .94 },
    // Wispy aerodynamic outer accent locks
    { p: [.56, .28, -.16], l: 1.40, fl: .72, up: .34, w: .082, tw: 1.1, g: .70 },
    { p: [.58, .08, -.18], l: 1.52, fl: .76, up: .04, w: .078, tw: -1.0,g: .72 },
    { p: [.50,-.12, -.12], l: 1.18, fl: .55, up: -.22,w: .072, tw: .9,  g: .75 }
  ];
  for (let m of sideMane) {
    let [x0, y0, z0] = [s * m.p[0], m.p[1], m.p[2]];
    hs([
      V(x0, y0, z0),
      V(x0 + s * m.fl * .42, y0 + m.up * .52, z0 - m.l * .30),
      V(x0 + s * m.fl * .92, y0 + m.up * .96 + .06, z0 - m.l * .66),
      V(x0 + s * m.fl * .74, y0 + m.up * .55 - .08, z0 - m.l)
    ], m.w * 1.60, cream, HX, .63, m.g, m.tw * s, .85);
  }

  // 10. Graceful swept rear-jaw cheek whiskers (leaves armored mid-jaw clean — NO chin ribcage!)
  binding = () => rigid(JAW);
  const jawLocks = [
    { p: [.34, -.30, .14], l: .95, fl: .22, dn: .28, w: .085, tw: .5 },
    { p: [.30, -.34, .34], l: .82, fl: .18, dn: .32, w: .072, tw: -.4 },
    { p: [.22, -.35, .64], l: .86, fl: .12, dn: .38, w: .066, tw: .4 }
  ];
  for (let jl of jawLocks) {
    let [x0, y0, z0] = [s * jl.p[0], jl.p[1], jl.p[2]];
    hs([
      V(x0, y0, z0),
      V(x0 + s * jl.fl * .45, y0 - jl.dn * .55, z0 - jl.l * .28),
      V(x0 + s * jl.fl * .85, y0 - jl.dn * .92, z0 - jl.l * .66),
      V(x0 + s * jl.fl * .60, y0 - jl.dn * .68, z0 - jl.l)
    ], jl.w * 1.25, cream, HX, .56, .86, jl.tw * s);
  }
}

// Crown dorsal mane crest (flowing seamlessly backward into dorsal fin)
binding = () => rigid(HEAD);
const crownLocks = [
  { x:  .00, y: .58, z:  .06, l: 1.58, up: .44, w: .155, tw:  .2 },
  { x: -.13, y: .55, z: -.04, l: 1.48, up: .38, w: .142, tw: -.5 },
  { x:  .13, y: .55, z: -.04, l: 1.48, up: .38, w: .142, tw:  .5 },
  { x: -.25, y: .50, z: -.14, l: 1.36, up: .32, w: .130, tw: -.7 },
  { x:  .25, y: .50, z: -.14, l: 1.36, up: .32, w: .130, tw:  .7 },
  { x: -.08, y: .50, z: -.30, l: 1.38, up: .28, w: .135, tw:  .4 },
  { x:  .08, y: .50, z: -.30, l: 1.38, up: .28, w: .135, tw: -.4 }
];
for (let cl of crownLocks) {
  hs([
    V(cl.x, cl.y, cl.z),
    V(cl.x * 1.28, cl.y + cl.up * .75, cl.z - cl.l * .30),
    V(cl.x * 1.52, cl.y + cl.up,       cl.z - cl.l * .66),
    V(cl.x * 1.35, cl.y + cl.up * .35, cl.z - cl.l)
  ], cl.w * 1.45, cream, HX, .64, .90, cl.tw);
}

// Focused, dignified imperial chin goatee (clustered at chin tip & swept cleanly back)
binding = () => rigid(JAW);
const goateeLocks = [
  { x:  .00, z: 1.06, l: 1.08, dn: .46, w: .092, tw:  .0 },
  { x: -.09, z: 1.00, l:  .96, dn: .42, w: .078, tw: -.5 },
  { x:  .09, z: 1.00, l:  .96, dn: .42, w: .078, tw:  .5 },
  { x: -.17, z:  .88, l:  .84, dn: .36, w: .065, tw: -.6 },
  { x:  .17, z:  .88, l:  .84, dn: .36, w: .065, tw:  .6 }
];
for (let glk of goateeLocks) {
  hs([
    V(glk.x, -.35, glk.z),
    V(glk.x * 1.15, -.35 - glk.dn * .60, glk.z - .16),
    V(glk.x * 1.05, -.35 - glk.dn * .95, glk.z - glk.l * .58),
    V(glk.x * .65,  -.35 - glk.dn * .72, glk.z - glk.l * 1.02)
  ], glk.w * 1.22, cream, HX, .54, .82, glk.tw);
}

// ============================================================================
// MUSCULAR PREDATORY LIMBS, GRASPING TALONS & SWEPT ELBOW FLAME RIBBONS
// ============================================================================
function leg(t, s, front) {
  let f = F(t), S = mul(f.R, s), D = mul(f.U, -1), T = f.T, r = radius(t);
  let P = (x, y, z) => add(f.p, add(mul(S, x), add(mul(D, y), mul(T, z))));

  let a = P(r * .58, r * .22, 0);
  let b = P(r + .28, .38, front ? .30 : -.13);
  let c = P(r + .50, .82, front ? .63 : .49);
  let d = P(r + .62, 1.24, front ? .15 : .79);
  let e = P(r + .70, 1.53, front ? -.43 : .30);

  let label = (front ? 'fore' : 'hind') + (s < 0 ? '_L' : '_R');
  let parent = Math.round(t * (NS - 1));
  let axis = norm(cross(sub(c, a), sub(e, c)));
  if (len(axis) < .1) axis = T;

  let upper = bone(label + '_upper', parent, a, axis, 'upper');
  let lower = bone(label + '_lower', upper, c, axis, 'lower');
  let foot  = bone(label + '_foot',  lower, e, axis, 'foot');

  const proj = (p, x, y) => dot(sub(p, x), sub(y, x)) / dot(sub(y, x), sub(y, x));
  const limbWeight = p => {
    let q = proj(p, a, c), v = proj(p, c, e);
    if (q < .26) return blend(parent, upper, smooth(q / .26));
    if (q < .79 && v < .10) return rigid(upper);
    if (v < .22) return blend(upper, lower, smooth((q - .79 + Math.max(0, v)) / .38));
    if (v < .76) return rigid(lower);
    return blend(lower, foot, smooth((v - .76) / .30));
  };

  binding = limbWeight;
  ell(a, V(.36, .36, .46), red, [S, D, T], 22, 14, .80);
  tube([a, b, c], q => .19 + .22 * Math.pow(Math.sin(PI * (.18 + .82 * q)), .85), red, 28, 16, T, .90, (u, q) => mul(red.c, .76 + .22*Math.sin(PI*q)));
  ell(c, V(.18, .18, .20), red, [S, D, T]);
  tube([c, d, e], q => lerp(.20, .125, q) + .042 * Math.sin(PI * q), red, 26, 15, T, .86);

  let fn = q => cat([a, b, c, d, e], q);
  for (let j = 0; j < 19; j++) {
    let q = (j + .4) / 19, ff = frame(fn, q, T);
    let rr = q < .5
      ? (.19 + .22 * Math.pow(Math.sin(PI * (.18 + .82 * q * 2)), .85))
      : lerp(.20, .125, (q - .5) * 2) + .042 * Math.sin(PI * (q - .5) * 2);
    for (let k = 0; k < 10; k++) {
      let aa = TAU * (k + (j % 2) * .5) / 10, Z = radial(ff, aa), X = norm(cross(ff.T, Z));
      scale(add(ff.p, mul(Z, rr * .93)), X, ff.T, Z, rr * .83, .19, red, range(.90, 1.10));
    }
  }

  // Flowing, sinuous elbow/hock flame ribbons (organic curve, not stiff triangles)
  const tuftSpecs = [
    { q: .45, l: .78, w: .092, out: .14, lift: -.16, tw:  .5 },
    { q: .68, l: 1.04,w: .086, out: .22, lift: -.08, tw: -.6 },
    { q: .86, l: .92, w: .076, out: .20, lift:  .04, tw:  .5 },
    { q: 1.00,l: .72, w: .064, out: .14, lift:  .12, tw: -.4 }
  ];
  for (let ts of tuftSpecs) {
    let p0 = mix(b, c, ts.q);
    strand([
      p0,
      add(p0, add(mul(S, ts.out * .5), add(mul(T, ts.l * .32), mul(D, ts.lift * .5)))),
      add(p0, add(mul(S, ts.out * 1.1), add(mul(T, ts.l * .72), mul(D, ts.lift)))),
      add(p0, add(mul(S, ts.out * .75), add(mul(T, ts.l * 1.12), mul(D, ts.lift * .6))))
    ], ts.w, cream, T, .42, .90, ts.tw * s);
  }

  // Predatory grasping eagle/dragon talons
  binding = () => rigid(foot);
  ell(e, V(.23, .15, .27), red, [S, D, T]);

  for (let k = 0; k < 5; k++) {
    let rear = k === 4;
    let spreadIdx = rear ? 0 : (k - 1.5);
    let baseDir = rear
      ? norm(add(mul(T, .88), mul(S, -.30)))
      : norm(add(mul(T, -.88), add(mul(S, .32 + spreadIdx * .28), mul(D, .08))));
    let root = add(e, add(mul(T, rear ? .16 : -.08), mul(S, rear ? -.04 : spreadIdx * .092)));
    let seg1 = rear ? .18 : (.24 - .025 * Math.abs(spreadIdx));
    let seg2 = rear ? .14 : (.21 - .02 * Math.abs(spreadIdx));
    let p1 = add(root, add(mul(baseDir, seg1), mul(D, -.05)));
    let p2 = add(p1, add(mul(baseDir, seg2), mul(D, .13)));
    let clawTip = add(p2, add(mul(baseDir, .06), mul(D, .24)));
    let clawHook = add(clawTip, add(mul(baseDir, -.14), mul(D, .08)));

    tube([root, p1, p2], q => lerp(.085, .050, q), red, 14, 10, T);
    ell(p1, V(.060, .062, .060), red);
    tube([p2, add(p2, add(mul(baseDir, .10), mul(D, .08))), clawTip, clawHook],
      q => .067 * Math.pow(1 - q, .72) + .002, claw, 16, 10, T);
  }
}

leg(.165, -1, true);
leg(.165,  1, true);
leg(.565, -1, false);
leg(.565,  1, false);

// ============================================================================
// FLOWING SILK-FLAME TAIL PLUME
// ============================================================================
// Unified flame plume, with broad interlocking locks and a sweeping centreline.
for (let i = 0; i < 23; i++) {
  let ring = i % 3, idx = Math.floor(i / 3), ang = idx/8*TAU + ring*.67;
  let t = .905 + ring*.029 + .006*Math.sin(idx*2.4);
  let f = F(t), R = radial(f,ang), p = bodyPoint(t,ang,-.008);
  let wob = Math.sin(i*2.399), L = (2.65 + .42*wob) * (1-ring*.12);
  let flare = (.34 + .09*wob)*(1-ring*.13), sweep = .32 + .12*Math.cos(ang);
  binding = () => spineW(t);
  strand([p,
    add(p,add(mul(f.T,L*.27),add(mul(R,flare*.82),mul(f.U,.10)))),
    add(p,add(mul(f.T,L*.64),add(mul(R,flare*1.38),mul(f.U,sweep)))),
    add(p,add(mul(f.T,L),add(mul(R,flare*.48),mul(f.U,sweep+.32*wob))))
  ], .205-ring*.028, cream, f.U, .60, .98, wob*.65, .75);
}

// Flowing neck mane: broad silk locks hugging the upper neck and streaming back in the wind
for (let side of [-1, 1]) {
  for (let i = 0; i < 10; i++) {
    let q = i / 9, t = .040 + q * .22;
    let f = F(t), r = radius(t);
    let a = side * (.80 + .22 * ((i * 5) % 3) / 2);
    let root = bodyPoint(t, a, -.012);
    let out = norm(radial(f, a));
    let vary = .84 + .32 * ((i * 7) % 5) / 4;
    let L = (1.50 + .55 * Math.sin(PI * Math.min(1, q * 1.1 + .15))) * vary;
    let wv = ((i % 2) ? 1 : -1) * .10;
    binding = () => spineW(t);
    strand([
      add(root, mul(out, -.02)),
      add(root, add(mul(f.T, L * .30), add(mul(out, .08 + r * .16), mul(f.U, .06)))),
      add(root, add(mul(f.T, L * .66), add(mul(out, .18 + r * .30 + wv), mul(f.U, .12)))),
      add(root, add(mul(f.T, L * 1.05), add(mul(out, .34 + r * .46), mul(f.U, .14 - wv))))
    ], .165 + .040 * vary, cream, out, .48, .92, (i % 2 ? .4 : -.4) * side, .85);
  }
}

// ============================================================================
// GROUND STAGE & WEBGL2 PIPELINE
// ============================================================================
function mm(a, b) {
  let r = [];
  for (let j = 0; j < 4; j++)
    for (let i = 0; i < 4; i++)
      r[j * 4 + i] = a[i] * b[j * 4] + a[4 + i] * b[j * 4 + 1] + a[8 + i] * b[j * 4 + 2] + a[12 + i] * b[j * 4 + 3];
  return r;
}
const identity = () => [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
function trans(p) { let m = identity(); m[12] = p[0]; m[13] = p[1]; m[14] = p[2]; return m; }
function rot(a, ang) {
  let [x, y, z] = a, c = Math.cos(ang), s = Math.sin(ang), t = 1 - c;
  return [t*x*x+c, t*x*y+s*z, t*x*z-s*y, 0, t*x*y-s*z, t*y*y+c, t*y*z+s*x, 0, t*x*z+s*y, t*y*z-s*x, t*z*z+c, 0, 0, 0, 0, 1];
}
function transform(m, p) {
  return V(m[0]*p[0] + m[4]*p[1] + m[8]*p[2] + m[12], m[1]*p[0] + m[5]*p[1] + m[9]*p[2] + m[13], m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14]);
}
function rotVec(v, axis, ang) {
  let c = Math.cos(ang), s = Math.sin(ang), d = dot(axis, v), cr = cross(axis, v);
  return add(mul(v, c), add(mul(cr, s), mul(axis, d * (1 - c))));
}

// Right-handed column-major frame [T, U, R, P] and its exact rigid inverse
function invFrameMat(T, U, R, P) {
  return [
    T[0], U[0], R[0], 0,
    T[1], U[1], R[1], 0,
    T[2], U[2], R[2], 0,
    -dot(T, P), -dot(U, P), -dot(R, P), 1
  ];
}
function frameToQuat(T, U, R) {
  let m00 = T[0], m01 = U[0], m02 = R[0];
  let m10 = T[1], m11 = U[1], m12 = R[1];
  let m20 = T[2], m21 = U[2], m22 = R[2];
  let tr = m00 + m11 + m22, x, y, z, w, s;
  if (tr > 0) {
    s = Math.sqrt(tr + 1) * 2; w = .25 * s; x = (m21 - m12) / s; y = (m02 - m20) / s; z = (m10 - m01) / s;
  } else if (m00 > m11 && m00 > m22) {
    s = Math.sqrt(1 + m00 - m11 - m22) * 2; w = (m21 - m12) / s; x = .25 * s; y = (m01 + m10) / s; z = (m02 + m20) / s;
  } else if (m11 > m22) {
    s = Math.sqrt(1 + m11 - m00 - m22) * 2; w = (m02 - m20) / s; x = (m01 + m10) / s; y = .25 * s; z = (m12 + m21) / s;
  } else {
    s = Math.sqrt(1 + m22 - m00 - m11) * 2; w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = .25 * s;
  }
  let l = Math.hypot(x, y, z, w) || 1;
  return [x / l, y / l, z / l, w / l];
}
function slerpQuat(a, b, t) {
  let bx = b[0], by = b[1], bz = b[2], bw = b[3];
  let cosom = a[0]*bx + a[1]*by + a[2]*bz + a[3]*bw;
  if (cosom < 0) { cosom = -cosom; bx = -bx; by = -by; bz = -bz; bw = -bw; }
  let s0, s1;
  if (1 - cosom > 1e-4) {
    let om = Math.acos(clamp(cosom, -1, 1)), sinom = Math.sin(om);
    s0 = Math.sin((1 - t) * om) / sinom; s1 = Math.sin(t * om) / sinom;
  } else { s0 = 1 - t; s1 = t; }
  let q = [s0*a[0] + s1*bx, s0*a[1] + s1*by, s0*a[2] + s1*bz, s0*a[3] + s1*bw];
  let l = Math.hypot(q[0], q[1], q[2], q[3]) || 1;
  return [q[0]/l, q[1]/l, q[2]/l, q[3]/l];
}
function quatPosToMat(q, P) {
  let [x, y, z, w] = q, xx = x*x, yy = y*y, zz = z*z, xy = x*y, xz = x*z, yz = y*z, wx = w*x, wy = w*y, wz = w*z;
  return [
    1 - 2*(yy + zz), 2*(xy + wz),     2*(xz - wy),     0,
    2*(xy - wz),     1 - 2*(xx + zz), 2*(yz + wx),     0,
    2*(xz + wy),     2*(yz - wx),     1 - 2*(xx + yy), 0,
    P[0],            P[1],            P[2],            1
  ];
}

// Precompute inverse bind frames for all NS=24 spine bones so skinning maps cleanly from bind->live curve
const bindSpineFrames = [], bindSpineInv = [];
for (let i = 0; i < NS; i++) {
  let f = F(i / (NS - 1));
  bindSpineFrames.push(f);
  bindSpineInv.push(invFrameMat(f.T, f.U, f.R, f.p));
}

// Resample any 3D curve C(s) by EXACT arc-length = total so the dragon NEVER stretches or compresses
function resampleSpineArcLength(curveFn, sampleSteps = 104, maxParam = total * 1.38) {
  let pts = new Array(sampleSteps + 1), dists = new Float32Array(sampleSteps + 1);
  let acc = 0;
  for (let k = 0; k <= sampleSteps; k++) {
    let s = (k / sampleSteps) * maxParam;
    pts[k] = curveFn(s);
    if (k > 0) acc += len(sub(pts[k], pts[k - 1]));
    dists[k] = acc;
  }
  let out = new Array(NS);
  let segLen = total / (NS - 1);
  let k = 0;
  for (let i = 0; i < NS; i++) {
    let targetD = i * segLen;
    while (k < sampleSteps - 1 && dists[k + 1] < targetD) k++;
    if (targetD <= dists[sampleSteps]) {
      let span = dists[k + 1] - dists[k];
      let alpha = span > 1e-6 ? (targetD - dists[k]) / span : 0;
      out[i] = mix(pts[k], pts[k + 1], alpha);
    } else {
      let dir = norm(sub(pts[sampleSteps], pts[sampleSteps - 1]));
      out[i] = add(pts[sampleSteps], mul(dir, targetD - dists[sampleSteps]));
    }
  }
  return out;
}

// Parallel-Transport (Bishop) frame solver along NS spine positions + controlled uprightness & banking
function solveSpineFrames(pos, opts = {}) {
  const upright = opts.upright ?? 0.22;
  const refUp = opts.refUp || V(0, 1, 0);
  const bankFn = opts.bankFn || null;
  const tangents = new Array(NS);
  for (let i = 0; i < NS; i++) {
    let a = pos[Math.max(0, i - 1)], b = pos[Math.min(NS - 1, i + 1)];
    tangents[i] = norm(sub(b, a));
  }

  const baseU = new Array(NS);
  // Seed U[0] perpendicular to T[0]
  let u0 = sub(refUp, mul(tangents[0], dot(refUp, tangents[0])));
  if (len(u0) < .10) {
    let alt = Math.abs(tangents[0][2]) < .85 ? V(0, 0, 1) : V(1, 0, 0);
    u0 = sub(alt, mul(tangents[0], dot(alt, tangents[0])));
  }
  baseU[0] = norm(u0);

  for (let i = 1; i < NS; i++) {
    let tPrev = tangents[i - 1], tCurr = tangents[i];
    let ax = cross(tPrev, tCurr), sinA = len(ax);
    let u = baseU[i - 1];
    if (sinA > 1e-5) {
      let ang = Math.atan2(sinA, clamp(dot(tPrev, tCurr), -1, 1));
      u = rotVec(u, mul(ax, 1 / sinA), ang);
    }
    u = norm(sub(u, mul(tCurr, dot(u, tCurr))));
    // Gentle upright righting when flying horizontally (vanishes smoothly on vertical climb/dive -> zero flip!)
    let horiz = Math.max(0, 1 - tCurr[1] * tCurr[1]);
    if (upright > 0 && horiz > .04) {
      let worldU = norm(sub(V(0, 1, 0), mul(tCurr, tCurr[1])));
      if (dot(u, worldU) > -.15) {
        u = norm(mix(u, worldU, upright * horiz));
        u = norm(sub(u, mul(tCurr, dot(u, tCurr))));
      }
    }
    baseU[i] = u;
  }

  const quats = new Array(NS);
  for (let i = 0; i < NS; i++) {
    let T = tangents[i], U = baseU[i], R = norm(cross(T, U));
    // Auto curvature banking + explicit clip bank function
    let roll = 0;
    if (bankFn) {
      // Lateral curvature approximation from adjacent tangents
      let curv = 0;
      if (i > 0 && i < NS - 1) {
        let dT = sub(tangents[i + 1], tangents[i - 1]);
        curv = dot(dT, R);
      }
      roll = bankFn(i / (NS - 1), curv, T, U, R);
    }
    if (Math.abs(roll) > 1e-5) {
      let c = Math.cos(roll), s = Math.sin(roll);
      let nU = norm(add(mul(U, c), mul(R, s)));
      let nR = norm(cross(T, nU));
      U = nU; R = nR;
    }
    quats[i] = frameToQuat(T, U, R);
  }
  return quats;
}

// ============================================================================
// 10 SERPENTINE MOTIONS & COMBAT SKILL TEMPLATES (EXTENSIBLE REGISTRY)
// ============================================================================
const motionParams = { ampX: 1.0, ampY: 1.0, freq: 1.0, bank: 1.0 };
const pulse = (x, a, b, c, d) => smooth((x - a) / (b - a)) * (1 - smooth((x - c) / (d - c)));

const clips = {
  // 1. DU LONG — Majestic cloud-riding serpentine glide (steady head, progressive neck->tail wave, zero hip-wiggle)
  swim_s: {
    name: 'Du Long · Uốn chữ S',
    duration: 3.6,
    loop: true,
    events: [],
    solve(u, p) {
      const ph = u * TAU;
      const ax = 1.45 * p.ampX, ay = 1.10 * p.ampY, fr = 1.10 * p.freq;
      const pos = resampleSpineArcLength(s => {
        let x = s / total;
        let w = ph - x * fr * TAU;
        // True anguilliform envelope: poised head & chest, fluid curve growing progressively toward tail
        let envZ = .14 + .58 * Math.pow(x, 1.25) + .65 * Math.pow(x, 2.4);
        let envY = .22 + .55 * smooth(x / .55) + .35 * x * x;
        let px = -7.4 + s * .96;
        let pz = Math.sin(w) * ax * envZ;
        let neckArch = 1.55 * Math.exp(-x * 5.0) - .45 * Math.exp(-Math.pow((x - .24) / .18, 2));
        let py = .85 + neckArch + Math.sin(w - .95) * ay * envY + .14 * Math.sin(ph);
        return V(px, py, pz);
      });
      const quats = solveSpineFrames(pos, {
        upright: .38,
        bankFn: (x, curv) => clamp(-curv * .48 * p.bank, -.28, .28) * smooth(x / .35)
      });
      // Gaze-stabilized head: gently counter-steers against neck tangent so head glides focused forward
      let fwd = norm(sub(pos[0], pos[1]));
      let neckYaw = Math.atan2(fwd[2], -fwd[0]);
      let neckPitch = Math.asin(clamp(fwd[1], -.7, .7));
      return {
        pos, quats,
        head: [-neckPitch * .55 + .03 * Math.sin(ph), neckYaw * .68, -neckYaw * .16 * p.bank],
        jaw: (jawDegrees + 1.5 * Math.sin(ph)) * PI / 180,
        limbs: (isFront, side) => {
          let lv = Math.sin(ph - (isFront ? .25 : 1.55) + side * .35);
          return [(-.30 + .09 * lv), (.22 - .11 * lv), (.05 * lv)];
        },
        previewPath: (k) => {
          let x = k, w = ph - x * fr * TAU;
          let envZ = .14 + .58 * Math.pow(x, 1.25) + .65 * Math.pow(x, 2.4);
          let envY = .22 + .55 * smooth(x / .55) + .35 * x * x;
          let neckArch = 1.55 * Math.exp(-x * 5.0) - .45 * Math.exp(-Math.pow((x - .24) / .18, 2));
          return V(-7.4 + x * total * .96, .85 + neckArch + Math.sin(w - .95) * ay * envY + .14 * Math.sin(ph), Math.sin(w) * ax * envZ);
        }
      };
    }
  },

  // 2. VÂN LONG — Regal hovering idle with seamless breathing waves & poised head
  idle_float: {
    name: 'Vân Long · Lơ lửng',
    duration: 4.4,
    loop: true,
    events: [],
    solve(u, p) {
      const ph = u * TAU;
      const ax = .44 * p.ampX, ay = .38 * p.ampY, fr = 1.0 * p.freq;
      const pos = resampleSpineArcLength(s => {
        let x = clamp(s / total, 0, 1);
        let bp = spine(x);
        let w = ph - x * fr * TAU;
        let tailGain = .12 + .88 * Math.pow(x, 1.25);
        return V(
          bp[0] + .10 * Math.sin(ph - x * 2.0) * p.ampX,
          bp[1] + .24 * Math.sin(ph) + Math.sin(w - .6) * ay * tailGain,
          bp[2] + Math.sin(w) * ax * tailGain
        );
      }, 96, total * 1.06);
      const quats = solveSpineFrames(pos, {
        upright: .38,
        bankFn: (x) => .07 * p.bank * Math.cos(ph - x * fr * TAU) * smooth(x / .4)
      });
      return {
        pos, quats,
        head: [.04 * Math.sin(ph), .05 * Math.sin(ph - .4), .02 * Math.cos(ph)],
        jaw: (jawDegrees + 1.4 * Math.sin(ph)) * PI / 180,
        limbs: (isFront, side) => {
          let lv = Math.sin(ph + side * .35 + (isFront ? 0 : 1.3));
          return [.05 * lv, -.08 * lv, .03 * lv];
        }
      };
    }
  },

  // 3. BÁT TỰ PHIÊN VÂN — 3D Figure-8 Follow-the-Leader Weave
  figure8: {
    name: 'Bát Tự · Lượn số 8',
    duration: 6.8,
    loop: true,
    events: [],
    solve(u, p, withRoot) {
      const headTheta = u * TAU;
      const Rx = 10.6, Rz = 9.8, Ry = 1.55 + .85 * p.ampY;
      const waveAmp = .58 * p.ampX, fr = 1.65 * p.freq;
      function lemniscate(th, s) {
        let x = s / total;
        let px = Rx * Math.sin(th);
        let pz = Rz * Math.sin(th) * Math.cos(th);
        let py = 1.6 + Ry * Math.sin(th * 2.0 + .45);
        // Subtle lateral serpentine propulsion along local curve normal
        let dx = Rx * Math.cos(th), dz = Rz * Math.cos(2 * th);
        let nl = Math.hypot(dx, dz) || 1;
        let nx = -dz / nl, nz = dx / nl;
        let ripple = Math.sin(headTheta * 3.0 - x * fr * TAU) * waveAmp * (.3 + .7 * x);
        return V(px + nx * ripple, py, pz + nz * ripple);
      }
      const pos = resampleSpineArcLength(s => lemniscate(headTheta - s * .142, s), 120, total * 1.45);
      if (!withRoot) {
        let c = pos[0];
        for (let i = 0; i < NS; i++) pos[i] = sub(pos[i], V(c[0] + 6.5, c[1] - 1.8, c[2]));
      }
      const quats = solveSpineFrames(pos, {
        upright: .22,
        bankFn: (x, curv) => clamp(-curv * 1.35 * p.bank, -.68, .68)
      });
      return {
        pos, quats,
        head: [.06 * Math.cos(headTheta * 2), 0, clamp(-Math.sin(headTheta) * .22 * p.bank, -.35, .35)],
        jaw: (jawDegrees + 2.5 * Math.sin(headTheta * 2)) * PI / 180,
        limbs: (isFront, side) => {
          let lv = Math.sin(headTheta * 3 + (isFront ? 0 : 1.5) + side * .3);
          return [-.42 + .08 * lv, .32 - .08 * lv, .10 * lv]; // aerodynamic tuck
        },
        previewPath: (k) => lemniscate(k * TAU, 0)
      };
    }
  },

  // 4. BÀN LONG QUẤN TRỤ — Coiling Protective Orbit
  coil_orbit: {
    name: 'Bàn Long · Quấn trụ',
    duration: 5.2,
    loop: true,
    events: [],
    solve(u, p, withRoot) {
      const headTheta = u * TAU;
      const R = 5.4 + .65 * p.ampX;
      const fr = 1.4 * p.freq;
      function orbitPt(th, s) {
        let x = s / total;
        let rLocal = R + .42 * p.ampX * Math.sin(headTheta * 2 - x * fr * TAU);
        let py = 1.35 + (1.35 * p.ampY) * Math.sin(th * 2.0) + .55 * Math.cos(x * PI);
        return V(Math.cos(th) * rLocal, py, Math.sin(th) * rLocal);
      }
      const pos = resampleSpineArcLength(s => orbitPt(headTheta - s / R, s), 110, total * 1.35);
      if (!withRoot) {
        let c = pos[0];
        for (let i = 0; i < NS; i++) pos[i] = sub(pos[i], V(c[0] + 5.5, c[1] - 1.6, c[2]));
      }
      const quats = solveSpineFrames(pos, {
        upright: .24,
        bankFn: (x, curv) => clamp(-.34 * p.bank - curv * .75 * p.bank, -.65, .65)
      });
      return {
        pos, quats,
        head: [.08 * Math.cos(headTheta * 2), .18, -.22 * p.bank],
        jaw: (jawDegrees + 3.0) * PI / 180,
        limbs: (isFront, side) => {
          let lv = Math.sin(headTheta * 2 + (isFront ? 0 : 1.4) + side * .4);
          return [-.25 + .10 * lv, .18 - .10 * lv, .06 * lv];
        },
        previewPath: (k) => orbitPt(k * TAU, 0)
      };
    }
  },

  // 5. THANH LONG THĂNG THIÊN — Ascending Dragon Vortex / Summon
  spiral_rise: {
    name: 'Thanh Long Thăng Thiên',
    duration: 3.8,
    loop: false,
    events: [[.35, 'emerge'], [1.55, 'vortex_ascend'], [2.55, 'roar_peak'], [3.40, 'ready']],
    solve(u, p, withRoot) {
      const climb = smoother(clamp(u / .82, 0, 1));
      const roar = pulse(u, .58, .72, .86, .98);
      const settle = smoother(clamp((u - .72) / .28, 0, 1));
      const headProgress = -1.2 + climb * 12.6; // arc parameter moving up the helix
      function helixPt(h) {
        let prog = clamp(h / 11.4, -.5, 1.2);
        let th = h * (.46 * p.freq) - .9;
        let r = lerp(4.8, 2.5, smooth(prog)) * (.72 + .28 * p.ampX);
        // Smoothly level out into a proud horizontal circle at the top
        let y = -4.2 + h * (.82 * (.75 + .25 * p.ampY)) - 1.4 * Math.pow(Math.max(0, prog - .75), 2) * 3.0;
        return V(Math.cos(th) * r, y, Math.sin(th) * r);
      }
      const pos = resampleSpineArcLength(s => helixPt(headProgress - s * .56), 120, total * 1.45);
      if (!withRoot) {
        let c = pos[0];
        for (let i = 0; i < NS; i++) pos[i] = sub(pos[i], V(c[0] + 5.0, c[1] - 2.2, c[2]));
      }
      const quats = solveSpineFrames(pos, {
        upright: .14,
        refUp: V(0, 1, 0),
        bankFn: (x) => -.38 * p.bank * (1 - .4 * settle)
      });
      return {
        pos, quats,
        head: [-.36 * roar + .12 * (1 - climb), .12 * Math.sin(u * TAU), -.18 * p.bank],
        jaw: (jawDegrees + 34 * roar + 8 * climb) * PI / 180,
        limbs: (isFront, side) => {
          let splay = roar * .35;
          return [-.28 + splay, .22 - splay * .6, .12 * roar];
        },
        previewPath: (k) => helixPt(-1.2 + k * 12.6)
      };
    }
  },

  // 6. THẦN LONG PHÁ KHÔNG — Coiled Anticipation -> Corkscrew Piercing Lunge
  coil_lunge: {
    name: 'Thần Long Phá Không',
    duration: 2.2,
    loop: false,
    events: [[.22, 'coil_windup'], [.68, 'launch'], [1.05, 'pierce_impact'], [1.65, 'recover']],
    solve(u, p, withRoot) {
      const wind = pulse(u, .02, .26, .34, .44);
      const dash = smoother(clamp((u - .28) / .30, 0, 1)) * (1 - smoother(clamp((u - .66) / .34, 0, 1)));
      const strike = pulse(u, .30, .44, .58, .76);
      const corkscrew = smoother(clamp((u - .30) / .45, 0, 1)) * TAU * 1.0 * p.bank;
      const headX = withRoot ? (-7.2 + 3.8 * wind - 9.4 * dash) : -7.2;
      const ax = (1.1 + 2.35 * wind - .85 * strike) * p.ampX;
      const fr = (1.1 + 1.05 * wind - .42 * strike) * p.freq;
      const pos = resampleSpineArcLength(s => {
        let x = s / total;
        let compression = 1 - .30 * wind + .12 * strike;
        let px = headX + s * .88 * compression;
        let w = x * fr * TAU + .4 * wind - u * 8.5 * strike;
        let env = Math.sin(PI * Math.min(1, .12 + x * .92));
        let pz = Math.sin(w) * ax * env;
        let py = 1.25 - .65 * wind * Math.exp(-x * 3) + .35 * Math.cos(w) * (.5 + .5 * wind) * p.ampY;
        return V(px, py, pz);
      }, 110, total * 1.45);
      const quats = solveSpineFrames(pos, {
        upright: strike > .2 ? .05 : .28,
        bankFn: (x, curv) => corkscrew * strike * (.35 + .65 * x) - curv * 1.2 * p.bank
      });
      return {
        pos, quats,
        head: [.18 * wind - .12 * strike, 0, .25 * Math.sin(corkscrew) * strike],
        jaw: (jawDegrees + 26 * strike + 8 * wind) * PI / 180,
        limbs: (isFront) => {
          // Tuck tightly back during piercing strike, brace on windup
          let tuck = -.68 * strike + .22 * wind;
          return [tuck, .48 * strike - .15 * wind, .15 * strike];
        },
        previewPath: (k) => V(-7.2 + 3.8 * pulse(k,.02,.26,.34,.44) - 9.4 * smoother(clamp((k-.28)/.30,0,1))*(1-smoother(clamp((k-.66)/.34,0,1))), 1.25, 0)
      };
    }
  },

  // 7. THIÊN LONG GIÁNG THẾ — High Arc -> Predatory Dive Impact -> Pull-up Swoop
  dive_strike: {
    name: 'Thiên Long Giáng Thế',
    duration: 3.0,
    loop: false,
    events: [[.45, 'climb_apex'], [1.05, 'dive_commit'], [1.62, 'ground_impact'], [2.35, 'pull_up']],
    solve(u, p, withRoot) {
      const headS = u * 30.0;
      const diveDepth = 1.0 + .35 * p.ampY;
      function arcPath(d) {
        // d in [-12 .. 32]: rises high on +X, swoops down to ground impact near origin, rockets out to -X
        let t = d / 30.0;
        let px = 11.5 - d * .92;
        let py = 2.6 + (4.2 * Math.sin(t * PI * 1.35) - 4.4 * Math.exp(-Math.pow((t - .55) / .17, 2))) * diveDepth;
        let pz = Math.sin(d * .26 * p.freq) * (1.25 * p.ampX) * (1 - .55 * Math.exp(-Math.pow((t - .55) / .18, 2)));
        return V(px, Math.max(-2.15, py), pz);
      }
      const pos = resampleSpineArcLength(s => arcPath(headS - s * .92), 120, total * 1.35);
      if (!withRoot) {
        let c = pos[0];
        for (let i = 0; i < NS; i++) pos[i] = sub(pos[i], V(c[0] + 6.0, c[1] - 1.5, c[2]));
      }
      const impact = pulse(u, .46, .54, .62, .75);
      const quats = solveSpineFrames(pos, {
        upright: .20,
        bankFn: (x, curv) => clamp(-curv * 1.5 * p.bank, -.75, .75)
      });
      return {
        pos, quats,
        head: [.14 * impact, 0, 0],
        jaw: (jawDegrees + 32 * impact) * PI / 180,
        limbs: (isFront) => {
          let reach = isFront ? (.42 * impact - .38 * (1 - impact)) : (-.35);
          return [reach, -.25 * impact + .25 * (1 - impact), .18 * impact];
        },
        previewPath: (k) => arcPath(k * 30.0)
      };
    }
  },

  // 8. LONG TỨC QUÉT NGANG — Elevated Pillar Stance + Wide Sweeping Breath Beam
  breath_sweep: {
    name: 'Long Tức Quét Ngang',
    duration: 3.6,
    loop: false,
    events: [[.45, 'inhale'], [1.00, 'breath_start'], [1.85, 'sweep_center'], [2.75, 'breath_end']],
    solve(u, p) {
      const inhale = pulse(u, .04, .22, .28, .38);
      const breath = pulse(u, .25, .34, .74, .92);
      const sweepT = smoother(clamp((u - .28) / .52, 0, 1));
      const sweepAngle = lerp(-0.92, 0.92, sweepT) * p.ampX * breath;
      const ph = u * TAU;
      const pos = resampleSpineArcLength(s => {
        let x = clamp(s / total, 0, 1);
        let bp = spine(x);
        // Elevate chest/neck into commanding turret, sweep upper third horizontally
        let neckMask = Math.exp(-x * 5.2);
        let chestLift = (2.1 * breath + 1.4 * inhale) * Math.exp(-x * 3.4);
        let recoilX = (1.6 * inhale - .6 * breath) * neckMask;
        let sweepZ = Math.sin(sweepAngle) * 4.4 * neckMask + Math.sin(ph - x * 1.3 * p.freq * TAU) * (.85 * p.ampX) * x;
        let counterY = Math.sin(ph * 1.5 - x * 4.0) * (.45 * p.ampY) * x;
        return V(bp[0] + recoilX, bp[1] + chestLift + counterY, bp[2] + sweepZ);
      }, 104, total * 1.25);
      const quats = solveSpineFrames(pos, {
        upright: .32,
        bankFn: (x) => -sweepAngle * .22 * p.bank * Math.exp(-x * 3)
      });
      return {
        pos, quats,
        head: [-.22 * inhale + .14 * breath, sweepAngle * .52, -sweepAngle * .15],
        jaw: (jawDegrees + 6 * inhale + 38 * breath) * PI / 180,
        limbs: (isFront, side) => {
          let brace = (isFront ? .28 : -.16) * (breath + inhale);
          return [brace + .05 * Math.sin(ph + side), -.18 * breath, .12 * breath];
        }
      };
    }
  },

  // 9. THẦN LONG BÃI VĨ — Anchored Front Torque -> Massive Horizontal Tail Whip
  tail_whip: {
    name: 'Thần Long Bãi Vĩ',
    duration: 2.4,
    loop: false,
    events: [[.30, 'wind_coil'], [.88, 'whip_snap'], [1.18, 'hit_arc'], [1.85, 'follow_through']],
    solve(u, p) {
      const wind = pulse(u, .02, .28, .36, .48);
      const whip = pulse(u, .34, .48, .58, .92);
      const snapWave = smoother(clamp((u - .32) / .40, 0, 1));
      const pos = resampleSpineArcLength(s => {
        let x = clamp(s / total, 0, 1);
        let bp = spine(x);
        // Whip travels down x > 0.25 with huge lateral & sweeping arc amplitude
        let tailMask = smooth((x - .18) / .55);
        let delayed = clamp(snapWave * 1.55 - x * .65, 0, 1);
        let swingAng = (-1.15 * wind + 2.35 * smoother(delayed) * whip) * (.75 + .25 * p.ampX);
        // Pivot the posterior spine around mid-torso anchor (~x=0.25)
        let pivot = spine(.24);
        let relX = bp[0] - pivot[0], relZ = bp[2] - pivot[2];
        let c = Math.cos(swingAng * tailMask), sn = Math.sin(swingAng * tailMask);
        let rx = pivot[0] + relX * c - relZ * sn;
        let rz = pivot[2] + relX * sn + relZ * c;
        let ry = bp[1] + .65 * p.ampY * Math.sin(PI * tailMask) * (whip - .35 * wind);
        return V(rx, ry, rz);
      }, 110, total * 1.32);
      const quats = solveSpineFrames(pos, {
        upright: .26,
        bankFn: (x) => (whip * .75 - wind * .35) * smooth((x - .2) / .6) * p.bank
      });
      return {
        pos, quats,
        head: [.10 * whip, -.32 * wind + .28 * whip, -.18 * whip],
        jaw: (jawDegrees + 20 * whip) * PI / 180,
        limbs: (isFront, side) => [(.22 * whip - .15 * wind) * side, -.18 * whip, .10 * whip]
      };
    }
  },

  // 10. LONG NGÂM CHẤN THIÊN — King-Cobra Rearing Stance + Skyward Roar Shockwave
  rear_roar: {
    name: 'Long Ngâm Chấn Thiên',
    duration: 3.0,
    loop: false,
    events: [[.45, 'rear_up'], [1.05, 'roar_start'], [1.50, 'shockwave_peak'], [2.45, 'roar_end']],
    solve(u, p) {
      const rear = pulse(u, .04, .28, .72, .96);
      const charge = pulse(u, .14, .30, .38, .48);
      const roar = pulse(u, .32, .44, .74, .94);
      const tremor = Math.sin(u * 42.0) * .14 * roar;
      const pos = resampleSpineArcLength(s => {
        let x = clamp(s / total, 0, 1);
        let bp = spine(x);
        let frontMask = Math.exp(-x * 3.8);
        let midCoil = Math.sin(PI * clamp((x - .18) / .65, 0, 1));
        let lift = (3.45 * rear + .55 * roar) * frontMask * (.75 + .25 * p.ampY);
        let pullBack = (2.2 * rear + .9 * charge - .7 * roar) * frontMask;
        let coilWide = (1.65 * rear * p.ampX) * Math.sin(x * 1.45 * p.freq * TAU) * midCoil;
        return V(bp[0] + pullBack, bp[1] + lift - .55 * midCoil * rear, bp[2] + coilWide + tremor * frontMask);
      }, 110, total * 1.32);
      const quats = solveSpineFrames(pos, {
        upright: .30,
        bankFn: (x) => .18 * p.bank * Math.sin(x * TAU) * rear
      });
      return {
        pos, quats,
        head: [.26 * charge - .42 * roar, tremor * .6, tremor * .4],
        jaw: (jawDegrees + 41 * roar + 5 * charge) * PI / 180,
        limbs: (isFront, side) => {
          let flair = isFront ? (.48 * roar - .22 * charge) : (-.18 * rear);
          return [flair, -.30 * roar, .20 * roar * side];
        }
      };
    }
  }
};

// ============================================================================
// RUNTIME RIG STATE, SMOOTH BLENDING & SOCKET / GUIDE VISUALIZER
// ============================================================================
let showBones = false, showGuides = false, restPose = false, jawDegrees = 4;
let clipName = 'idle_float', lastSkill = 'spiral_rise', clipTime = 0, clipSpeed = 1;
let rootMotion = true, followCam = true, loopSkill = false;
let posed = new Array(bones.length).fill(null).map(identity);
let poseArray = new Float32Array(bones.length * 16);
let currentSpinePos = bindSpineFrames.map(f => f.p);
let currentPreviewFn = null;
let dragonCenter = V(0, 0.5, 0);
let eventListeners = new Set(), blendFrom = null, blendTime = 0;
let bannerTimer = 0;

function evaluateCurrentPose(name, time) {
  let spec = clips[name];
  let u = (spec.loop || loopSkill)
    ? (((time % spec.duration) + spec.duration) % spec.duration) / spec.duration
    : clamp(time / spec.duration, 0, 1);
  return spec.solve(u, motionParams, rootMotion);
}

function applyPoseToRig(pose) {
  currentSpinePos = pose.pos;
  currentPreviewFn = pose.previewPath || null;

  // Compute centroid for shadow frustum & follow camera
  let c = V(0, 0, 0);
  for (let idx of [0, 5, 11, 17, 23]) c = add(c, pose.pos[idx]);
  dragonCenter = mul(c, 0.2);

  // 1. Spine bones (0..NS-1): map bind frame -> live curve frame
  for (let i = 0; i < NS; i++) {
    let liveM = quatPosToMat(pose.quats[i], pose.pos[i]);
    let m = restPose ? identity() : mm(liveM, bindSpineInv[i]);
    posed[i] = m;
    poseArray.set(m, i * 16);
  }

  // 2. Head & Jaw (local Euler around anatomical HX/HY/HZ attached to neck_00)
  const [hp, hy, hr] = restPose ? [0, 0, 0] : (pose.head || [0, 0, 0]);
  let headRot = mm(rot(HY, hy), mm(rot(HX, hp), rot(HZ, hr)));
  let localHead = mm(mm(trans(bones[HEAD].p), headRot), trans(mul(bones[HEAD].p, -1)));
  posed[HEAD] = restPose ? identity() : mm(posed[0], localHead);
  poseArray.set(posed[HEAD], HEAD * 16);

  let jAng = restPose ? 0 : (pose.jaw ?? (jawDegrees * PI / 180));
  let localJaw = mm(mm(trans(bones[JAW].p), rot(bones[JAW].axis, jAng)), trans(mul(bones[JAW].p, -1)));
  posed[JAW] = restPose ? identity() : mm(posed[HEAD], localJaw);
  poseArray.set(posed[JAW], JAW * 16);

  // 3. Articulated 4 Limbs (attached to their spine parent bones)
  const limbMap = new Map();
  for (let i = JAW + 1; i < bones.length; i++) {
    let b = bones[i];
    let isFront = b.name.startsWith('fore');
    let side = b.name.includes('_L') ? -1 : 1;
    let key = (isFront ? 'f' : 'h') + side;
    if (!limbMap.has(key)) {
      limbMap.set(key, (!restPose && pose.limbs) ? pose.limbs(isFront, side) : [0, 0, 0]);
    }
    let arr = limbMap.get(key);
    let ang = restPose ? 0 : (b.kind === 'upper' ? arr[0] : b.kind === 'lower' ? arr[1] : arr[2]);
    let local = mm(mm(trans(b.p), rot(b.axis, ang)), trans(mul(b.p, -1)));
    let m = restPose ? identity() : mm(posed[b.parent], local);
    posed[i] = m;
    poseArray.set(m, i * 16);
  }
}

function socket(name) {
  const def = {
    mouth:  [HEAD, H(V(0, -.07, 1.78)), HZ],
    head:   [HEAD, H(V(0, .26, .55)), HZ],
    chest:  [4, F(4 / (NS - 1)).p, mul(F(4 / (NS - 1)).T, -1)],
    tail:   [NS - 1, F(1).p, F(1).T],
    fore_L: [bones.findIndex(b => b.name === 'fore_L_foot'), null, HZ],
    fore_R: [bones.findIndex(b => b.name === 'fore_R_foot'), null, HZ]
  }[name];
  if (!def) throw Error('Unknown socket ' + name);
  let [id, p, dir] = def, m = posed[id] || identity();
  let position = transform(m, p || bones[id].p);
  let direction = norm(sub(transform(m, add(p || bones[id].p, dir)), position));
  return { position, direction };
}


// Export interface: no DOM or WebGL required.
rootMotion = opts.rootMotion;
jawDegrees = opts.jaw;
Object.assign(motionParams, opts.motion);
currentPalette = presetData(opts.variant);
Object.assign(currentPalette, opts.palette || {});
const out = opts.tmp;
function write(name, a) { fs.writeFileSync(out+'/'+name, Buffer.from(a.buffer, a.byteOffset, a.byteLength)); }
write('vertices.bin', new Float32Array(verts));
write('indices.bin', new Uint32Array(inds));
write('skin.bin', new Float32Array(skinData));
write('style.bin', new Float32Array(styleData));
// Bind world frames. All are right-handed; local +Y is a useful limb axis.
const bind = bones.map((b,i) => {
  if(i < NS) {const f=bindSpineFrames[i]; return quatPosToMat(frameToQuat(f.T,f.U,f.R),b.p);}
  if(i===HEAD || i===JAW) return quatPosToMat(frameToQuat(HX,HY,HZ),b.p);
  let child=bones.find(x=>x.parent===i);
  let Y=norm(child?sub(child.p,b.p):sub(b.p,bones[b.parent].p));
  let X=norm(sub(b.axis,mul(Y,dot(b.axis,Y))));
  if(len(X)<.01) X=norm(cross(Y, Math.abs(Y[1])<.9?V(0,1,0):V(0,0,1)));
  let Z=norm(cross(X,Y)); X=norm(cross(Y,Z));
  return quatPosToMat(frameToQuat(X,Y,Z),b.p);
});
const socketNames=['mouth','head','chest','tail','fore_L','fore_R'];
const socketBones=[HEAD,HEAD,4,NS-1,bones.findIndex(b=>b.name==='fore_L_foot'),bones.findIndex(b=>b.name==='fore_R_foot')];
const sockets=socketNames.map((name,i)=>({name,bone:socketBones[i],...socket(name)}));
const meta={bones,bind,roles,palette:currentPalette,sockets,total,clips:[]};
let clipsToExport=opts.clips.length?opts.clips:Object.keys(clips);
for(const name of clipsToExport) {
  const spec=clips[name]; if(!spec) throw Error('Unknown clip '+name);
  const n=Math.ceil(spec.duration*opts.fps), data=new Float32Array((n+1)*bones.length*16);
  for(let i=0;i<=n;i++) {
    const u=spec.loop && i===n?0:i/n;
    applyPoseToRig(spec.solve(u,motionParams,rootMotion));
    data.set(poseArray,i*bones.length*16);
  }
  write(name+'.bin',data);
  meta.clips.push({id:name,name:spec.name,duration:spec.duration,loop:spec.loop,events:spec.events.map(([time,event])=>({time,event})),samples:n+1});
}
fs.writeFileSync(out+'/meta.json',JSON.stringify(meta));

"""

# ============================================================================
# PYTHON GLTF EXPORTER
# ============================================================================

def normalize(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-15)


def smoothstep(a,b,x):
    x=np.clip((x-a)/(b-a),0,1)
    return x*x*(3-2*x)


def lerp(a,b,t):
    return a+(b-a)*t


def rgb(s):
    return (np.array([int(s.lstrip('#')[i:i+2],16) for i in (0,2,4)])/255.)**2.2


def quat(m):
    """Convert a proper rotation matrix to glTF xyzw quaternion."""
    r=m[:3,:3]; tr=np.trace(r)
    if tr>0:
        s=2*math.sqrt(max(0,tr+1)); q=[(r[2,1]-r[1,2])/s,(r[0,2]-r[2,0])/s,(r[1,0]-r[0,1])/s,s/4]
    else:
        i=int(np.argmax(np.diag(r))); j=(i+1)%3; k=(i+2)%3
        s=2*math.sqrt(max(0,1+r[i,i]-r[j,j]-r[k,k])); q=[0.,0.,0.,0.]
        q[i]=s/4; q[j]=(r[j,i]+r[i,j])/s; q[k]=(r[k,i]+r[i,k])/s; q[3]=(r[k,j]-r[j,k])/s
    return normalize(np.array(q))


def mat_from_quat(q,p):
    x,y,z,w=q; m=np.eye(4)
    m[:3,:3]=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
               [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
               [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
    m[:3,3]=p; return m


def decompose(m):
    return m[:3,3].copy(),quat(m)


class GLB:
    def __init__(self):
        self.bin=bytearray()
        self.doc={"asset":{"version":"2.0","generator":"Dragon V12 Python exporter"},
                  "scene":0,"scenes":[{"name":"Dragon","nodes":[]}],
                  "nodes":[],"meshes":[],"skins":[],"materials":[],"animations":[],
                  "accessors":[],"bufferViews":[],"buffers":[{"byteLength":0}]}

    def accessor(self, data, kind, component=5126, target=None, bounds=False):
        dtype={5126:'<f4',5123:'<u2',5125:'<u4'}[component]
        width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[kind]
        a=np.ascontiguousarray(data,dtype=dtype).reshape(-1,width)
        assert len(a)>0 and np.isfinite(a).all()
        self.bin.extend(b'\0'*(-len(self.bin)%4)); offset=len(self.bin)
        self.bin.extend(a.tobytes()); view={'buffer':0,'byteOffset':offset,'byteLength':a.nbytes}
        if target: view['target']=target
        vi=len(self.doc['bufferViews']); self.doc['bufferViews'].append(view)
        ac={'bufferView':vi,'componentType':component,'count':len(a),'type':kind}
        if bounds: ac.update(min=a.min(axis=0).tolist(),max=a.max(axis=0).tolist())
        ai=len(self.doc['accessors']); self.doc['accessors'].append(ac); return ai

    def write(self,path):
        self.bin.extend(b'\0'*(-len(self.bin)%4)); self.doc['buffers'][0]['byteLength']=len(self.bin)
        j=json.dumps(self.doc,ensure_ascii=False,separators=(',',':'),allow_nan=False).encode('utf-8'); j+=b' '*(-len(j)%4)
        path=Path(path); path.parent.mkdir(parents=True,exist_ok=True)
        with path.open('wb') as f:
            f.write(struct.pack('<4sII',b'glTF',2,12+8+len(j)+8+len(self.bin)))
            f.write(struct.pack('<I4s',len(j),b'JSON')); f.write(j)
            f.write(struct.pack('<I4s',len(self.bin),b'BIN\0')); f.write(self.bin)


def bake_colors(v, style, meta):
    """Port the view-independent section of the V12 fragment shader."""
    palette=np.array([rgb(meta['palette'][r]['color']) for r in meta['roles']])
    finish=np.array([[meta['palette'][r]['metallic'],meta['palette'][r]['roughness']] for r in meta['roles']])
    slot=style[:,0].astype(int); st=style[:,1:2]; ex=style[:,2:4]
    color=v[:,6:9].astype(float).copy(); mr=v[:,9:11].astype(float).copy()
    for role in range(len(palette)):
        sel=slot==role
        if not sel.any(): continue
        x=st[sel]; e=ex[sel]; tint=np.broadcast_to(palette[role],(len(x),3)).copy(); f=np.broadcast_to(finish[role],(len(x),2)).copy()
        if role==10:
            tint=lerp(tint,palette[11],x); f=lerp(f,finish[11],x)
        elif role==6:
            w=np.clip(x,0,1); tint=lerp(tint,palette[7],w); f=lerp(f,finish[7],w)
        elif role==3:
            hs=np.clip(np.where(x>1.5,x-2,x),0,1)
            shaft=lerp(palette[3],palette[2]*1.15,.42)
            h=lerp(palette[4]*.62,shaft,smoothstep(0,.62,hs))
            h=lerp(h,palette[6],smoothstep(.62,1,hs)*.70)
            tint=np.where(x>.001,h,tint)
            f[:,1:2]=np.where(x>.001,lerp(f[:,1:2]+.14,f[:,1:2]-.08,smoothstep(.15,1,hs)),f[:,1:2])
        elif role==2:
            sv=np.clip((x-.05)/.95,0,1); ux=np.abs(2*e[:,0:1]-1); dors=e[:,1:2]
            gild=palette[11]*1.10+tint*.12
            h=lerp(tint,palette[10]*1.55,dors*dors*.30)
            h=lerp(h,palette[5]*.92,(1-dors)**2.2*.26)
            h=lerp(h,gild,smoothstep(.48,1,sv)*(.10+.13*dors))
            h*=1+.045*np.exp(-ux*ux*14)*smoothstep(.1,.8,sv)
            tint=np.where(x>.04,h,tint); f[:,1:2]-=np.where(x>.04,.025*sv,0)
        color[sel]=tint*v[sel,6:7]; mr[sel]=f
    return np.clip(color,0,1),np.clip(mr,0,1),slot


def export_glb(args, folder):
    folder=Path(folder); meta=json.loads((folder/'meta.json').read_text())
    v=np.fromfile(folder/'vertices.bin','<f4').reshape(-1,12)
    tri=np.fromfile(folder/'indices.bin','<u4').reshape(-1,3)
    sk=np.fromfile(folder/'skin.bin','<f4').reshape(-1,4)
    style=np.fromfile(folder/'style.bin','<f4').reshape(-1,4)
    bones=meta['bones']; nb=len(bones); scale=args.scale
    assert len(v)==len(sk)==len(style) and tri.max()<len(v)
    assert np.isfinite(v).all() and np.isfinite(sk).all() and np.isfinite(style).all()
    assert (sk[:,:2]>=0).all() and (sk[:,:2]<nb).all() and (sk[:,2:]>=0).all()
    assert np.allclose(sk[:,2:].sum(1),1,atol=1e-5)
    # Repair zero normals at collapsed parametric poles, and remove degenerate faces.
    pos=v[:,:3].astype(float)*scale; normals=v[:,3:6].astype(float).copy()
    f=np.cross(pos[tri[:,1]]-pos[tri[:,0]],pos[tri[:,2]]-pos[tri[:,0]])
    keep=np.linalg.norm(f,axis=1)>1e-14*scale*scale; tri=tri[keep].copy(); f=f[keep]
    flip=(f*normals[tri].sum(1)).sum(1)<0
    tri[flip]=tri[flip][:,[0,2,1]]; f[flip]*=-1
    missing=np.linalg.norm(normals,axis=1)<1e-8
    if missing.any():
        acc=np.zeros_like(normals)
        for k in range(3): np.add.at(acc,tri[:,k],f)
        normals[missing]=acc[missing]
        normals[np.linalg.norm(normals,axis=1)<1e-8]=[0,1,0]
    normals=normalize(normals)
    color,mr,slot=bake_colors(v,style,meta)
    joints=np.zeros((len(v),4),dtype=np.uint16); joints[:,:2]=sk[:,:2].astype(np.uint16)
    weights=np.zeros((len(v),4),dtype=np.float32); weights[:,:2]=sk[:,2:]; weights/=weights.sum(1,keepdims=True)
    # Merge duplicate joint indices for rigid points, leaving zero weights unused.
    duplicate=joints[:,0]==joints[:,1]; weights[duplicate,0]+=weights[duplicate,1]; weights[duplicate,1]=0
    joints[weights==0]=0
    g=GLB(); d=g.doc
    # A GLTF primitive is homogeneous in material. Retain source semantic slots.
    groups={}
    for i,k in enumerate(slot[tri[:,0]]):
        a=tri[i,0]
        if k>=0: key=(int(k),)
        else: key=(-1,*[round(float(x),4) for x in v[a,9:12]])
        groups.setdefault(key,[]).append(i)
    fixed_names={(0.,.78,0.):"mouth",(.02,.42,0.):"tongue",(0.,.10,0.):"pupil",(.04,.30,0.):"tooth",(0.,.06,1.45):"glint"}
    primitives=[]
    for key, face_ids in groups.items():
        subtri=tri[face_ids]; used, remap=np.unique(subtri,return_inverse=True)
        base=color[used]; factor=np.maximum(base.max(0),1e-6); vc=base/factor
        m,r=mr[used].mean(0); emission=float(v[used,11].mean()); k=key[0]
        if k>=0: name=meta['roles'][k]
        else:
            name=fixed_names.get(key[1:],'detail_'+str(len(d['materials'])))
        material={'name':name,'doubleSided':True,'pbrMetallicRoughness':{'baseColorFactor':[*factor.tolist(),1.0],'metallicFactor':float(m),'roughnessFactor':float(r)}}
        if emission>0: material['emissiveFactor']=np.clip(base.mean(0)*emission*(1.15 if k==9 else 1),0,1).tolist()
        mi=len(d['materials']); d['materials'].append(material)
        attr={'POSITION':g.accessor(pos[used],'VEC3',target=34962,bounds=True),
              'NORMAL':g.accessor(normals[used],'VEC3',target=34962),
              'COLOR_0':g.accessor(vc,'VEC3',target=34962),
              'JOINTS_0':g.accessor(joints[used],'VEC4',5123,34962),
              'WEIGHTS_0':g.accessor(weights[used],'VEC4',target=34962)}
        ia=g.accessor(remap.reshape(-1),'SCALAR',5123 if len(used)<=65536 else 5125,34963)
        primitives.append({'attributes':attr,'indices':ia,'material':mi,'mode':4})
    d['meshes']=[{'name':'Dragon_V12','primitives':primitives}]
    d['nodes']=[{'name':'Dragon_Armature','children':[1]}, {'name':'Dragon_Mesh','mesh':0,'skin':0}]
    d['scenes'][0]['nodes']=[0]; off=2
    bind=np.asarray(meta['bind'],dtype=float).reshape(nb,4,4).transpose(0,2,1).copy()
    bind[:,:3,3]*=scale
    inv=np.linalg.inv(bind)
    parents=[b['parent'] for b in bones]
    for i,b in enumerate(bones):
        p=parents[i]; local=bind[i] if p<0 else inv[p]@bind[i]
        t,q=decompose(local); node={'name':b['name'],'translation':t.tolist(),'rotation':q.tolist(),'extras':{'kind':b['kind']}}
        d['nodes'].append(node)
        parent_node=0 if p<0 else off+p
        d['nodes'][parent_node].setdefault('children',[]).append(off+i)
    ibm=g.accessor(inv.transpose(0,2,1),'MAT4')
    d['skins']=[{'name':'Dragon_Skeleton','joints':list(range(off,off+nb)),'inverseBindMatrices':ibm,'skeleton':off}]
    for sock in meta['sockets']:
        bone=sock['bone']; z=normalize(np.array(sock['direction'])); up=np.array([0,1,0] if abs(z[1])<.95 else [1,0,0]); x=normalize(np.cross(up,z)); y=np.cross(z,x)
        m=np.eye(4); m[:3,:3]=np.column_stack([x,y,z]); m[:3,3]=np.asarray(sock['position'])*scale
        t,q=decompose(inv[bone]@m); idx=len(d['nodes']); d['nodes'].append({'name':'socket_'+sock['name'],'translation':t.tolist(),'rotation':q.tolist(),'extras':{'forwardAxis':'+Z'}}); d['nodes'][off+bone].setdefault('children',[]).append(idx)
    max_error=0.
    for clip in meta['clips']:
        n=clip['samples']; source=np.fromfile(folder/(clip['id']+'.bin'),'<f4').reshape(n,nb,4,4).transpose(0,1,3,2).astype(float)
        source[:,:,:3,3]*=scale
        world=source@bind[None]
        local=world.copy()
        for i,p in enumerate(parents):
            if p>=0: local[:,i]=np.linalg.inv(world[:,p])@world[:,i]
        assert np.isfinite(local).all() and np.allclose(np.linalg.det(local[:,:,:3,:3]),1,atol=2e-5)
        times=np.linspace(0,clip['duration'],n,dtype=np.float32); ti=g.accessor(times,'SCALAR',bounds=True)
        anim={'name':clip['id'],'samplers':[],'channels':[],'extras':{'displayName':clip['name'],'loop':clip['loop'],'events':clip['events'],'rootMotion':not args.in_place,'sampleRate':(n-1)/clip['duration']}}
        translations=local[:,:,:3,3]; rotations=np.empty((n,nb,4))
        for i in range(nb):
            for f in range(n):
                q=quat(local[f,i]); rotations[f,i]= -q if f and np.dot(q,rotations[f-1,i])<0 else q
            for path,values,kind in [('translation',translations[:,i],'VEC3'),('rotation',rotations[:,i],'VEC4')]:
                # Rigid limb/head translations are constant; one key is sufficient only if equal to bind.
                default=d['nodes'][off+i][path]
                if path=='translation' and np.max(np.abs(values-default))<1e-6*max(scale,1): continue
                ai=g.accessor(values,kind); si=len(anim['samplers']); anim['samplers'].append({'input':ti,'output':ai,'interpolation':'LINEAR'}); anim['channels'].append({'sampler':si,'target':{'node':off+i,'path':path}})
        # Reconstruct the hierarchical skin matrices from exported TRS at EVERY sample.
        for f in range(n):
            check=[]
            for i,p in enumerate(parents):
                m=mat_from_quat(rotations[f,i].astype(np.float32),translations[f,i].astype(np.float32)); check.append(m if p<0 else check[p]@m)
            max_error=max(max_error,float(np.max(np.abs(np.array(check)@inv-source[f]))))
        d['animations'].append(anim)
    assert max_error<.0002*max(1,scale),max_error
    d['asset']['extras']={'source':'Asian Dragon WebGL V12','variant':args.variant,'quality':args.quality,'metersPerSourceUnit':scale,'sourceTriangles':int(len(v) and np.fromfile(folder/'indices.bin','<u4').size//3),'exportTriangles':len(tri),'sourceVertices':len(v),'bones':nb,'sourceArcLength':meta['total'],'palette':meta['palette'],'maxSkinMatrixError':max_error,'notes':'PBR vertex colors; custom WebGL shaders and VFX not exported. Loop flags and skill events are extras.'}
    g.write(args.output)
    print(f'{args.output}: {len(v):,} source vertices, {len(tri):,} triangles, {nb} joints, {len(d["animations"])} animations, {len(primitives)} primitives. Max skin matrix error: {max_error:.3g}')
    for c in meta['clips']: print(f'  {c["id"]:16s} {c["duration"]:4.1f}s  {c["samples"]:4d} keys  {"loop" if c["loop"] else "one-shot"}')


def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output','-o',default='dragon_v12.glb'); p.add_argument('--quality',choices=['mobile','game','hero'],default='game'); p.add_argument('--variant',choices=['gold','fire','jade','white','black'],default='gold')
    p.add_argument('--fps',type=int,default=30); p.add_argument('--scale',type=float,default=1.0); p.add_argument('--in-place',action='store_true',help='Disable root motion in the original solver where supported')
    p.add_argument('--clips',default='',help='Comma-separated clip IDs; default all 10'); p.add_argument('--jaw',type=float,default=4); p.add_argument('--amp-x',type=float,default=1); p.add_argument('--amp-y',type=float,default=1); p.add_argument('--freq',type=float,default=1); p.add_argument('--bank',type=float,default=1)
    p.add_argument('--palette',default='{}',help='JSON mapping material role to {color, metallic, roughness}; complete override per role'); args=p.parse_args()
    if not (1<=args.fps<=120 and math.isfinite(args.scale) and args.scale>0): p.error('fps must be 1..120 and scale > 0')
    node=shutil.which('node') or shutil.which('nodejs');
    if not node: p.error('Node.js is required for the embedded V12 procedural source; install Node.js first.')
    with tempfile.TemporaryDirectory(prefix='dragon_v12_') as tmp:
        opts={'tmp':tmp,'quality':args.quality,'variant':args.variant,'fps':args.fps,'rootMotion':not args.in_place,'jaw':args.jaw,'motion':{'ampX':args.amp_x,'ampY':args.amp_y,'freq':args.freq,'bank':args.bank},'clips':[s.strip() for s in args.clips.split(',') if s.strip()],'palette':json.loads(args.palette)}
        env=dict(os.environ,DRAGON_OPTIONS=json.dumps(opts)); subprocess.run([node,'-'],input=SOURCE_JS,text=True,env=env,check=True)
        export_glb(args,tmp)

if __name__=='__main__': main()
