// Shared production Wind evaluation. Requires motion_wind_types/noise.
vec2 terrainHeightValidity(int sampleIndex) {
    vec4 packed = uWindTerrain.samples[sampleIndex / 2];
    return ((sampleIndex & 1) == 0) ? packed.xy : packed.zw;
}

bool sampleTerrainHeight(vec2 worldXZ, out float height) {
    if (uWindTerrain.meta.x == 0 || uWindTerrain.meta.y != WIND_TERRAIN_GRID_SIZE)
        return false;
    vec2 cell = uWindTerrain.origin_cell.zw;
    if (cell.x <= 0.0 || cell.y <= 0.0) return false;

    vec2 grid = (worldXZ - uWindTerrain.origin_cell.xy) / cell;
    float gridMax = float(WIND_TERRAIN_GRID_SIZE - 1);
    if (grid.x < 0.0 || grid.y < 0.0 || grid.x > gridMax || grid.y > gridMax)
        return false;

    ivec2 base = ivec2(floor(grid));
    base = min(base, ivec2(WIND_TERRAIN_GRID_SIZE - 2));
    vec2 fraction = grid - vec2(base);
    int i00 = base.y * WIND_TERRAIN_GRID_SIZE + base.x;
    int i10 = i00 + 1;
    int i01 = i00 + WIND_TERRAIN_GRID_SIZE;
    int i11 = i01 + 1;
    vec2 h00 = terrainHeightValidity(i00);
    vec2 h10 = terrainHeightValidity(i10);
    vec2 h01 = terrainHeightValidity(i01);
    vec2 h11 = terrainHeightValidity(i11);
    if (min(min(h00.y, h10.y), min(h01.y, h11.y)) < 0.5) return false;

    float nearHeight = mix(h00.x, h10.x, fraction.x);
    float farHeight = mix(h01.x, h11.x, fraction.x);
    height = mix(nearHeight, farHeight, fraction.y);
    return true;
}

float evalTerrainLift(vec3 pos, vec3 macroVelocity) {
    float liftK = uWind.macro_params.w;
    float speedXZ = length(macroVelocity.xz);
    if (liftK <= 0.0 || speedXZ <= 1e-3) return 0.0;

    const float sampleDist = 1.5;
    vec2 directionXZ = macroVelocity.xz / speedXZ;
    float h0 = 0.0;
    float h1 = 0.0;
    bool hasH0 = sampleTerrainHeight(pos.xz, h0);
    bool hasH1 = sampleTerrainHeight(pos.xz + directionXZ * sampleDist, h1);
    if (!hasH0 || !hasH1) return 0.0;

    float positiveSlope = max((h1 - h0) / sampleDist, 0.0);
    return clamp(positiveSlope * speedXZ * liftK, 0.0, 12.0);
}

