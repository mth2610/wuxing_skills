#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_texel;
uniform vec2 u_direction;
uniform float u_depthRange;
uniform float u_kernelRadius;
uniform int u_filterRadius;
uniform int u_fillHoles;
uniform mat4 u_projection;
uniform mat4 u_inverseProjection;

float ViewDistance(float deviceDepth) {
    vec4 clip=vec4(0.0,0.0,deviceDepth*2.0-1.0,1.0);
    vec4 view=u_inverseProjection*clip;
    return max(0.0001,-view.z/view.w);
}

float DeviceDepth(float viewDistance) {
    vec4 clip=u_projection*vec4(0.0,0.0,-viewDistance,1.0);
    return clip.z/clip.w*0.5+0.5;
}

void AccumulateSample(float sampleDeviceDepth, float spatialWeight,
                      inout float lower, inout float upper,
                      inout float weightedDepth, inout float weightSum,
                      float centerDistance) {
    if(sampleDeviceDepth>=0.99999) return;
    float sampleDistance=ViewDistance(sampleDeviceDepth);

    /* Narrow-range filtering is deliberately asymmetric. A much closer
     * sample belongs to another foreground sheet and is rejected. A deeper
     * sample is clamped to one kernel behind the centre, rounding the visible
     * surface near a discontinuity without pulling it into the background. */
    if(sampleDistance<lower) return;
    if(sampleDistance>upper) {
        sampleDistance=min(sampleDistance,
                           centerDistance+u_kernelRadius*1.10);
    } else {
        lower=min(lower,sampleDistance-u_depthRange);
        upper=max(upper,sampleDistance+u_depthRange);
    }
    weightedDepth+=sampleDistance*spatialWeight;
    weightSum+=spatialWeight;
}

void main() {
    float centerDevice=texture(texture0,fragTexCoord).r;

    /* Seed only a one-texel capture hole. This joins overlapping kernels but
     * does not grow isolated droplets or bridge separate fluid sheets. */
    if(centerDevice>=0.99999 && u_fillHoles!=0) {
        float nearest=1.0;
        for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++)
            nearest=min(nearest,texture(texture0,
                fragTexCoord+vec2(x,y)*u_texel).r);
        centerDevice=nearest;
    }
    if(centerDevice>=0.99999) {
        finalColor=vec4(1.0,0.0,0.0,1.0);
        return;
    }

    float centerDistance=ViewDistance(centerDevice);
    float weightedDepth=centerDistance;
    float weightSum=1.0;

    /* Each scan direction tracks its own connected narrow range, matching the
     * 2018 filter and avoiding cross-edge range expansion. */
    float lowerPositive=centerDistance-u_depthRange;
    float upperPositive=centerDistance+u_depthRange;
    float lowerNegative=lowerPositive;
    float upperNegative=upperPositive;

    for(int i=1;i<=4;i++) {
        if(i>u_filterRadius) break;
        float fi=float(i);
        float spatialWeight=exp(-0.5*fi*fi/4.0);
        float positive=texture(texture0,
            fragTexCoord+u_direction*u_texel*fi).r;
        float negative=texture(texture0,
            fragTexCoord-u_direction*u_texel*fi).r;
        AccumulateSample(positive,spatialWeight,
                         lowerPositive,upperPositive,
                         weightedDepth,weightSum,centerDistance);
        AccumulateSample(negative,spatialWeight,
                         lowerNegative,upperNegative,
                         weightedDepth,weightSum,centerDistance);
    }

    float filteredDistance=weightedDepth/max(weightSum,0.0001);
    finalColor=vec4(clamp(DeviceDepth(filteredDistance),0.0,1.0),
                    0.0,0.0,1.0);
}
