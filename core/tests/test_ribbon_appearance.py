#!/usr/bin/env python3
"""Behavioral checks for copied ribbon appearance validation and shared width/layers.

Mock raylib types only; this does not prove device shader execution or appearance.
"""
import pathlib
import re
import subprocess
import tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
SOURCE=(ROOT/'core/trails/trail_system.c').read_text()
def extract(name):
    m=re.search(r'^(?:static )?(?:inline )?(?:float|bool) '+name+r'\(',SOURCE,re.M)
    a=SOURCE.index('{',m.start());b=a+1;depth=1
    while depth:
        depth+=(SOURCE[b]=='{')-(SOURCE[b]=='}');b+=1
    return SOURCE[m.start():b]
STUBS=r'''
#include "raylib.h"
typedef struct {int placeholder;} Material;
typedef int BlendMode;
typedef struct {int placeholder;} Mesh;
typedef struct {int meshCount;} Model;
typedef struct {Vector3 position,target,up;float fovy;int projection;} Camera3D;
enum {BLEND_ALPHA,BLEND_ADDITIVE,BLEND_ALPHA_PREMULTIPLY};
#include "core/trails/trail_ribbon.h"
#include "core/trails/trail_recipe.h"
#include <assert.h>
#include <stdio.h>
static float SmoothStepC(float a,float b,float x){float t=fminf(1,fmaxf(0,(x-a)/(b-a)));return t*t*(3-2*t);}
float SkillCurve_Eval(const SkillCurve *c,float t){(void)c;return t*.5f;}
static bool TrailUsesAdditiveBlend(const TrailEntity *t){return t->blendMode!=BLEND_ALPHA;}
'''
MAIN=r'''
int main(void){
 TrailRibbonAppearance a={0};assert(TrailRibbonAppearance_IsValid(&a));
 a.enabled=true;a.layerCount=3;assert(TrailRibbonAppearance_IsValid(&a));
 a.layerCount=4;assert(!TrailRibbonAppearance_IsValid(&a));
 a.layerCount=-1;assert(!TrailRibbonAppearance_IsValid(&a));a.layerCount=3;
 a.layers[2].widthMul=NAN;assert(!TrailRibbonAppearance_IsValid(&a));a.layers[2].widthMul=1;
 a.layers[1].headAlphaPow=INFINITY;assert(!TrailRibbonAppearance_IsValid(&a));a.layers[1].headAlphaPow=0;
 a.material.hdrGain=NAN;assert(!TrailRibbonAppearance_IsValid(&a));a.material.hdrGain=1;
 a.blendMode=99;assert(!TrailRibbonAppearance_IsValid(&a));a.blendMode=BLEND_ALPHA_PREMULTIPLY;
 a.ribbonMode=99;assert(!TrailRibbonAppearance_IsValid(&a));a.ribbonMode=RIBBON_FIXED_NORMAL;
 a.fixedNormal.x=NAN;assert(!TrailRibbonAppearance_IsValid(&a));a.fixedNormal=(Vector3){0,1,0};
 assert(TrailRibbonAppearance_IsValid(&a));
 TrailRibbonConfig first={.appearance=a},copy=first;first.appearance.layers[0].alphaMul=.1f;
 assert(copy.appearance.layers[0].alphaMul==0);
 assert(MOTION_RIBBON_ENERGY_SILK==TRAIL_PRESET_ENERGY);
 assert(MOTION_RIBBON_SMOKE_WISP==TRAIL_PRESET_SMOKE);
 assert(MOTION_RIBBON_EMBER_FILAMENT==TRAIL_PRESET_BLADE);
 assert(MOTION_RIBBON_WATER_STREAM==TRAIL_PRESET_WATER);
 TrailEntity t={0};t.widthEnvelope=TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN;
 assert(fabsf(TrailRibbon_WidthEnvelope(&t,1,0)-.15f)<1e-6);
 assert(fabsf(TrailRibbon_WidthEnvelope(&t,0,0)-1)<1e-6);
 t.widthEnvelope=TRAIL_WIDTH_ENVELOPE_ENERGY_BLADE;
 assert(TrailRibbon_WidthEnvelope(&t,.86f,0)>TrailRibbon_WidthEnvelope(&t,0,0));
 SkillCurve curve={0};t.widthCurve=&curve;assert(TrailRibbon_WidthEnvelope(&t,.6f,0)==.3f);
 TrailLayer layer={.alphaMul=.2f};t.blendMode=BLEND_ALPHA_PREMULTIPLY;t.material.bodyOpacity=.9f;
 assert(TrailRibbon_LayerAlpha(&t,&layer,0)==.9f);
 assert(TrailRibbon_LayerAlpha(&t,&layer,1)==.2f);
 t.blendMode=BLEND_ALPHA;assert(TrailRibbon_LayerAlpha(&t,&layer,0)==.2f);
 puts("Ribbon appearance: copied layers, validation, preset vocabulary, shared width and coverage PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='wuxing-ribbon-appearance-') as temp:
    p=pathlib.Path(temp)
    (p/'test.c').write_text(STUBS+'\n'.join(extract(x) for x in ('ComputeWidthEnvelopeFast','TrailRibbon_WidthEnvelope','TrailRibbon_LayerAlpha'))+MAIN)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-I'+str(ROOT),'-I'+str(ROOT/'core/tests/stubs'),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
# Actual shader link/dispatch remain a device-tier responsibility.
for shader in ('trail_deform.vs','trail_ribbon_vertex.glsl'):
    source=(ROOT/'core/trails/shaders'/shader).read_text()
    assert 'out float vPathMetres;' in source and 'out vec4 vColor;' in source and 'out vec2 vSegUV;' in source
fragment=(ROOT/'core/trails/shaders/trail_deform.fs').read_text()
assert 'in float vPathMetres;' in fragment and 'u_nodeArc!=0?vPathMetres:' in fragment

plain=(ROOT/'core/trails/shaders/trail_ribbon_gpu.vs').read_text()
rich=(ROOT/'core/trails/shaders/trail_ribbon_material_gpu.vs').read_text()
assert '#define TRAIL_RIBBON_STYLED 0' in plain
assert '#define TRAIL_RIBBON_STYLED 1' in rich
assert 'trail_ribbon_vertex.glsl' in plain and 'trail_ribbon_vertex.glsl' in rich
# Device captures verify linking; these guards keep Plain's node arrays compiled out.
