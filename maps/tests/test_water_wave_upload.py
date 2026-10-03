#!/usr/bin/env python3
"""Execute production wake-upload initialization, synchronization and cleanup."""
import argparse
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r"^(?:static|void)[^\n]*\b" + name + r"\(", source, re.M)
    if not match:
        raise ValueError("Production function missing: " + name)
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


STUBS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define WATER_FIELD_SIZE 128
#define PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 7
#define TEXTURE_FILTER_BILINEAR 1
#define TEXTURE_WRAP_CLAMP 1
#define WAVE_BYTES (WATER_FIELD_SIZE * WATER_FIELD_SIZE * 4u)
typedef struct {unsigned int id;} Texture2D;
typedef struct {int meshCount;} Model;
typedef struct {void *data;int width,height,mipmaps,format;} Image;
typedef struct {
    Model waterModel,bankModel,bedModel;
    float *waveHeightField,*waveNextField,*waveVelocityField;
    unsigned char *wavePixels;
    Texture2D waveFieldTex;
    bool ready;
    unsigned char *waveUploadedPixels;
    unsigned int waveUploadedTextureId;
} MapWaterSurface;
static unsigned char gpuPixels[WAVE_BYTES];
static int uploads,liveCaches;
static bool failCache,failTexture;
static void *MemAlloc(unsigned int bytes) {
    assert(bytes==WAVE_BYTES);
    if(failCache) return NULL;
    void *p=malloc(bytes);if(p)liveCaches++;return p;
}
static void MemFree(void *p) {if(p){assert(liveCaches>0);liveCaches--;free(p);}}
static Texture2D LoadTextureFromImage(Image image) {
    assert(image.width==WATER_FIELD_SIZE && image.height==WATER_FIELD_SIZE);
    assert(image.mipmaps==1 && image.format==PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if(failTexture) return (Texture2D){0};
    memcpy(gpuPixels,image.data,WAVE_BYTES);return (Texture2D){7};
}
static void SetTextureFilter(Texture2D texture,int filter) {assert(texture.id && filter==TEXTURE_FILTER_BILINEAR);}
static void SetTextureWrap(Texture2D texture,int wrap) {assert(texture.id && wrap==TEXTURE_WRAP_CLAMP);}
static void UpdateTexture(Texture2D texture,const void *data) {
    assert(texture.id && data);uploads++;memcpy(gpuPixels,data,WAVE_BYTES);
}
static void UnloadTexture(Texture2D texture) {assert(texture.id);}
static void UnloadModel(Model model) {(void)model;}
"""

MAIN = r"""
int main(void) {
    MapWaterSurface water=CreateWake();
    assert(water.ready && water.waveUploadedPixels && liveCaches==1);
    assert(water.waveUploadedTextureId==water.waveFieldTex.id);
    Water_UploadWaveField(&water);assert(uploads==0);
    Water_UploadWaveField(&water);assert(uploads==0);
    water.wavePixels[WAVE_BYTES-1]=127;
    Water_UploadWaveField(&water);assert(uploads==1);
    assert(memcmp(gpuPixels,water.wavePixels,WAVE_BYTES)==0);
    Water_UploadWaveField(&water);assert(uploads==1);
    /* Every change during settling is retained, including return to prior bytes. */
    memset(water.wavePixels,127,WAVE_BYTES);
    Water_UploadWaveField(&water);assert(uploads==2);
    memset(water.wavePixels,128,WAVE_BYTES);
    Water_UploadWaveField(&water);assert(uploads==3);
    Water_UploadWaveField(&water);assert(uploads==3);
    water.waveFieldTex.id=9;
    Water_UploadWaveField(&water);assert(uploads==4);
    assert(water.waveUploadedTextureId==9);
    water.waveFieldTex.id=0;
    Water_UploadWaveField(&water);assert(uploads==4);
    water.waveFieldTex.id=9;
    unsigned char *pixels=water.wavePixels;water.wavePixels=NULL;
    Water_UploadWaveField(&water);assert(uploads==4);water.wavePixels=pixels;
    MapProp_UnloadWaterSurface(&water);
    assert(!water.ready && !water.waveUploadedPixels && !water.waveUploadedTextureId && liveCaches==0);
    MapProp_UnloadWaterSurface(&water);assert(liveCaches==0);
    water=CreateWake();Water_UploadWaveField(&water);assert(uploads==4);
    water.wavePixels[0]=126;Water_UploadWaveField(&water);assert(uploads==5);
    assert(gpuPixels[0]==126);MapProp_UnloadWaterSurface(&water);
    /* Allocation failure preserves the previous always-upload behavior. */
    failCache=true;water=CreateWake();assert(!water.waveUploadedPixels);
    Water_UploadWaveField(&water);Water_UploadWaveField(&water);assert(uploads==7);
    MapProp_UnloadWaterSurface(&water);failCache=false;assert(liveCaches==0);
    failTexture=true;water=CreateWake();assert(!water.waveFieldTex.id && !water.waveUploadedPixels);
    Water_UploadWaveField(&water);assert(uploads==7);MapProp_UnloadWaterSurface(&water);
    assert(liveCaches==0);
    puts("water upload: initial data, idle reuse, mutations, settling, texture identity, fallback and unload/reinit passed");
}
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path, default=ROOT / "maps/toolkit/map_props_nature.inl")
    args = parser.parse_args()
    source = args.source.read_text()
    init = function(source, "Water_InitializeWaveUploadCache")
    upload = function(source, "Water_UploadWaveField")
    destroy = function(source, "MapProp_UnloadWaterSurface")
    create = source[source.index("MapWaterSurface MapProp_CreateWaterSurface("):]
    begin = create.index("    const int cellCount = WATER_FIELD_SIZE * WATER_FIELD_SIZE;")
    end = create.index("    return water;", begin) + len("    return water;")
    create = "static MapWaterSurface CreateWake(void)\n{\n    MapWaterSurface water={0};\n" + create[begin:end] + "\n}\n"
    draw = function(source, "MapProp_DrawWaterBed")
    assert "Water_UploadWaveField((MapWaterSurface *)water);" in draw
    with tempfile.TemporaryDirectory(prefix="wuxing-water-upload-") as directory:
        work = pathlib.Path(directory)
        def run(body, name):
            path = work / (name + ".c")
            binary = work / name
            path.write_text(STUBS + init + body + create + destroy + MAIN)
            subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(binary)], check=True)
            return subprocess.run([str(binary)], capture_output=True, text=True)
        result = run(upload, "water")
        if result.returncode:
            raise RuntimeError(result.stderr)
        print(result.stdout.strip())
        unconditional = "static void Water_UploadWaveField(MapWaterSurface *water)\n{\nif(water->waveFieldTex.id && water->wavePixels) UpdateTexture(water->waveFieldTex,water->wavePixels);\n}\n"
        result = run(unconditional, "former_upload")
        assert result.returncode != 0, "Test failed to reject unconditional wake upload"
        print("water upload: former unconditional-upload behavior rejected")


if __name__ == "__main__":
    main()

