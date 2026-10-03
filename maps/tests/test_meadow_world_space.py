#!/usr/bin/env python3
"""Execute production chunk uploads and evaluate its GLSL world expressions."""
import argparse
import math
import pathlib
import re
import subprocess
import tempfile

from test_meadow_parametric import function, typedef

ROOT = pathlib.Path(__file__).resolve().parents[2]

STUBS = r'''
#include "maps/toolkit/map_props.h"
#define RAYMATH_STATIC_INLINE
#include "raymath.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
static int s_natureParameterLoc[2],s_natureBladeOffsetLoc[2],s_natureBladeCountLoc[2];
static int s_natureTuftLodLevelLoc,s_natureCanonicalLoc[2],s_natureCanonicalBladesLoc[2];
static int s_natureGeometryLodLoc[2],s_natureTuftOffsetLoc[2],s_natureCompactLoc[2];
static int s_natureVisibleOffsetLoc,s_natureVisibleIdsLoc;
static int s_natureWorldOffsetLoc[2]={30,31};
static Matrix currentTransform,uploadedMvp;
static Vector3 expectedOffset;
static int expectedPass,expectedInstances,offsetUploads,draws,activeShader;
static Matrix rlGetMatrixTransform(void) {return currentTransform;}
static Matrix rlGetMatrixModelview(void) {return MatrixRotateX(.2f);}
static Matrix rlGetMatrixProjection(void) {return MatrixPerspective(.75,1.7,.1,300);}
void SetShaderValueMatrix(Shader shader,int location,Matrix value) {
    assert(activeShader==(int)shader.id);
    if(location==SHADER_LOC_MATRIX_MVP)uploadedMvp=value;
}
void SetShaderValue(Shader shader,int location,const void *value,int type) {
    assert(activeShader==(int)shader.id);
    if(location==s_natureWorldOffsetLoc[expectedPass]) {
        assert(type==SHADER_UNIFORM_VEC3);
        assert(Vector3Distance(*(const Vector3 *)value,expectedOffset)<.000001f);
        offsetUploads++;
    }
}
static void rlActiveTextureSlot(int slot) {(void)slot;}
static void rlEnableTexture(unsigned int id) {(void)id;}
static bool rlEnableVertexArray(unsigned int id) {assert(id==17);return true;}
static void rlDisableVertexArray(void) {}
static void rlDrawVertexArrayElementsInstanced(int start,int count,const void *offset,int instances) {
    assert(start==0 && count==9 && offset==NULL && instances==expectedInstances);
    assert(offsetUploads==draws+1);draws++;
}
'''

MAIN = r'''
int main(void) {
    int locs[SHADER_LOC_MAP_BRDF+1]={0};
    locs[SHADER_LOC_MATRIX_MVP]=SHADER_LOC_MATRIX_MVP;
    locs[SHADER_LOC_MATRIX_MODEL]=-1;
    Shader shader={.id=42,.locs=locs};activeShader=42;
    NatureTuftRange range={.count=11,.visibleCount={7,5,3}};
    NatureParametricMeadow data={.ranges=&range,.canonical=true};
    for(int lod=0;lod<4;lod++)data.templates[lod]=(NatureTuftTemplate){.vao=17,.indexCount=9,.blades=3};
    MapMeadowSurface meadow={.parametric=&data};
    for(int camera=0;camera<2;camera++) {
        currentTransform=MatrixMultiply(MatrixRotateXYZ((Vector3){.18f+camera*.4f,.3f-camera*.8f,.12f}),
            MatrixTranslate(20-camera*31,-9+camera*5,60-camera*100));
        expectedOffset=camera ? (Vector3){-11,2.5f,7} : (Vector3){0};
        for(int compact=0;compact<2;compact++)for(int lod=0;lod<4;lod++) {
            data.compact=compact!=0;expectedPass=lod==3;
            expectedInstances=compact && !expectedPass ? range.visibleCount[lod] : range.count;
            NatureParametric_DrawChunk(&meadow,0,lod,shader,expectedOffset);
            Matrix model=MatrixMultiply(MatrixTranslate(expectedOffset.x,expectedOffset.y,expectedOffset.z),currentTransform);
            Matrix oracle=MatrixMultiply(MatrixMultiply(model,rlGetMatrixModelview()),rlGetMatrixProjection());
            const float *actual=(const float *)&uploadedMvp,*expected=(const float *)&oracle;
            for(int i=0;i<16;i++)assert(actual[i]==expected[i]);
        }
    }
    assert(draws==16 && offsetUploads==16);
    puts("PASS: active visible/shadow chunk uploads retain MVP, instance counts and world offsets");
}
'''


