// Immutable authored blade descriptors. Seven RGBA32F texels per blade,
// 256 descriptors per row; template.z = local blade id + row role / 4.
uniform sampler2D u_bladeParameters;
uniform int u_bladeOffset;
uniform int u_bladesPerTuft;

vec4 NatureBladeParameter(int blade, int column)
{
    return texelFetch(u_bladeParameters, ivec2((blade % 256) * 7 + column, blade / 256), 0);
}

void NatureEvaluateBlade(out vec3 position, out vec3 normal,
                         out vec4 color, out vec2 windUV, out vec2 surfaceUV)
{
    int localBlade = int(floor(vertexPosition.z));
    int blade = u_bladeOffset + gl_InstanceID * u_bladesPerTuft + localBlade;
    vec4 p0 = NatureBladeParameter(blade, 0);
    vec4 p1 = NatureBladeParameter(blade, 1);
    vec4 p2 = NatureBladeParameter(blade, 2);
    vec4 p3 = NatureBladeParameter(blade, 3);
    vec4 clump = NatureBladeParameter(blade, 4);
    vec3 rootColor = NatureBladeParameter(blade, 5).rgb;
    vec3 tipColor = NatureBladeParameter(blade, 6).rgb;
    float t = vertexPosition.y;
    float sideSign = vertexPosition.x;
    float role = fract(vertexPosition.z) * 4.0; // 1 lower, 2 upper, 3 apex
    float u = 1.0 - t;
    vec3 center = p0.xyz*(u*u*u) + p1.xyz*(3.0*u*u*t) +
                  p2.xyz*(3.0*u*t*t) + p3.xyz*(t*t*t);
    vec3 tangent = normalize(3.0*u*u*(p1.xyz-p0.xyz) +
                             6.0*u*t*(p2.xyz-p1.xyz) + 3.0*t*t*(p3.xyz-p2.xyz));
    vec3 side = cross(tangent, vec3(0.0,1.0,0.0));
    float sideLength = length(side);
    side = sideLength > 0.001 ? side/sideLength : vec3(-sin(clump.w),0.0,cos(clump.w));
    vec3 geometricNormal = normalize(cross(side,tangent));
    if (geometricNormal.y < 0.0) geometricNormal = -geometricNormal;
    geometricNormal.y = max(geometricNormal.y, 0.25);
    geometricNormal = normalize(geometricNormal);
    float profile = p2.w > 0.5 ? (0.85 + 0.25*sin(3.141592653589793*t))*(1.0-pow(t,1.8))
                              : (0.72 + 0.50*t)*(1.0-t*t);
    float halfWidth = p1.w * 0.5 * max(profile,role < 1.5 ? 0.08 : 0.03);
    position = center + side*halfWidth*sideSign + geometricNormal*halfWidth*0.18;
    if (role > 2.5) position = center;
    vec3 sphereDirection = normalize(position - (clump.xyz-vec3(0.0,0.04,0.0)));
    vec3 target = normalize(mix(sphereDirection,vec3(0.0,1.0,0.0),role > 2.5 ? 0.55 : 0.45));
    vec3 meshNormal = normalize(geometricNormal + side*sideSign*(role < 1.5 ? 0.35 : 0.32));
    normal = normalize(mix(meshNormal,target,role > 2.5 ? 0.78 : 0.72));
    float occlusion = t < 0.22 ? 0.88 + 0.12*(t/0.22) : 1.0;
    if (role > 2.5) occlusion *= 0.86;
    // Match the two byte quantizations in Nature_LerpColor/Nature_ScaleColor.
    color = vec4(clamp(floor(floor(mix(rootColor,tipColor,t))*occlusion),0.0,255.0)/255.0,1.0);
    windUV = vec2(p0.w,t);
    surfaceUV = vec2(sideSign,t);
}