// Động lực học luồng gió mô phỏng theo Ghost of Tsushima Wind System
vec3 evalWindVelocityMode(vec3 pos, float time, bool background) {
    vec3 totalVel = vec3(0.0);
    // 1. Gió vĩ mô cuộn gradient noise 3D theo không gian và thời gian
    vec3 baseDir = uWind.macro_dir_amp.xyz;
    float gustAmp = uWind.macro_dir_amp.w;
    float baseLen = length(baseDir);
    if (baseLen > 1e-4) {
        if (gustAmp > 1e-4) {
            float s = uWind.macro_params.x;
            float spd = uWind.macro_params.y;
            float nx = noiseScalar(vec3(pos.x * s - time * spd, pos.y * s + 17.3, pos.z * s - time * spd * 0.7));
            float ny = noiseScalar(vec3(pos.x * s + 37.1, pos.y * s - time * spd * 0.8, pos.z * s + 19.7));
            float nz = noiseScalar(vec3(pos.x * s - time * spd * 0.6, pos.y * s + 53.9, pos.z * s + time * spd * 0.5));
            float amp = gustAmp * max(baseLen, 2.0);
            vec3 turb = vec3(
                baseDir.x * (1.0 + gustAmp * nx * 0.5) + nx * amp * 0.5,
                baseDir.y + ny * amp * 0.35,
                baseDir.z * (1.0 + gustAmp * nz * 0.5) + nz * amp * 0.5
            );
            totalVel += turb;
        } else {
            totalVel += baseDir;
        }
    }

    // 2. Điều biến vận tốc theo cao độ (Atmospheric Boundary Layer Height Gradient)
    float heightK = uWind.macro_extra.x;
    if (heightK > 0.0) {
        float h0 = 0.0;
        if (sampleTerrainHeight(pos.xz, h0)) {
            float hRel = max(pos.y - h0, 0.0);
            float low = min(hRel / 2.5, 1.0);
            float high = (hRel > 2.5) ? min((hRel - 2.5) / 9.5, 1.0) : 0.0;
            float hFactor = 0.5 + 0.5 * low + 0.4 * high;
            float factor = 1.0 + heightK * (hFactor - 1.0);
            totalVel *= factor;
        }
    }

    // 3. Nâng luồng gió theo dốc địa hình từ lưới dùng chung CPU/GPU.
    totalVel.y += evalTerrainLift(pos, totalVel);

    // 3. Các xoáy khí cục bộ (Vorticles: Linear Gust, Radial Blast, Vortex)
    int vortCount = int(uWind.macro_params.z + 0.5);
    for (int i = background?int(uWind.macro_extra.y):0; i < vortCount && i < MAX_GPU_VORTICLES; ++i) {
        vec3 vPos = uWind.vorticles[i].pos_radius.xyz;
        float vRadius = uWind.vorticles[i].pos_radius.w;
        vec3 delta = pos - vPos;
        float distSq = dot(delta, delta);
        float rSq = vRadius * vRadius;
        if (distSq >= rSq || rSq < 1e-6) continue;

        float dist = sqrt(distSq);
        float spatialAtten = 1.0 - (dist / vRadius);
        float lifetime = uWind.vorticles[i].params.y;
        float maxLifetime = uWind.vorticles[i].params.z;
        float temporalAtten = (maxLifetime > 1e-4) ? (lifetime / maxLifetime) : 1.0;
        float weight = spatialAtten * temporalAtten;

        int vType = int(uWind.vorticles[i].params.x + 0.5);
        vec3 vDir = uWind.vorticles[i].dir_strength.xyz;
        float vStrength = uWind.vorticles[i].dir_strength.w;

        if (vType == 0) { // VORTICLE_LINEAR_GUST
            totalVel += vDir * (vStrength * weight);
        } else if (vType == 1) { // VORTICLE_RADIAL_BLAST
            vec3 normOut = (dist > 1e-4) ? (delta / dist) : vec3(0.0, 1.0, 0.0);
            totalVel += normOut * (vStrength * weight);
        } else if (vType == 2) { // VORTICLE_VORTEX (Mô hình Rankine: rCore = 0.10 * R)
            vec3 tangent = cross(vDir, delta);
            float tanLen = length(tangent);
            float rCore = 0.10 * vRadius;
            float coreFactor = (tanLen < rCore) ? (tanLen / rCore) : 1.0;
            if (tanLen > 1e-4) {
                totalVel += (tangent / tanLen) * (vStrength * weight * coreFactor);
            }
            float inwardPull = uWind.vorticles[i].params.w;
            if (abs(inwardPull) > 1e-4) {
                float proj = dot(delta, vDir);
                vec3 closestOnAxis = vDir * proj;
                vec3 radial = delta - closestOnAxis;
                float radDist = length(radial);
                if (radDist > 1e-4) {
                    vec3 radDir = radial / radDist;
                    totalVel += radDir * (-inwardPull * weight * coreFactor);
                }
            }
        } else if (vType == 3) { // VORTICLE_TURBULENCE
            // Nhiễu hash-gradient 3D cục bộ: 3 kênh decorrelated
            float ns = vDir.x; // noiseScale packed in direction.x
            float spd = vDir.y; // noiseSpeed packed in direction.y
            float attackTime = max(vDir.z, 0.0);
            float age = max(maxLifetime - lifetime, 0.0);
            float attackWeight = attackTime > 1e-4
                ? smoothstep(0.0, attackTime, age) : 1.0;
            float px_s = pos.x * ns, py_s = pos.y * ns, pz_s = pos.z * ns;
            float t = time * spd;
            float nx = noiseScalar(vec3(px_s + t,       py_s + 17.3, pz_s - t * 0.7));
            float ny = noiseScalar(vec3(px_s + 37.1,    py_s - t * 0.8, pz_s + 19.7));
            float nz = noiseScalar(vec3(px_s - t * 0.6, py_s + 53.9, pz_s + t * 0.5));
            float turbStrength = vStrength * weight * attackWeight;
            totalVel += vec3(nx * turbStrength, ny * turbStrength, nz * turbStrength);
        }
    }
    return totalVel;
}

vec3 evalGuidingWind(vec3 pos,float time) {
    if(uWind.guidingOrigin.w<.5 || uWind.guidingTarget.w<=.001) return vec3(0);
    vec3 dir=uWind.guidingDirection.xyz,toPos=pos-uWind.guidingOrigin.xyz;
    float span=length((uWind.guidingTarget.xyz-uWind.guidingOrigin.xyz).xz);
    if(span<1.0) span=24.0;
    float along=dot(toPos.xz,dir.xz);if(along< -1.5 || along>span+3.0) return vec3(0);
    float perp=length(toPos.xz-dir.xz*along),dy=abs(toPos.y);
    if(perp>=3.8 || dy>=2.8) return vec3(0);
    float w=(1.0-perp/3.8)*(1.0-perp/3.8)*(1.0-dy/2.8)*uWind.guidingTarget.w;
    if(w<=.001) return vec3(0);
    float speed=uWind.guidingDirection.w;
    vec3 flow=dir*speed*(1.0+.22*sin(along*.20-time*3.5))*w;
    flow+=vec3(noiseScalar(vec3(pos.x*.35,pos.y*.35+time*1.5,pos.z*.35)),
        noiseScalar(vec3(pos.x*.35+17.3,pos.y*.35-time*1.2,pos.z*.35+19.7)),
        noiseScalar(vec3(pos.x*.35-23.5,pos.y*.35,pos.z*.35+time*1.6)))*vec3(.8,.4,.8)*w;
    float h0,h1;bool hasH0=sampleTerrainHeight(pos.xz,h0),hasH1=sampleTerrainHeight(pos.xz+dir.xz*2.4,h1);
    if(hasH0) {
        if(hasH1 && h1>h0) flow.y+=min((h1-h0)/2.4*speed*.45*w,6.0);
        if(pos.y<h0+1.2) flow.y+=min((h0+1.2-pos.y)*3.0*w,8.0);
    }
    return flow;
}
vec3 evalWindVelocity(vec3 pos,float time) { return evalWindVelocityMode(pos,time,false)+evalGuidingWind(pos,time); }
vec3 evalMotionBackgroundWind(vec3 pos,float time) { return evalWindVelocityMode(pos,time,true)+evalGuidingWind(pos,time); }