class Vector(tuple):
    def __add__(self, other):
        return Vector(a + b for a, b in zip(self, other))


class Matrix:
    def __init__(self, values):
        self.values = values

    def __mul__(self, other):
        if isinstance(other, Matrix):
            return Matrix([[sum(self.values[i][k] * other.values[k][j] for k in range(len(other.values)))
                            for j in range(len(other.values[0]))] for i in range(len(self.values))])
        return Vector(sum(a * b for a, b in zip(row, other)) for row in self.values)


def vec3(value):
    return Vector(value[:3])


def vec4(value, w):
    return Vector((*value, w))


def mat3(matrix):
    return Matrix([row[:3] for row in matrix.values[:3]])


def normalize(value):
    length = math.sqrt(sum(component * component for component in value))
    return Vector(component / length for component in value)


def close(actual, expected):
    assert max(abs(a - b) for a, b in zip(actual, expected)) < 1e-10, (actual, expected)


def check_shader_world_math():
    paths = ROOT / "maps/toolkit/shaders"
    visible = (paths / "nature_lit_parametric.vs").read_text()
    shadow = (paths / "nature_shadow_parametric.vs").read_text()
    root = (paths / "nature_parametric.glsl").read_text()
    expressions = {
        "visible": re.search(r"vec3 world = ([^;]+);", visible).group(1),
        "displaced": re.search(r"\n\s*world = ([^;]+);", visible).group(1),
        "shadow": re.search(r"vec3 world = ([^;]+);", shadow).group(1),
        "root": re.search(r"vec3 worldRoot = ([^;]+);", root).group(1),
        "normal": re.search(r"fragNormal = ([^;]+);", visible).group(1),
    }
    for angle, translation in ((.18, (20, -9, 60)), (.73, (-11, 2, -40))):
        c, s = math.cos(angle), math.sin(angle)
        rotation = [[c, 0, s], [0, 1, 0], [-s, 0, c]]
        camera = Matrix([rotation[i] + [translation[i]] for i in range(3)] + [[0, 0, 0, 1]])
        inverse_translation = [-sum(rotation[j][i] * translation[j] for j in range(3)) for i in range(3)]
        inverse = Matrix([[rotation[j][i] for j in range(3)] + [inverse_translation[i]] for i in range(3)] + [[0, 0, 0, 1]])
        for offset in (Vector((0, 0, 0)), Vector((-11, 2.5, 7))):
            model = camera * Matrix([[1, 0, 0, offset[0]], [0, 1, 0, offset[1]], [0, 0, 1, offset[2]], [0, 0, 0, 1]])
            scope = dict(vec3=vec3, vec4=vec4, mat3=mat3, normalize=normalize,
                         matModel=model, u_worldFromShaderSpace=inverse, u_worldOffset=offset,
                         bentNormal=Vector((.2, .8, -.3)), root=Vector((33, .1, 24)))
            for local in (Vector((33, .1, 24)), Vector((33.08, .53, 23.96))):
                scope.update(local=local, shaderPosition=vec3(model * vec4(local, 1)))
                for name in ("visible", "displaced", "shadow"):
                    close(eval(expressions[name], {"__builtins__": {}}, scope), local + offset)
                close(eval(expressions["root"], {"__builtins__": {}}, scope), scope["root"] + offset)
                close(eval(expressions["normal"], {"__builtins__": {}}, scope), normalize(scope["bentNormal"]))
                close(vec3(inverse * model * vec4(local, 1)), local + offset)
    print("PASS: production world/LOD/normal expressions match camera-cancelled transforms")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--raylib-include", default="/tmp/rlvk_visual_cache/raylib/src")
    args = parser.parse_args()
    source = (ROOT / "maps/toolkit/map_props_meadow_parametric.inl").read_text()
    code = STUBS + "\n#define NATURE_PARAMETRIC_LODS 4\n"
    code += "\n".join(typedef(source, name) for name in ("NatureTuftTemplate", "NatureTuftRange", "NatureParametricMeadow"))
    code += "\n" + function(source, "NatureParametric_DrawChunk") + MAIN
    with tempfile.TemporaryDirectory(prefix="meadow-world-") as directory:
        work = pathlib.Path(directory)
        (work / "test.c").write_text(code)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT), "-I", args.raylib_include,
                        str(work / "test.c"), "-lm", "-o", str(work / "test")], check=True)
        subprocess.run([str(work / "test")], check=True)
    check_shader_world_math()


if __name__ == "__main__":
    main()
