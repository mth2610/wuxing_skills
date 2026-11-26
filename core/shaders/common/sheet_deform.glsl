// ============================================================================
// WUXING — Reusable Thin-Sheet Aerodynamic Vertex Deformation (GLSL)
//
// GPU mirror of SheetAero_DeformPoint() in core/motion/sheet_aerodynamics.c.
// Evaluates longitudinal arch bend, stall S-curve, helical twist, transverse
// cupping, and traveling trailing-edge flutter, plus finite-difference normal.
//
// def  = vec4(bend, sBend, twist, cup)
// aero = vec2(flutterAmp, flutterPhase)
// ============================================================================

vec3 Sheet_DeformPos(vec3 p, vec2 halfExtents, vec4 def, vec2 aero) {
    float u = p.x / max(halfExtents.x, 0.0001);
    float v = p.y / max(halfExtents.y, 0.0001);

    // Slight length foreshortening when bent so sheet stays inextensible
    float archShrink = min(0.18, def.x * def.x * 0.042 + def.y * def.y * 0.032);
    p.y *= (1.0 - archShrink);

    // Longitudinal arch bend + stall S-curve camber
    float bendZ  = def.x * (v * v - 0.34) * 0.24;
    float sBendZ = def.y * (v * v * v - 0.56 * v) * 0.26;

    // Transverse cross-width cupping
    float cupZ = def.w * (u * u - 0.35) * 0.14;

    // Traveling aerodynamic trailing-edge flutter
    float tailWeight = 0.16 + 0.84 * clamp(0.5 - 0.5 * v, 0.0, 1.0);
    float edgeWeight = 0.72 + 0.28 * u * u;
    float w1 = sin(v * 6.0 + aero.y) * 0.72;
    float w2 = sin(v * 11.4 + aero.y * 1.68 + u * 2.2) * 0.28;
    float flutZ = aero.x * (w1 + w2) * tailWeight * edgeWeight;

    p.z += bendZ + sBendZ + cupZ + flutZ;

    // Helical torsional twist along spine
    float tw = def.z * v * 0.56;
    float cs = cos(tw);
    float sn = sin(tw);
    p.xz = mat2(cs, -sn, sn, cs) * p.xz;
    return p;
}

void Sheet_DeformPoint(vec3 p, vec2 halfExtents, vec4 def, vec2 aero,
                       out vec3 outPos, out vec3 outNormal) {
    vec3 p0 = Sheet_DeformPos(p, halfExtents, def, aero);
    vec3 px = Sheet_DeformPos(p + vec3(0.004, 0.0, 0.0), halfExtents, def, aero);
    vec3 py = Sheet_DeformPos(p + vec3(0.0, 0.004, 0.0), halfExtents, def, aero);
    outPos = p0;
    outNormal = normalize(cross(px - p0, py - p0));
}
