#version 330
#include "core/shaders/common/vs_header.glsl"

// GPU-side "mọc lên" cho crystal cluster mesh tĩnh (xem
// ProceduralMesh_BuildCrystalClusterMesh trong core/geometry/pm_crystal.inl):
// mesh bake 1 lần ở chiều cao đầy đủ (local Y=0 là gốc/đế), CPU mỗi frame chỉ
// cần set uniform này thay vì rebuild toàn bộ hình học. Default 1.0
// (CrystalMaterial_Begin luôn set lại giá trị này mỗi Begin) để không phá vỡ
// các lệnh vẽ crystal immediate-mode cũ (progress đã bake sẵn ở CPU, không
// cần scale lại ở đây).
uniform float u_growProgress;

void main() {
    vec3 pos = vertexPosition;
    pos.y *= u_growProgress;
    VS_FinalOutput(pos);
}
