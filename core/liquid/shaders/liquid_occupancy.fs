#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
/* An 8x8 tile plus its one-pixel halo contains every possible 3x3 hole-fill
 * source. Integer fetches work even when R32F linear filtering is unsupported.
 * Clamp exactly like the reconstruction target's clamp-to-edge sampler. */
void main() {
    ivec2 size=textureSize(texture0,0);
    ivec2 tiles=(size+ivec2(7))/8;
    ivec2 base=clamp(ivec2(fragTexCoord*vec2(tiles)),ivec2(0),tiles-1)*8;
    float nearest=1.0;
    for(int y=-1;y<=8;y++) for(int x=-1;x<=8;x++)
        nearest=min(nearest,texelFetch(texture0,clamp(base+ivec2(x,y),ivec2(0),size-1),0).r);
    finalColor=vec4(nearest<0.99999?1.0:0.0,0.0,0.0,1.0);
}
