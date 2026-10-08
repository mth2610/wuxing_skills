#include "maps/toolkit/map_ecology.h"
#include "maps/toolkit/meadow_palette.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
static uint8_t s_habitat[MAP_ECOLOGY_RES * MAP_ECOLOGY_RES * 4];
static uint8_t s_distance[MAP_ECOLOGY_RES * MAP_ECOLOGY_RES * 4];
static const MapEcology *s_owner;
static float EcoClamp(float v, float a, float b) { return fmaxf(a, fminf(b, v)); }
static float EcoSmooth(float a, float b, float v) {
    v = EcoClamp((v-a)/(b-a),0.0f,1.0f); return v*v*(3.0f-2.0f*v);
}
float MapEcology_RoadDistance(const MapEcologyConfig *c, float x, float z) {
    float d = MAP_ECOLOGY_DISTANCE_RANGE;
    for(int i=0; i<c->pathCount && i<MAP_ECOLOGY_MAX_PATHS; i++) {
        Vector4 p=c->paths[i]; float dx=p.z-p.x, dz=p.w-p.y;
        float t=EcoClamp(((x-p.x)*dx+(z-p.y)*dz)/fmaxf(dx*dx+dz*dz,1e-8f),0,1);
        float qx=x-p.x-t*dx, qz=z-p.y-t*dz;
        d=fminf(d,sqrtf(qx*qx+qz*qz)-c->roadHalfWidth);
    } return d;
}
float MapEcology_ShoreDistance(Vector4 lake, float x, float z) {
    if(lake.z<=0 || lake.w<=0) return MAP_ECOLOGY_DISTANCE_RANGE;
    /* Closest point on an ellipse in meters: solve its Lagrange multiplier.
     * Bisection handles anisotropic map texels without a pixel-distance scale. */
    float a=fmaxf(lake.z,lake.w), b=fminf(lake.z,lake.w);
    float px=fabsf(x-lake.x), py=fabsf(z-lake.y);
    if(lake.z<lake.w) { float q=px; px=py; py=q; }
    float sign=(px*px/(a*a)+py*py/(b*b)<1)?-1.0f:1.0f;
    if(py<1e-6f) {
        float cx=a*px/(a*a-b*b+1e-8f);
        if(cx<1.0f) { float qx=a*cx, qy=b*sqrtf(fmaxf(0,1-cx*cx));
            return sign*sqrtf((qx-px)*(qx-px)+qy*qy); }
        return sign*fabsf(a-px);
    }
    float lo=-b*b+b*py*0.00001f, hi=fmaxf(a*px+b*py,a*a);
    for(int i=0;i<24;i++) {
        float t=(lo+hi)*0.5f;
        float u=a*px/(t+a*a), v=b*py/(t+b*b);
        if(u*u+v*v>1.0f) lo=t; else hi=t;
    }
    float t=(lo+hi)*0.5f, qx=a*a*px/(t+a*a), qy=b*b*py/(t+b*b);
    return sign*sqrtf((qx-px)*(qx-px)+(qy-py)*(qy-py));
}
static void EcoEncode(float v, uint8_t *p) {
    float u=EcoClamp((v/MAP_ECOLOGY_DISTANCE_RANGE+1)*0.5f,0,1)*255;
    p[0]=(uint8_t)floorf(u); p[1]=(uint8_t)roundf((u-floorf(u))*255);
}
static float EcoDecode(const uint8_t *p) {
    return ((p[0]+p[1]/255.0f)/255.0f*2-1)*MAP_ECOLOGY_DISTANCE_RANGE;
}
bool MapEcology_Bake(MapEcology *m,const MapEcologyConfig *c,
                     MapEcologyDensityFn density,MapEcologyEligibleFn eligible,void *user) {
    if(!m || !c || c->rect.z<=0 || c->rect.w<=0 || c->pathCount<0
       || (c->pathCount>0 && !c->paths) || (s_owner && s_owner!=m)) return false;
    if(m->ready) return true;
    double start=GetTime();
    for(int y=0;y<MAP_ECOLOGY_RES;y++) for(int x=0;x<MAP_ECOLOGY_RES;x++) {
        float wx=c->rect.x+(x+0.5f)*c->rect.z/MAP_ECOLOGY_RES;
        float wz=c->rect.y+(y+0.5f)*c->rect.w/MAP_ECOLOGY_RES;
        int k=(y*MAP_ECOLOGY_RES+x)*4;
        float road=MapEcology_RoadDistance(c,wx,wz), shore=MapEcology_ShoreDistance(c->lake,wx,wz);
        float cover=density?density(wx,wz,user):1.0f;
        if(eligible && !eligible(wx,wz,user)) cover=0;
        float habitat=MeadowHabitat(wx,wz);
        float moisture=1.0f-EcoSmooth(0,3.0f,shore);
        /* Preserve the authored meadow's 0.30--0.42m broad canopy; G
         * stores its normalized growth potential. Wet margins add only 3mm. */
        float canopy=habitat>0.60f ? 0.36f+(habitat-0.60f)/0.40f*0.06f
                    : habitat<0.35f ? 0.30f+habitat/0.35f*0.04f
                    : 0.33f+(habitat-0.35f)/0.25f*0.04f;
        float growth=EcoClamp((canopy-0.30f)/0.12f+moisture*0.025f,0,1);
        s_habitat[k]=(uint8_t)roundf(EcoClamp(cover,0,1)*255);
        s_habitat[k+1]=(uint8_t)roundf(growth*255);
        s_habitat[k+2]=(uint8_t)roundf(moisture*255);
        s_habitat[k+3]=(uint8_t)roundf(habitat*255);
        EcoEncode(road,s_distance+k); EcoEncode(shore,s_distance+k+2);
    }
    Image h={s_habitat,MAP_ECOLOGY_RES,MAP_ECOLOGY_RES,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Image d={s_distance,MAP_ECOLOGY_RES,MAP_ECOLOGY_RES,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    m->habitatTexture=LoadTextureFromImage(h); m->distanceTexture=LoadTextureFromImage(d);
    if(!m->habitatTexture.id || !m->distanceTexture.id) { MapEcology_Unload(m); return false; }
    SetTextureFilter(m->habitatTexture,TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(m->distanceTexture,TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(m->habitatTexture,TEXTURE_WRAP_CLAMP); SetTextureWrap(m->distanceTexture,TEXTURE_WRAP_CLAMP);
    m->rect=c->rect; m->ready=true; s_owner=m;
    TraceLog(LOG_INFO,"MAP_ECOLOGY: 1024x1024 8MiB bake %.2f ms",(GetTime()-start)*1000);
    return true;
}
MapEcologySample MapEcology_Sample(const MapEcology *m,float x,float z) {
    MapEcologySample out={0};
    if(!m || !m->ready || m!=s_owner || x<m->rect.x || z<m->rect.y || x>m->rect.x+m->rect.z || z>m->rect.y+m->rect.w) return out;
    float fx=EcoClamp((x-m->rect.x)/m->rect.z*MAP_ECOLOGY_RES-0.5f,0,MAP_ECOLOGY_RES-1);
    float fy=EcoClamp((z-m->rect.y)/m->rect.w*MAP_ECOLOGY_RES-0.5f,0,MAP_ECOLOGY_RES-1);
    int ix=(int)fx, iy=(int)fy, jx=ix+1<MAP_ECOLOGY_RES?ix+1:ix, jy=iy+1<MAP_ECOLOGY_RES?iy+1:iy;
    float tx=fx-ix,ty=fy-iy;
    float v[6]={0};
    for(int j=0;j<2;j++) for(int i=0;i<2;i++) {
        int k=((j?jy:iy)*MAP_ECOLOGY_RES+(i?jx:ix))*4;
        float w=(i?tx:1-tx)*(j?ty:1-ty);
        for(int c=0;c<4;c++) v[c]+=s_habitat[k+c]/255.0f*w;
        v[4]+=EcoDecode(s_distance+k)*w;v[5]+=EcoDecode(s_distance+k+2)*w;
    }
    out=(MapEcologySample){v[0],v[1],v[2],v[3],v[4],v[5]};return out;
}
void MapEcology_Unload(MapEcology *m) {
    if(!m)return;
    if(m->habitatTexture.id)UnloadTexture(m->habitatTexture);
    if(m->distanceTexture.id)UnloadTexture(m->distanceTexture);
    if(s_owner==m)s_owner=NULL;
    memset(m,0,sizeof(*m));
}
