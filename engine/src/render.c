#include "game_internal.h"

#include <stdint.h>

typedef struct { float x, y, z; } rv3;
typedef struct { float x, y, z; } cv3;
typedef struct { float sx, sy, z; int valid; } rpv;
typedef struct {
    float cam_x, cam_y, cam_z;
    float forward_x, forward_z;
    float right_x, right_z;
    float cp, sp;
    float focal;
    uint32_t w, h;
} rcam;

typedef struct {
    uint32_t sky_top,sky_horizon,fog,water,water_glint;
    uint32_t land_low,land_mid,land_high,coast,coast_edge,road;
    uint32_t building,building_alt,glass,rock,trunk,leaf_low,leaf_high;
    uint32_t neutral_turret,ammo,accent;
} visual_palette;

static uint32_t g_daylight_q8=256u;
static float g_daylight_f=1.0f;
static float g_sun_world_x=0.0f;
static float g_sun_world_z=1.0f;
static uint8_t g_depth_fog_amount[523];
static uint16_t g_depth_fog_r_term[97];
static uint16_t g_depth_fog_g_term[97];
static uint16_t g_depth_fog_b_term[97];
static uint32_t g_depth_fog_color=UINT32_MAX;
static uint32_t g_depth_fog_ready=0u;

static visual_palette palette(void) {
    static const visual_palette p[ODG_VISUAL_THEME_COUNT] = {
        /* North Atlantic — mineral terrain, oxidised metal and cold air. */
        {0x07111dffu,0x526f7dffu,0x788b91ffu,0x0a2634ffu,0x548b96ffu,
         0x29483dffu,0x36594affu,0x536a55ffu,0x8d8068ffu,0xc6b48effu,0x293842ffu,
         0x3b4853ffu,0x4b5964ffu,0x709ba5ffu,0x4d5351ffu,0x4c4037ffu,0x305446ffu,0x456b58ffu,
         0xc4ccd0ffu,0xd4a650ffu,0x68aebaffu},
        /* Temperate daylight — clear visibility without toy-like saturation. */
        {0x102537ffu,0x7b9aa3ffu,0x91a5a6ffu,0x173a46ffu,0x78a6adffu,
         0x3b5848ffu,0x4a674fffu,0x66755affu,0x9c8d70ffu,0xd3c19affu,0x39464bffu,
         0x505b61ffu,0x626c70ffu,0x8daeb4ffu,0x5b605dffu,0x55453affu,0x41614affu,0x58765affu,
         0xd6d9d3ffu,0xd5a64cffu,0x8ebdb9ffu},
        /* Copper hour — warm atmosphere over restrained olive and slate. */
        {0x171a27ffu,0x9b6d63ffu,0x927e77ffu,0x243544ffu,0x9b7167ffu,
         0x4d5745ffu,0x60664bffu,0x797657ffu,0xa17d59ffu,0xd0a574ffu,0x443f46ffu,
         0x62565affu,0x756568ffu,0xb28879ffu,0x625d61ffu,0x5c4335ffu,0x4f5d43ffu,0x6a704dffu,
         0xded3c8ffu,0xd09a49ffu,0xc98267ffu},
        /* Blackwater — near-black arena with one controlled signal colour. */
        {0x03070cffu,0x1d2933ffu,0x46535bffu,0x06151dffu,0x244955ffu,
         0x172b2dffu,0x203738ffu,0x304744ffu,0x5d5951ffu,0x8f8372ffu,0x202a31ffu,
         0x2d3943ffu,0x3e4a53ffu,0x567f8affu,0x40494bffu,0x3f3934ffu,0x24473dffu,0x345b4affu,
         0xb9c3c9ffu,0xc99a4affu,0x699cafffu}
    };
    uint32_t t=g_odg.visual_theme;
    if (t>=ODG_VISUAL_THEME_COUNT) t=0u;
    return p[t];
}

static void prepare_depth_fog(void) {
    uint32_t fog=palette().fog;
    uint32_t i;
    if(g_depth_fog_ready==0u){
        for(i=0u;i<=522u;++i)
            g_depth_fog_amount[i]=(uint8_t)(i>=522u?0u:(i<=211u?96u:((522u-i)*96u)/(522u-211u)));
        g_depth_fog_ready=1u;
    }
    if(g_depth_fog_color!=fog){
        uint32_t fr=(fog>>24)&255u,fg=(fog>>16)&255u,fb=(fog>>8)&255u;
        for(i=0u;i<=96u;++i){
            g_depth_fog_r_term[i]=(uint16_t)(fr*i);
            g_depth_fog_g_term[i]=(uint16_t)(fg*i);
            g_depth_fog_b_term[i]=(uint16_t)(fb*i);
        }
        g_depth_fog_color=fog;
    }
}

static uint32_t rgba_lerp(uint32_t a,uint32_t b,uint32_t t256) {
    uint32_t ar=(a>>24)&255u,ag=(a>>16)&255u,ab=(a>>8)&255u;
    uint32_t br=(b>>24)&255u,bg=(b>>16)&255u,bb=(b>>8)&255u;
    uint32_t inv=256u-t256;
    uint32_t r=(ar*inv+br*t256)>>8,g=(ag*inv+bg*t256)>>8,bl=(ab*inv+bb*t256)>>8;
    return (r<<24)|(g<<16)|(bl<<8)|0xffu;
}

static uint32_t rgba_mix(uint32_t c, float k) {
    uint32_t r = (c >> 24) & 255u;
    uint32_t g = (c >> 16) & 255u;
    uint32_t b = (c >> 8) & 255u;
    uint32_t a = c & 255u;
    uint32_t rr = (uint32_t)((float)r * k);
    uint32_t gg = (uint32_t)((float)g * k);
    uint32_t bb = (uint32_t)((float)b * k);
    if (rr > 255u) rr = 255u;
    if (gg > 255u) gg = 255u;
    if (bb > 255u) bb = 255u;
    return (rr << 24) | (gg << 16) | (bb << 8) | a;
}

static uint32_t actor_base_color(uint32_t id) {
    /* Identity remains readable, but lives in the same mineral colour system as the
     * world instead of looking like fluorescent toy plastic. */
    static const uint32_t colors[ODG_MAX_ACTORS] = {
        0x58adbdffu, 0xc95d70ffu, 0xc58d4dffu, 0x8e74b1ffu, 0x55a27affu,
        0xb96691ffu, 0xc4a34fffu, 0x667faeffu, 0xbd6855ffu, 0x77a45bffu
    };
    return colors[id % ODG_MAX_ACTORS];
}

static uint32_t territory_color(uint32_t id,uint32_t terrain) {
    /* Ownership is a stain/material response in the land, not a luminous tile laid on
     * top. Stable colour also removes the old per-cell shimmer while the camera moves. */
    uint32_t signal=actor_base_color(id);
    return rgba_lerp(terrain,signal,id==ODG_PLAYER_ID?82u:62u);
}

static uint32_t trail_color(uint32_t id) {
    return rgba_mix(actor_base_color(id),1.06f);
}

static uint32_t visual_hash2(uint32_t x,uint32_t z) {
    uint32_t v=x*UINT32_C(0x9e3779b1)^z*UINT32_C(0x85ebca6b)^UINT32_C(0x51ed270b);
    v^=v>>16u;v*=UINT32_C(0x7feb352d);v^=v>>15u;v*=UINT32_C(0x846ca68b);v^=v>>16u;
    return v;
}

static float terrain_yf(float x, float z) {
    int32_t fx=(int32_t)(x*(float)ODG_FX_ONE);
    int32_t fz=(int32_t)(z*(float)ODG_FX_ONE);
    return odg_fx_to_float(odg_terrain_height_fx(fx,fz));
}

/* Reversed inverse-depth. Rasterization already interpolates 1/z, so v12 stores that
 * quantity directly instead of dividing back to z for every covered pixel. Larger values
 * are nearer. The mapping keeps useful precision across the 0.15..45m camera range. */
static uint16_t invz16(float inv_z) {
    int v=(int)(inv_z*9000.0f);
    if(v<1)v=1;
    if(v>65535)v=65535;
    return (uint16_t)v;
}

static uint32_t depth_fog_inv(uint32_t c,uint16_t inv_depth) {
    uint32_t t,near_weight;
    uint32_t r,g,b;
    /* ~17m starts fog, ~43m reaches the current 37.5% cap. Interpolating in inverse
     * depth is monotonic. Amount and fog-weighted terms are cached outside the hot
     * pixel path, so this executes no palette copy or division per fragment. */
    if((uint32_t)inv_depth>=522u)return c;
    t=g_depth_fog_amount[inv_depth];near_weight=256u-t;
    r=(c>>24)&255u;g=(c>>16)&255u;b=(c>>8)&255u;
    r=(r*near_weight+g_depth_fog_r_term[t])>>8;
    g=(g*near_weight+g_depth_fog_g_term[t])>>8;
    b=(b*near_weight+g_depth_fog_b_term[t])>>8;
    return (r<<24)|(g<<16)|(b<<8)|(c&255u);
}

static void put_px_index(uint32_t idx,uint32_t c,uint16_t inv_depth) {
    uint8_t *p;
    if(inv_depth<=g_odg_depth[idx])return;
    g_odg_depth[idx]=inv_depth;
    c=depth_fog_inv(c,inv_depth);
    p=&g_odg_framebuffer[idx*4u];
    p[0]=(uint8_t)(c>>24);p[1]=(uint8_t)(c>>16);p[2]=(uint8_t)(c>>8);p[3]=(uint8_t)c;
    ++g_odg.render_pixels_touched;
}

static void put_px(int x,int y,uint32_t c,uint16_t inv_depth) {
    uint32_t idx;
    if(x<0||y<0||x>=(int)g_odg.width||y>=(int)g_odg.height)return;
    idx=(uint32_t)y*g_odg.width+(uint32_t)x;
    put_px_index(idx,c,inv_depth);
}

static cv3 world_to_camera(const rcam *c, rv3 p) {
    float dx = p.x - c->cam_x;
    float dy = p.y - c->cam_y;
    float dz = p.z - c->cam_z;
    float local_x = dx * c->right_x + dz * c->right_z;
    float local_f = dx * c->forward_x + dz * c->forward_z;
    cv3 o;
    o.x = local_x;
    o.y = c->cp * dy + c->sp * local_f;
    o.z = -c->sp * dy + c->cp * local_f;
    return o;
}

static int world_point_maybe_visible(const rcam *c,float x,float z,float radius) {
    float dx=x-c->cam_x,dz=z-c->cam_z;
    float forward=dx*c->forward_x+dz*c->forward_z;
    float side=dx*c->right_x+dz*c->right_z;
    if(side<0.0f)side=-side;
    if(forward < -(radius+0.55f)) return 0;
    /* Conservative horizontal frustum guard. Gameplay uses ~80deg FOV and showcase is
     * slightly wider; 1.22 leaves generous safety for cell corners and near clipping. */
    if(forward>0.0f && side > forward*1.22f + radius + 1.40f) return 0;
    return 1;
}

static rpv project_camera(const rcam *c, cv3 p) {
    rpv o;
    o.valid = p.z > 0.15f;
    if (!o.valid) { o.sx = 0.0f; o.sy = 0.0f; o.z = 999.0f; return o; }
    o.sx = (float)c->w * 0.5f + p.x * c->focal / p.z;
    o.sy = (float)c->h * 0.43f - p.y * c->focal / p.z;
    o.z = p.z;
    return o;
}


static float edgef(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static cv3 clip_intersection(cv3 a, cv3 b, float near_z) {
    float denom = b.z - a.z;
    float t = denom != 0.0f ? (near_z - a.z) / denom : 0.0f;
    cv3 o;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    o.z = near_z;
    return o;
}

static uint32_t clip_near_triangle(const cv3 in[3], cv3 out[4]) {
    const float near_z = 0.151f;
    uint32_t count = 0u;
    uint32_t i;
    cv3 prev = in[2];
    int prev_inside = prev.z >= near_z;
    for (i = 0u; i < 3u; ++i) {
        cv3 cur = in[i];
        int cur_inside = cur.z >= near_z;
        if (cur_inside != 0) {
            if (prev_inside == 0 && count < 4u) out[count++] = clip_intersection(prev, cur, near_z);
            if (count < 4u) out[count++] = cur;
        } else if (prev_inside != 0 && count < 4u) {
            out[count++] = clip_intersection(prev, cur, near_z);
        }
        prev = cur;
        prev_inside = cur_inside;
    }
    return count;
}

static int float_ceil_to_int(float v) {
    int i=(int)v;
    if ((float)i<v) ++i;
    return i;
}

static int float_floor_to_int(float v) {
    int i=(int)v;
    if ((float)i>v) --i;
    return i;
}

static int edge_span_clip(float b,float a,int *lo,int *hi) {
    const float eps=0.00001f;
    if (a>eps) {
        int v=float_ceil_to_int((-b/a)-eps);
        if (v>*lo) *lo=v;
    } else if (a<-eps) {
        int v=float_floor_to_int((b/(-a))+eps);
        if (v<*hi) *hi=v;
    } else if (b<0.0f) {
        return 0;
    }
    return *lo<=*hi;
}

static void raster_camera_triangle(const rcam *c, cv3 a, cv3 b, cv3 d, uint32_t color) {
    rpv p0 = project_camera(c, a);
    rpv p1 = project_camera(c, b);
    rpv p2 = project_camera(c, d);
    float area;
    float minxf, maxxf, minyf, maxyf;
    float inv_area, iz0, iz1, iz2;
    float dw0dx, dw1dx, dw2dx, dw0dy, dw1dy, dw2dy;
    float row_w0, row_w1, row_w2;
    float row_invz, invz_dx, invz_dy;
    float sign;
    int minx, maxx, miny, maxy, y;
    if (!p0.valid || !p1.valid || !p2.valid) return;
    area = edgef(p0.sx, p0.sy, p1.sx, p1.sy, p2.sx, p2.sy);
    if (area > -0.02f && area < 0.02f) return;
    minxf = p0.sx; if (p1.sx < minxf) minxf = p1.sx; if (p2.sx < minxf) minxf = p2.sx;
    maxxf = p0.sx; if (p1.sx > maxxf) maxxf = p1.sx; if (p2.sx > maxxf) maxxf = p2.sx;
    minyf = p0.sy; if (p1.sy < minyf) minyf = p1.sy; if (p2.sy < minyf) minyf = p2.sy;
    maxyf = p0.sy; if (p1.sy > maxyf) maxyf = p1.sy; if (p2.sy > maxyf) maxyf = p2.sy;
    minx = (int)minxf; maxx = (int)maxxf + 1;
    miny = (int)minyf; maxy = (int)maxyf + 1;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)c->w) maxx = (int)c->w - 1;
    if (maxy >= (int)c->h) maxy = (int)c->h - 1;
    if (maxx < minx || maxy < miny) return;

    /* v12 scanline spans. Edge functions are affine, therefore each scanline can solve
     * the three half-plane inequalities once and rasterize only covered pixels.  The hot
     * inner loop then advances inverse depth with one add instead of testing three edges
     * and rebuilding barycentrics for every pixel in a triangle's bounding box. */
    inv_area = 1.0f / area;
    iz0 = 1.0f / p0.z; iz1 = 1.0f / p1.z; iz2 = 1.0f / p2.z;
    dw0dx = p2.sy - p1.sy; dw0dy = -(p2.sx - p1.sx);
    dw1dx = p0.sy - p2.sy; dw1dy = -(p0.sx - p2.sx);
    dw2dx = p1.sy - p0.sy; dw2dy = -(p1.sx - p0.sx);
    row_w0 = edgef(p1.sx,p1.sy,p2.sx,p2.sy,(float)minx+0.5f,(float)miny+0.5f);
    row_w1 = edgef(p2.sx,p2.sy,p0.sx,p0.sy,(float)minx+0.5f,(float)miny+0.5f);
    row_w2 = edgef(p0.sx,p0.sy,p1.sx,p1.sy,(float)minx+0.5f,(float)miny+0.5f);
    invz_dx=(dw0dx*iz0+dw1dx*iz1+dw2dx*iz2)*inv_area;
    invz_dy=(dw0dy*iz0+dw1dy*iz1+dw2dy*iz2)*inv_area;
    row_invz=(row_w0*iz0+row_w1*iz1+row_w2*iz2)*inv_area;
    sign=area>0.0f?1.0f:-1.0f;

    for (y=miny;y<=maxy;++y) {
        int lo=0,hi=maxx-minx;
        float b0=sign*row_w0,b1=sign*row_w1,b2=sign*row_w2;
        float a0=sign*dw0dx,a1=sign*dw1dx,a2=sign*dw2dx;
        if (edge_span_clip(b0,a0,&lo,&hi) && edge_span_clip(b1,a1,&lo,&hi) && edge_span_clip(b2,a2,&lo,&hi)) {
            int x=minx+lo;
            int xend=minx+hi;
            float inv_z=row_invz+(float)lo*invz_dx;
            uint32_t idx=(uint32_t)y*c->w+(uint32_t)x;
            for (;x<=xend;++x,++idx) {
                if (inv_z>0.0f) put_px_index(idx,color,invz16(inv_z));
                inv_z+=invz_dx;
            }
        }
        row_w0+=dw0dy;row_w1+=dw1dy;row_w2+=dw2dy;row_invz+=invz_dy;
    }
    ++g_odg.render_triangles;
}

static void tri(const rcam *c, rv3 a, rv3 b, rv3 d, uint32_t color) {
    cv3 input[3] = { world_to_camera(c, a), world_to_camera(c, b), world_to_camera(c, d) };
    cv3 clipped[4];
    uint32_t n = clip_near_triangle(input, clipped);
    if (n < 3u) return;
    raster_camera_triangle(c, clipped[0], clipped[1], clipped[2], color);
    if (n == 4u) raster_camera_triangle(c, clipped[0], clipped[2], clipped[3], color);
}

static void line_overlay(const rcam *c, rv3 a, rv3 b, uint32_t color) {
    const float near_z=0.151f;
    cv3 ca=world_to_camera(c,a),cb=world_to_camera(c,b);
    rpv p0,p1;
    float invz,invz_step;
    int x0,y0,x1,y1,dx,dy,sx,sy,err,e2,steps;
    if (ca.z<near_z && cb.z<near_z) return;
    if (ca.z<near_z) ca=clip_intersection(ca,cb,near_z);
    if (cb.z<near_z) cb=clip_intersection(cb,ca,near_z);
    p0=project_camera(c,ca);p1=project_camera(c,cb);
    if(!p0.valid||!p1.valid) return;
    x0=(int)p0.sx;y0=(int)p0.sy;x1=(int)p1.sx;y1=(int)p1.sy;
    dx=odg_abs_i32(x1-x0);dy=-odg_abs_i32(y1-y0);
    sx=x0<x1?1:-1;sy=y0<y1?1:-1;err=dx+dy;
    steps=odg_abs_i32(x1-x0);
    if(odg_abs_i32(y1-y0)>steps) steps=odg_abs_i32(y1-y0);
    if(steps<1) steps=1;
    invz=1.0f/p0.z;
    invz_step=((1.0f/p1.z)-invz)/(float)steps;
    for(;;){
        if(invz>0.0f) put_px(x0,y0,color,invz16(invz));
        if(x0==x1&&y0==y1)break;
        e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}
        invz+=invz_step;
    }
}

static void quad(const rcam *c, rv3 a, rv3 b, rv3 d, rv3 e, uint32_t color) {
    tri(c, a, b, d, color);
    tri(c, a, d, e, color);
}

static void box_y(const rcam *c, float x, float z, float hx, float hz, float y0, float h, uint32_t base) {
    rv3 v[8] = {
        {x-hx,y0,z-hz},{x+hx,y0,z-hz},{x+hx,y0,z+hz},{x-hx,y0,z+hz},
        {x-hx,y0+h,z-hz},{x+hx,y0+h,z-hz},{x+hx,y0+h,z+hz},{x-hx,y0+h,z+hz}
    };
    uint32_t top = rgba_mix(base, 1.18f);
    uint32_t side = rgba_mix(base, 0.82f);
    uint32_t dark = rgba_mix(base, 0.62f);
    tri(c,v[4],v[5],v[6],top); tri(c,v[4],v[6],v[7],top);
    tri(c,v[0],v[1],v[5],dark); tri(c,v[0],v[5],v[4],dark);
    tri(c,v[1],v[2],v[6],side); tri(c,v[1],v[6],v[5],side);
    tri(c,v[2],v[3],v[7],dark); tri(c,v[2],v[7],v[6],dark);
    tri(c,v[3],v[0],v[4],side); tri(c,v[3],v[4],v[7],side);
}

static void octa(const rcam *c, float x, float z, float r, float y0, float h, uint32_t base) {
    rv3 top = {x,y0+h,z}, bot = {x,y0+0.02f,z};
    rv3 e = {x+r,y0+h*0.45f,z}, w = {x-r,y0+h*0.45f,z};
    rv3 n = {x,y0+h*0.45f,z+r}, s = {x,y0+h*0.45f,z-r};
    uint32_t a = rgba_mix(base,1.18f), b = rgba_mix(base,0.92f), d = rgba_mix(base,0.70f);
    tri(c,top,e,n,a); tri(c,top,n,w,a); tri(c,top,w,s,b); tri(c,top,s,e,b);
    tri(c,bot,n,e,d); tri(c,bot,w,n,d); tri(c,bot,s,w,d); tri(c,bot,e,s,d);
}

static rv3 oriented_point(float x, float z, float fx, float fz, float local_x, float local_z, float y) {
    float rx = fz;
    float rz = -fx;
    rv3 p;
    p.x = x + rx * local_x + fx * local_z;
    p.y = y;
    p.z = z + rz * local_x + fz * local_z;
    return p;
}

static void oriented_box_y(const rcam *c,float x,float z,float fx,float fz,
                           float local_x,float local_z,float hx,float hz,
                           float y0,float h,uint32_t base) {
    rv3 v[8];
    float center_ground=terrain_yf(x,z);
    float base_offset=y0-center_ground;
    uint32_t j;
    uint32_t top=rgba_mix(base,1.15f),side=rgba_mix(base,0.84f),dark=rgba_mix(base,0.66f);
    v[0]=oriented_point(x,z,fx,fz,local_x-hx,local_z-hz,0.0f);
    v[1]=oriented_point(x,z,fx,fz,local_x+hx,local_z-hz,0.0f);
    v[2]=oriented_point(x,z,fx,fz,local_x+hx,local_z+hz,0.0f);
    v[3]=oriented_point(x,z,fx,fz,local_x-hx,local_z+hz,0.0f);
    for (j=0u;j<4u;++j) v[j].y=terrain_yf(v[j].x,v[j].z)+base_offset;
    for (j=0u;j<4u;++j) { v[j+4u]=v[j]; v[j+4u].y+=h; }
    tri(c,v[4],v[5],v[6],top);tri(c,v[4],v[6],v[7],top);
    tri(c,v[0],v[1],v[5],dark);tri(c,v[0],v[5],v[4],dark);
    tri(c,v[1],v[2],v[6],side);tri(c,v[1],v[6],v[5],side);
    tri(c,v[2],v[3],v[7],rgba_mix(side,1.05f));tri(c,v[2],v[7],v[6],rgba_mix(side,1.05f));
    tri(c,v[3],v[0],v[4],dark);tri(c,v[3],v[4],v[7],dark);
}

static void ground_shadow_layer(const rcam *c,float x,float z,float sx,float sz,float ox,float oz,float yoff,uint32_t col) {
    float x0=x-sx+ox,x1=x+sx+ox,z0=z-sz+oz,z1=z+sz+oz;
    quad(c,(rv3){x0,terrain_yf(x0,z0)+yoff,z0},
           (rv3){x1,terrain_yf(x1,z0)+yoff,z0},
           (rv3){x1,terrain_yf(x1,z1)+yoff,z1},
           (rv3){x0,terrain_yf(x0,z1)+yoff,z1},col);
}

static void ground_shadow(const rcam *c,float x,float z,float sx,float sz,float strength) {
    visual_palette p=palette();
    uint32_t outer=rgba_mix(p.sky_top,0.34f+strength*0.08f);
    uint32_t middle=rgba_mix(p.sky_top,0.25f+strength*0.06f);
    uint32_t inner=rgba_mix(p.sky_top,0.18f+strength*0.05f);
    float ox=0.16f*strength,oz=0.20f*strength;
    /* Shadows share the terrain height field with territory. A flat shadow quad could
     * dive below claimed ground on a slope; per-corner heights keep it above territory
     * fill (0.108) but below its luminous border/trail layers. */
    ground_shadow_layer(c,x,z,sx*1.22f,sz*1.22f,ox,oz,0.128f,outer);
    ground_shadow_layer(c,x,z,sx,sz,ox*0.65f,oz*0.65f,0.130f,middle);
    ground_shadow_layer(c,x,z,sx*0.66f,sz*0.66f,0.0f,0.0f,0.132f,inner);
}

static void actor_shadow(const rcam *c, float x, float z, float r) {
    ground_shadow(c,x,z,r*0.84f,r*0.61f,0.72f);
}

static void ground_disc8(const rcam *c,float x,float z,float r,float yoff,uint32_t col){
    static const float dx[8]={1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f,0.0f,0.7071f};
    static const float dz[8]={0.0f,0.7071f,1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f};
    rv3 center={x,terrain_yf(x,z)+yoff,z};uint32_t i;
    for(i=0u;i<8u;++i){uint32_t j=(i+1u)&7u;rv3 a={x+dx[i]*r,terrain_yf(x+dx[i]*r,z+dz[i]*r)+yoff,z+dz[i]*r};rv3 b={x+dx[j]*r,terrain_yf(x+dx[j]*r,z+dz[j]*r)+yoff,z+dz[j]*r};tri(c,center,a,b,col);}
}

static void ground_axis_connector(const rcam *c,float x,float z,int axis,float halfw,float yoff,uint32_t col){
    if(axis==0){
        quad(c,(rv3){x,terrain_yf(x,z-halfw)+yoff,z-halfw},(rv3){x+1.0f,terrain_yf(x+1.0f,z-halfw)+yoff,z-halfw},
             (rv3){x+1.0f,terrain_yf(x+1.0f,z+halfw)+yoff,z+halfw},(rv3){x,terrain_yf(x,z+halfw)+yoff,z+halfw},col);
    }else{
        quad(c,(rv3){x-halfw,terrain_yf(x-halfw,z)+yoff,z},(rv3){x+halfw,terrain_yf(x+halfw,z)+yoff,z},
             (rv3){x+halfw,terrain_yf(x+halfw,z+1.0f)+yoff,z+1.0f},(rv3){x-halfw,terrain_yf(x-halfw,z+1.0f)+yoff,z+1.0f},col);
    }
}

static void ground_diag_connector(const rcam *c,float x,float z,int zsign,float halfw,float yoff,uint32_t col){
    const float inv=0.70710678f;
    float ex=x+1.0f,ez=z+(float)zsign;
    float rx=-(float)zsign*inv*halfw,rz=inv*halfw;
    quad(c,(rv3){x-rx,terrain_yf(x-rx,z-rz)+yoff,z-rz},
           (rv3){x+rx,terrain_yf(x+rx,z+rz)+yoff,z+rz},
           (rv3){ex+rx,terrain_yf(ex+rx,ez+rz)+yoff,ez+rz},
           (rv3){ex-rx,terrain_yf(ex-rx,ez-rz)+yoff,ez-rz},col);
}

static void ground_segment(const rcam *c,float x0,float z0,float x1,float z1,
                           float halfw,float yoff,uint32_t col){
    float dx=x1-x0,dz=z1-z0,adx=dx<0.0f?-dx:dx,adz=dz<0.0f?-dz:dz;
    float major=adx>adz?adx:adz,minor=adx>adz?adz:adx;
    float length=major+minor*0.41421356f;
    float px,pz;
    if(length<0.001f)return;
    px=-dz*halfw/length;pz=dx*halfw/length;
    quad(c,(rv3){x0-px,terrain_yf(x0-px,z0-pz)+yoff,z0-pz},
           (rv3){x0+px,terrain_yf(x0+px,z0+pz)+yoff,z0+pz},
           (rv3){x1+px,terrain_yf(x1+px,z1+pz)+yoff,z1+pz},
           (rv3){x1-px,terrain_yf(x1-px,z1-pz)+yoff,z1-pz},col);
}

static void runner(const rcam *c, const odg_actor *a) {
    float x = odg_fx_to_float(a->x);
    float z = odg_fx_to_float(a->z);
    float fx = (float)a->face_x_q15 / (float)ODG_Q15_ONE;
    float fz = (float)a->face_z_q15 / (float)ODG_Q15_ONE;
    float gy = terrain_yf(x,z);
    float speed_ratio = (float)(a->speed_fx > 0 ? a->speed_fx : 0) / (float)ODG_PLAYER_SPEED_FX;
    float steer = (float)a->steer_q15 / (float)ODG_Q15_ONE;
    uint32_t phase=(uint32_t)((g_odg.tick*(uint64_t)(2u+(uint32_t)(speed_ratio*4.0f))+(uint64_t)a->id*17u)%UINT64_C(64));
    float lean=-steer*0.095f;
    const float half=0.42f;
    visual_palette p=palette();
    uint32_t signal=actor_base_color(a->id);
    uint32_t base=a->flash_ticks!=0u?0xdfe7e9ffu:rgba_lerp(p.building,signal,a->type==ODG_ACTOR_PLAYER?118u:92u);
    uint32_t front=a->type==ODG_ACTOR_PLAYER?rgba_mix(signal,1.12f):rgba_mix(signal,0.98f);
    if (fx == 0.0f && fz == 0.0f) fz = 1.0f;
    if (speed_ratio>1.25f) speed_ratio=1.25f;

    actor_shadow(c,x,z,0.58f);
    /* The game identity is deliberately cubic: presentation changes never alter the
     * actor's authoritative radius, footprint or movement state. */
    oriented_box_y(c,x,z,fx,fz,lean,0.0f,half,half,gy+0.08f,0.84f,base);
    oriented_box_y(c,x,z,fx,fz,lean,0.423f,0.24f,0.020f,gy+0.27f,0.37f,front);
    if(a->type==ODG_ACTOR_PLAYER){
        uint32_t belt=rgba_lerp(signal,0xdbe5e6ffu,42u+(phase<32u?phase:64u-phase));
        oriented_box_y(c,x,z,fx,fz,lean,0.0f,half+0.012f,half+0.012f,
                       gy+0.33f,0.055f,belt);
    }
}

static void clear_frame(const rcam *c) {
    uint32_t x,y;
    uint32_t w=g_odg.width,h=g_odg.height;
    visual_palette p=palette();
    uint32_t phase=(uint32_t)((g_odg.tick/900u+12u)%64u);
    uint32_t day_h=phase<32u?(phase<=16u?phase:32u-phase):0u;
    uint32_t light=150u+day_h*6u;if(light>246u)light=246u;
    uint32_t sky_top=rgba_mix(p.sky_top,(float)light/256.0f);
    uint32_t sky_horizon=rgba_mix(p.sky_horizon,0.56f+0.44f*(float)light/256.0f);
    uint32_t sky_mid=rgba_lerp(sky_top,sky_horizon,104u);
    uint32_t haze=rgba_lerp(sky_horizon,p.fog,68u);
    uint32_t split=(h*45u)/100u;
    int32_t sun_x=(int32_t)(w/2u),sun_y=(int32_t)(h/3u),sun_r=(int32_t)((h*14u)/100u),core_r=(int32_t)((h*15u)/1000u);
    int celestial_visible=0;
    g_daylight_q8=light;g_daylight_f=(float)light/256.0f;
    if(sun_r<20) sun_r=20;
    if(core_r<4) core_r=4;

    /* Celestial azimuth is WORLD-fixed. Rotating the camera changes where sun/moon
     * appears on screen; it no longer sticks to the same screen coordinate. */
    {
        static const int32_t d16[16][2]={{32767,0},{30273,12539},{23170,23170},{12539,30273},{0,32767},{-12539,30273},{-23170,23170},{-30273,12539},{-32767,0},{-30273,-12539},{-23170,-23170},{-12539,-30273},{0,-32767},{12539,-30273},{23170,-23170},{30273,-12539}};
        uint32_t ai=(phase>>2u)&15u;float sx=(float)d16[ai][0]/32767.0f,sz=(float)d16[ai][1]/32767.0f;
        float side,front;
        if(phase>=32u){sx=-sx;sz=-sz;}
        g_sun_world_x=sx;g_sun_world_z=sz;
        side=sx*c->right_x+sz*c->right_z;front=sx*c->forward_x+sz*c->forward_z;
        if(front>-0.38f){
            float elev=phase<32u?(float)day_h/16.0f:0.34f;
            sun_x=(int32_t)((float)w*0.5f+side*(float)w*0.44f);
            sun_y=(int32_t)((float)h*(0.51f-elev*0.36f));
            celestial_visible=1;
        }
    }

    odg_memset(g_odg_depth,0,(size_t)w*(size_t)h*sizeof(g_odg_depth[0]));
    for(y=0u;y<h;++y){
        uint32_t base,r,g,b;
        if(y<=split){uint32_t t=split!=0u?(y*256u)/split:0u;if(t<168u)base=rgba_lerp(sky_top,sky_mid,(t*256u)/168u);else base=rgba_lerp(sky_mid,sky_horizon,((t-168u)*256u)/88u);}
        else{uint32_t den=h>split?h-split:1u;uint32_t t=((y-split)*256u)/den;if(t>256u)t=256u;base=rgba_lerp(haze,p.fog,t/2u);}
        r=(base>>24)&255u;g=(base>>16)&255u;b=(base>>8)&255u;
        for(x=0u;x<w;++x){uint32_t i=(y*w+x)*4u;g_odg_framebuffer[i]=(uint8_t)r;g_odg_framebuffer[i+1u]=(uint8_t)g;g_odg_framebuffer[i+2u]=(uint8_t)b;g_odg_framebuffer[i+3u]=255u;}
    }
    if(celestial_visible){
        int32_t minx=sun_x-sun_r,maxx=sun_x+sun_r,miny=sun_y-sun_r,maxy=sun_y+sun_r,rr=sun_r*sun_r,cr=core_r*core_r;
        uint32_t glow=phase<32u?rgba_mix(p.accent,0.92f):0xaebdcbffu;
        if(minx<0) minx=0;
        if(miny<0) miny=0;
        if(maxx>=(int32_t)w) maxx=(int32_t)w-1;
        if(maxy>=(int32_t)h) maxy=(int32_t)h-1;
        for(y=(uint32_t)miny;y<=(uint32_t)maxy;++y)for(x=(uint32_t)minx;x<=(uint32_t)maxx;++x){int32_t dx=(int32_t)x-sun_x,dy=(int32_t)y-sun_y,d2=dx*dx+dy*dy;uint32_t i,col;if(d2>=rr)continue;i=(y*w+x)*4u;col=((uint32_t)g_odg_framebuffer[i]<<24)|((uint32_t)g_odg_framebuffer[i+1u]<<16)|((uint32_t)g_odg_framebuffer[i+2u]<<8)|255u;col=rgba_lerp(col,glow,(uint32_t)(((int64_t)(rr-d2)*58)/rr));if(d2<cr){uint32_t core=(uint32_t)(((int64_t)(cr-d2)*150)/cr)+46u;if(core>218u)core=218u;col=rgba_lerp(col,0xf4eee2ffu,core);}g_odg_framebuffer[i]=(uint8_t)(col>>24);g_odg_framebuffer[i+1u]=(uint8_t)(col>>16);g_odg_framebuffer[i+2u]=(uint8_t)(col>>8);g_odg_framebuffer[i+3u]=255u;}
    }
    /* Night reference stars are world-space bearings, not a screen overlay. They move
     * across the viewport only when the camera/world-time changes, making it visually
     * obvious that the sky is not glued to the camera. */
    if(phase>=30u){
        static const int32_t sd[16][2]={{32767,0},{30273,12539},{23170,23170},{12539,30273},{0,32767},{-12539,30273},{-23170,23170},{-30273,12539},{-32767,0},{-30273,-12539},{-23170,-23170},{-12539,-30273},{0,-32767},{12539,-30273},{23170,-23170},{30273,-12539}};
        static const uint8_t sy[12]={13,21,9,28,17,34,12,25,7,31,19,14};
        uint32_t si;
        for(si=0u;si<12u;++si){
            uint32_t ai=(si*5u+3u+(phase>>3u))&15u;
            float sx=(float)sd[ai][0]/32767.0f,sz=(float)sd[ai][1]/32767.0f;
            float side=sx*c->right_x+sz*c->right_z,front=sx*c->forward_x+sz*c->forward_z;
            if(front>-0.18f){
                int32_t px=(int32_t)((float)w*0.5f+side*(float)w*0.47f);
                int32_t py=(int32_t)((float)h*((float)sy[si]/100.0f));
                if(px>=1&&px+1<(int32_t)w&&py>=1&&py+1<(int32_t)h){
                    uint32_t alpha=phase<36u?(phase-30u)*28u:168u;uint32_t yy,xx;
                    if(alpha>180u)alpha=180u;
                    for(yy=(uint32_t)(py-1);yy<=(uint32_t)(py+1);++yy)for(xx=(uint32_t)(px-1);xx<=(uint32_t)(px+1);++xx){
                        uint32_t ii=(yy*w+xx)*4u;uint32_t old=((uint32_t)g_odg_framebuffer[ii]<<24)|((uint32_t)g_odg_framebuffer[ii+1u]<<16)|((uint32_t)g_odg_framebuffer[ii+2u]<<8)|255u;
                        uint32_t aa=(xx==(uint32_t)px&&yy==(uint32_t)py)?alpha:alpha/3u;uint32_t cc=rgba_lerp(old,0xeaf6ffffu,aa);
                        g_odg_framebuffer[ii]=(uint8_t)(cc>>24);g_odg_framebuffer[ii+1u]=(uint8_t)(cc>>16);g_odg_framebuffer[ii+2u]=(uint8_t)(cc>>8);g_odg_framebuffer[ii+3u]=255u;
                    }
                }
            }
        }
    }

    g_odg.render_triangles=0u;g_odg.render_pixels_touched=0u;
}


static void ground_and_grid(const rcam *c) {
    uint32_t z;
    float half=(float)ODG_WORLD_HALF_CELLS;
    visual_palette p=palette();
    /* Water/void under the organic country silhouette. */
    quad(c,(rv3){-half,-0.32f,-half},(rv3){half,-0.32f,-half},
         (rv3){half,-0.32f,half},(rv3){-half,-0.32f,half},p.water);
    /* Sparse animated glints give the ocean scale without textures. Land is rendered
     * afterwards and naturally occludes them in the shared z-buffer. */
    for(z=0u;z<9u;++z){
        uint32_t h=visual_hash2(z,(uint32_t)(g_odg.tick/16u));
        float zz=-half+4.0f+(float)((z*13u+(uint32_t)(g_odg.tick/12u))%(ODG_GRID_SIZE-8u));
        float gx=-half+2.0f+(float)(h%(ODG_GRID_SIZE-16u));
        float len=3.5f+(float)((h>>8u)&7u)*0.72f;
        float yy=-0.305f+(float)(z&1u)*0.004f;
        quad(c,(rv3){gx,yy,zz},(rv3){gx+len,yy,zz},
             (rv3){gx+len,yy,zz+0.025f},(rv3){gx,yy,zz+0.025f},rgba_mix(p.water_glint,0.58f));
    }

    /* Stability rule: land, territory and trails all derive from the SAME 1x1 terrain
     * tessellation. The old renderer merged long land runs but shorter territory runs;
     * on curved height fields the two planes could cross and a claimed patch appeared
     * to blink as the camera moved. Shared cell geometry makes that impossible. */
    for (z=0u;z<ODG_GRID_SIZE;++z) {
        uint32_t x;
        for (x=0u;x<ODG_GRID_SIZE;++x) {
            uint32_t cell=z*ODG_GRID_SIZE+x;
            float x0,x1,z0,z1;
            float h00,h10,h11,h01,hc,sx,sz,light,grain;
            uint32_t noise,base,shade;
            if (g_odg.playable[cell]==0u) continue;
            if (!world_point_maybe_visible(c,-(float)ODG_WORLD_HALF_CELLS+(float)x+0.5f,
                                             -(float)ODG_WORLD_HALF_CELLS+(float)z+0.5f,0.78f)) continue;
            x0=-(float)ODG_WORLD_HALF_CELLS+(float)x; x1=x0+1.0f;
            z0=-(float)ODG_WORLD_HALF_CELLS+(float)z; z1=z0+1.0f;
            h00=terrain_yf(x0,z0); h10=terrain_yf(x1,z0);
            h11=terrain_yf(x1,z1); h01=terrain_yf(x0,z1);
            hc=(h00+h10+h11+h01)*0.25f;
            sx=((h10+h11)-(h00+h01))*0.5f;
            sz=((h01+h11)-(h00+h10))*0.5f;
            /* World-fixed daylight rotates over time. Camera yaw does not rotate the
             * illumination; only the day/night phase changes the world-space sun vector. */
            light=(0.72f+0.28f*g_daylight_f)-sx*g_sun_world_x*0.19f-sz*g_sun_world_z*0.19f;
            if (light<0.76f) light=0.76f;
            if (light>1.12f) light=1.12f;
            noise=visual_hash2(x,z);
            grain=((float)((noise>>27u)&31u)-15.5f)*0.0022f;
            {
                int coast=(x==0u || x+1u==ODG_GRID_SIZE || z==0u || z+1u==ODG_GRID_SIZE ||
                           g_odg.playable[cell-1u]==0u || g_odg.playable[cell+1u]==0u ||
                           g_odg.playable[cell-ODG_GRID_SIZE]==0u || g_odg.playable[cell+ODG_GRID_SIZE]==0u);
                if (coast) base=p.coast;
                else base=hc>1.70f?p.land_high:(hc>0.80f?p.land_mid:p.land_low);
            }
            shade=rgba_mix(base,light+grain);
            if(g_odg.territory[cell]!=ODG_OWNER_NONE)
                shade=territory_color(ODG_ID_FROM_OWNER(g_odg.territory[cell]),shade);
            quad(c,(rv3){x0,h00-0.050f,z0},
                   (rv3){x1,h10-0.050f,z0},
                   (rv3){x1,h11-0.050f,z1},
                   (rv3){x0,h01-0.050f,z1},shade);
        }
    }

    /* Coastline is the dominant world boundary. */
    for (z=0u;z<ODG_GRID_SIZE;++z) {
        uint32_t x;
        for (x=0u;x<ODG_GRID_SIZE;++x) {
            uint32_t cidx=z*ODG_GRID_SIZE+x;
            float x0,x1,z0,z1;
            if (g_odg.playable[cidx]==0u) continue;
            if (!world_point_maybe_visible(c,-(float)ODG_WORLD_HALF_CELLS+(float)x+0.5f,
                                             -(float)ODG_WORLD_HALF_CELLS+(float)z+0.5f,0.78f)) continue;
            x0=-(float)ODG_WORLD_HALF_CELLS+(float)x; x1=x0+1.0f;
            z0=-(float)ODG_WORLD_HALF_CELLS+(float)z; z1=z0+1.0f;
            if (x==0u || g_odg.playable[cidx-1u]==0u) {float a=terrain_yf(x0,z0),b=terrain_yf(x0,z1);quad(c,(rv3){x0,-0.30f,z0},(rv3){x0,-0.30f,z1},(rv3){x0,b-0.02f,z1},(rv3){x0,a-0.02f,z0},rgba_mix(p.rock,0.72f));line_overlay(c,(rv3){x0,a+0.015f,z0},(rv3){x0,b+0.015f,z1},p.coast_edge);}
            if (x+1u==ODG_GRID_SIZE || g_odg.playable[cidx+1u]==0u) {float a=terrain_yf(x1,z0),b=terrain_yf(x1,z1);quad(c,(rv3){x1,-0.30f,z0},(rv3){x1,a-0.02f,z0},(rv3){x1,b-0.02f,z1},(rv3){x1,-0.30f,z1},rgba_mix(p.rock,0.80f));line_overlay(c,(rv3){x1,a+0.015f,z0},(rv3){x1,b+0.015f,z1},p.coast_edge);}
            if (z==0u || g_odg.playable[cidx-ODG_GRID_SIZE]==0u) {float a=terrain_yf(x0,z0),b=terrain_yf(x1,z0);quad(c,(rv3){x0,-0.30f,z0},(rv3){x0,a-0.02f,z0},(rv3){x1,b-0.02f,z0},(rv3){x1,-0.30f,z0},rgba_mix(p.rock,0.66f));line_overlay(c,(rv3){x0,a+0.015f,z0},(rv3){x1,b+0.015f,z0},p.coast_edge);}
            if (z+1u==ODG_GRID_SIZE || g_odg.playable[cidx+ODG_GRID_SIZE]==0u) {float a=terrain_yf(x0,z1),b=terrain_yf(x1,z1);quad(c,(rv3){x0,-0.30f,z1},(rv3){x1,-0.30f,z1},(rv3){x1,b-0.02f,z1},(rv3){x0,a-0.02f,z1},rgba_mix(p.rock,0.86f));line_overlay(c,(rv3){x0,a+0.015f,z1},(rv3){x1,b+0.015f,z1},p.coast_edge);}
        }
    }
}

static void render_routes(const rcam *c) {
    uint32_t z;
    visual_palette p=palette();
    uint32_t lane=rgba_mix(p.road,1.17f);
    uint32_t guide=rgba_mix(p.accent,0.70f);
    for (z=0u;z<ODG_GRID_SIZE;++z) {
        uint32_t x;
        for (x=0u;x<ODG_GRID_SIZE;++x) {
            uint32_t cell=z*ODG_GRID_SIZE+x;
            int32_t wx=(int32_t)x-ODG_WORLD_HALF_CELLS;
            int32_t wz=(int32_t)z-ODG_WORLD_HALF_CELLS;
            int vertical=odg_abs_i32(wx)<=1 && wz>-48 && wz<49;
            int horizontal=odg_abs_i32(wz+4)<=1 && wx>-52 && wx<53;
            int diagonal=odg_abs_i32((wx*2)-wz-10)<=2 && wx>-34 && wx<35;
            float x0,z0,x1,z1,y00,y10,y11,y01;
            if (g_odg.playable[cell]==0u || (!vertical && !horizontal && !diagonal)) continue;
            if (!world_point_maybe_visible(c,-(float)ODG_WORLD_HALF_CELLS+(float)x+0.5f,
                                             -(float)ODG_WORLD_HALF_CELLS+(float)z+0.5f,0.80f)) continue;
            x0=-(float)ODG_WORLD_HALF_CELLS+(float)x;
            z0=-(float)ODG_WORLD_HALF_CELLS+(float)z;
            x1=x0+1.0f;z1=z0+1.0f;
            y00=terrain_yf(x0,z0)+0.012f;y10=terrain_yf(x1,z0)+0.012f;
            y11=terrain_yf(x1,z1)+0.012f;y01=terrain_yf(x0,z1)+0.012f;
            quad(c,(rv3){x0,y00,z0},(rv3){x1,y10,z0},
                 (rv3){x1,y11,z1},(rv3){x0,y01,z1},p.road);
            quad(c,(rv3){x0+0.075f,terrain_yf(x0+0.075f,z0+0.075f)+0.019f,z0+0.075f},
                 (rv3){x1-0.075f,terrain_yf(x1-0.075f,z0+0.075f)+0.019f,z0+0.075f},
                 (rv3){x1-0.075f,terrain_yf(x1-0.075f,z1-0.075f)+0.019f,z1-0.075f},
                 (rv3){x0+0.075f,terrain_yf(x0+0.075f,z1-0.075f)+0.019f,z1-0.075f},lane);
            if (((x+z)&7u)==0u) {
                if (vertical) line_overlay(c,(rv3){(x0+x1)*0.5f,terrain_yf((x0+x1)*0.5f,z0)+0.032f,z0+0.17f},
                                              (rv3){(x0+x1)*0.5f,terrain_yf((x0+x1)*0.5f,z1)+0.032f,z1-0.17f},guide);
                else if (horizontal) line_overlay(c,(rv3){x0+0.17f,terrain_yf(x0,(z0+z1)*0.5f)+0.032f,(z0+z1)*0.5f},
                                                   (rv3){x1-0.17f,terrain_yf(x1,(z0+z1)*0.5f)+0.032f,(z0+z1)*0.5f},guide);
                else line_overlay(c,(rv3){x0+0.17f,terrain_yf(x0+0.17f,z0+0.17f)+0.032f,z0+0.17f},
                                    (rv3){x1-0.17f,terrain_yf(x1-0.17f,z1-0.17f)+0.032f,z1-0.17f},guide);
            }
        }
    }
}

static void render_territory_edges(const rcam *c) {
    uint32_t z;
    for (z = 0u; z < ODG_GRID_SIZE; ++z) {
        uint32_t x;
        for (x = 0u; x < ODG_GRID_SIZE; ++x) {
            uint32_t cell = z * ODG_GRID_SIZE + x;
            uint8_t owner = g_odg.territory[cell];
            uint32_t id;
            uint32_t col;
            float x0, x1, z0, z1;
            if (owner == ODG_OWNER_NONE) continue;
            if (!world_point_maybe_visible(c,-(float)ODG_WORLD_HALF_CELLS+(float)x+0.5f,
                                             -(float)ODG_WORLD_HALF_CELLS+(float)z+0.5f,0.82f)) continue;
            id = ODG_ID_FROM_OWNER(owner);
            col=rgba_mix(actor_base_color(id),id==ODG_PLAYER_ID?0.96f:0.78f);
            x0 = -(float)ODG_WORLD_HALF_CELLS + (float)x;
            x1 = x0 + 1.0f;
            z0 = -(float)ODG_WORLD_HALF_CELLS + (float)z;
            z1 = z0 + 1.0f;
            if (x == 0u || g_odg.territory[cell - 1u] != owner)
                line_overlay(c, (rv3){x0,terrain_yf(x0,z0+0.08f)+0.068f,z0+0.08f}, (rv3){x0,terrain_yf(x0,z1-0.08f)+0.068f,z1-0.08f}, col);
            if (x + 1u == ODG_GRID_SIZE || g_odg.territory[cell + 1u] != owner)
                line_overlay(c, (rv3){x1,terrain_yf(x1,z0+0.08f)+0.068f,z0+0.08f}, (rv3){x1,terrain_yf(x1,z1-0.08f)+0.068f,z1-0.08f}, col);
            if (z == 0u || g_odg.territory[cell - ODG_GRID_SIZE] != owner)
                line_overlay(c, (rv3){x0+0.08f,terrain_yf(x0+0.08f,z0)+0.068f,z0}, (rv3){x1-0.08f,terrain_yf(x1-0.08f,z0)+0.068f,z0}, col);
            if (z + 1u == ODG_GRID_SIZE || g_odg.territory[cell + ODG_GRID_SIZE] != owner)
                line_overlay(c, (rv3){x0+0.08f,terrain_yf(x0+0.08f,z1)+0.068f,z1}, (rv3){x1-0.08f,terrain_yf(x1-0.08f,z1)+0.068f,z1}, col);
        }
    }
}

static void render_trails(const rcam *c) {
    uint32_t z;
    for(z=0u;z<ODG_GRID_SIZE;++z){uint32_t x;for(x=0u;x<ODG_GRID_SIZE;++x){
        uint32_t cell=z*ODG_GRID_SIZE+x;uint8_t owner=g_odg.trail_owner[cell];float cx,cz;uint32_t id,outer,inner;
        if(owner==ODG_OWNER_NONE||g_odg.playable[cell]==0u)continue;
        cx=-(float)ODG_WORLD_HALF_CELLS+(float)x+0.5f;cz=-(float)ODG_WORLD_HALF_CELLS+(float)z+0.5f;
        if(!world_point_maybe_visible(c,cx,cz,0.72f))continue;
        id=ODG_ID_FROM_OWNER(owner);outer=rgba_mix(actor_base_color(id),0.46f);inner=trail_color(id);
        ground_disc8(c,cx,cz,0.13f,0.078f,outer);ground_disc8(c,cx,cz,0.045f,0.091f,inner);
        if(x+1u<ODG_GRID_SIZE&&g_odg.trail_owner[cell+1u]==owner){ground_axis_connector(c,cx,cz,0,0.13f,0.078f,outer);ground_axis_connector(c,cx,cz,0,0.045f,0.091f,inner);}
        if(z+1u<ODG_GRID_SIZE&&g_odg.trail_owner[cell+ODG_GRID_SIZE]==owner){ground_axis_connector(c,cx,cz,1,0.13f,0.078f,outer);ground_axis_connector(c,cx,cz,1,0.045f,0.091f,inner);}
        /* A diagonal is real only when there is no orthogonal bridge. Connecting every
         * touching diagonal closed A-B-C corners into little /\ triangles and produced
         * the visible zigzag teeth. Orthogonal paths now win; isolated diagonals remain. */
        if(x+1u<ODG_GRID_SIZE&&z+1u<ODG_GRID_SIZE&&
           g_odg.trail_owner[cell+ODG_GRID_SIZE+1u]==owner&&
           g_odg.trail_owner[cell+1u]!=owner&&g_odg.trail_owner[cell+ODG_GRID_SIZE]!=owner){
            ground_diag_connector(c,cx,cz,1,0.11f,0.078f,outer);
            ground_diag_connector(c,cx,cz,1,0.034f,0.091f,inner);
        }
        if(x+1u<ODG_GRID_SIZE&&z>0u&&g_odg.trail_owner[cell-ODG_GRID_SIZE+1u]==owner&&
           g_odg.trail_owner[cell+1u]!=owner&&g_odg.trail_owner[cell-ODG_GRID_SIZE]!=owner){
            ground_diag_connector(c,cx,cz,-1,0.11f,0.078f,outer);
            ground_diag_connector(c,cx,cz,-1,0.034f,0.091f,inner);
        }
    }}
    for(z=0u;z<ODG_MAX_ACTORS;++z){const odg_actor *a=&g_odg.actors[z];if(!a->active||a->hp==0u||!a->trail_active||a->last_cell>=ODG_CELL_COUNT)continue;{
        float cx=odg_fx_to_float(odg_cell_center_x(a->last_cell)),cz=odg_fx_to_float(odg_cell_center_z(a->last_cell));float ax=odg_fx_to_float(a->x),az=odg_fx_to_float(a->z);
        uint32_t outer=rgba_mix(actor_base_color(a->id),0.46f),inner=trail_color(a->id);
        ground_segment(c,cx,cz,ax,az,0.13f,0.078f,outer);
        ground_segment(c,cx,cz,ax,az,0.045f,0.091f,inner);
    }}
}


static void render_boundaries(const rcam *c) {
    (void)c; /* API v8 uses an irregular coastline instead of a square wall. */
}

static void render_building(const rcam *c,float x,float z,float hx,float hz,float h,uint32_t base) {
    float y=terrain_yf(x,z);
    float upper=h*0.12f;
    uint32_t wall=rgba_mix(base,0.90f);
    uint32_t roof=rgba_mix(base,0.62f);
    uint32_t trim=rgba_mix(base,1.02f);
    uint32_t glass=rgba_mix(palette().glass,0.82f);
    uint32_t band;
    if (upper<0.28f) upper=0.28f;
    ground_shadow(c,x,z,hx*1.04f,hz*1.04f,1.0f);
    box_y(c,x,z,hx*1.08f,hz*1.08f,y,0.16f,rgba_mix(base,0.42f));
    box_y(c,x,z,hx,hz,y+0.14f,h,wall);
    /* Recessed upper mass and a thin coping line establish architectural scale
     * without turning the skyline into stacked toy blocks. */
    box_y(c,x,z,hx*0.78f,hz*0.76f,y+h+0.14f,upper,roof);
    box_y(c,x,z,hx*0.84f,hz*0.82f,y+h+upper+0.14f,0.055f,trim);
    if (hx>0.70f && hz>0.70f) {
        for(band=0u;band<3u;++band){
            float wy=y+0.40f+(h-0.70f)*(float)(band+1u)/4.0f;
            box_y(c,x,z+hz+0.022f,hx*0.70f,0.022f,wy,0.095f,glass);
            box_y(c,x-hx-0.022f,z,0.022f,hz*0.68f,wy+0.035f,0.075f,rgba_mix(glass,0.86f));
        }
        box_y(c,x,z+hz+0.028f,hx*0.10f,0.035f,y+0.14f,0.70f,rgba_mix(base,0.46f));
        if (h>4.0f) {
            uint32_t pulse=(uint32_t)((g_odg.tick+(uint64_t)((x+z+128.0f)*5.0f))%UINT64_C(90));
            float glow=(pulse<45u?(float)pulse:(float)(90u-pulse))/45.0f;
            box_y(c,x,z,0.045f,0.045f,y+h+upper+0.19f,0.52f,rgba_mix(palette().accent,0.62f+glow*0.18f));
        }
    }
}

static void render_distant_landmarks(const rcam *c) {
    static const float towers[][5] = {
        {-54.0f,-42.0f,1.8f,1.6f,6.2f},{-48.0f,-46.0f,1.5f,1.8f,8.0f},
        { 52.0f, 39.0f,1.9f,1.7f,6.8f},{ 45.0f, 45.0f,1.4f,1.5f,8.8f},
        {-40.0f, 48.0f,2.0f,1.5f,5.6f},{ 36.0f,-49.0f,1.8f,1.5f,6.4f},
        {-57.0f,  7.0f,1.5f,1.8f,7.2f},{ 56.0f, -9.0f,1.5f,1.7f,7.6f}
    };
    uint32_t i;
    visual_palette p=palette();
    for (i=0u;i<(uint32_t)(sizeof(towers)/sizeof(towers[0]));++i) {
        uint32_t col=(i&1u)?p.building:p.building_alt;
        render_building(c,towers[i][0],towers[i][1],towers[i][2],towers[i][3],towers[i][4],col);
    }
    /* Orientation beacons are thin, not giant blocks, so they read as distant civic
     * landmarks while preserving a believable scale against the runner. */
    render_building(c,-51.0f,-51.0f,0.85f,0.85f,8.0f,p.building_alt);
    render_building(c, 51.0f,-51.0f,0.85f,0.85f,7.0f,p.building);
    render_building(c,-51.0f, 51.0f,0.85f,0.85f,7.4f,p.building);
    render_building(c, 51.0f, 51.0f,0.85f,0.85f,8.4f,p.building_alt);
}


static void render_tree(const rcam *c,float x,float z,float s) {
    float y=terrain_yf(x,z);
    visual_palette p=palette();
    ground_shadow(c,x,z,0.47f*s,0.38f*s,0.62f);
    box_y(c,x,z,0.10f*s,0.10f*s,y+0.07f,0.82f*s,p.trunk);
    box_y(c,x,z,0.48f*s,0.43f*s,y+0.66f*s,0.58f*s,p.leaf_low);
    box_y(c,x+0.10f*s,z-0.06f*s,0.34f*s,0.31f*s,y+1.12f*s,0.42f*s,p.leaf_high);
    octa(c,x-0.16f*s,z+0.08f*s,0.22f*s,y+1.20f*s,0.34f*s,rgba_mix(p.leaf_high,1.08f));
}

static void render_world_obstacle(const rcam *c,const odg_obstacle *o) {
    float x=odg_fx_to_float(o->x),z=odg_fx_to_float(o->z);
    float hx=odg_fx_to_float(o->hx),hz=odg_fx_to_float(o->hz),h=odg_fx_to_float(o->height_fx);
    float y=terrain_yf(x,z);
    visual_palette p=palette();
    if(o->palette==2u){
        float s=(hx<hz?hx:hz)*0.72f;
        if (s < 0.75f) s = 0.75f;
        if (s > 1.45f) s = 1.45f;
        render_tree(c,x-hx*0.38f,z-hz*0.22f,s);
        render_tree(c,x+hx*0.30f,z+hz*0.24f,s*0.88f);
        if(hx>1.8f||hz>1.8f)render_tree(c,x+hx*0.12f,z-hz*0.48f,s*0.72f);
    }else if(o->palette==1u){
        uint32_t col=p.rock;
        box_y(c,x,z,hx*0.82f,hz*0.78f,y,h*0.55f,col);
        box_y(c,x-hx*0.35f,z+hz*0.18f,hx*0.46f,hz*0.42f,y,h*0.39f,rgba_mix(col,0.82f));
        box_y(c,x+hx*0.30f,z-hz*0.28f,hx*0.38f,hz*0.35f,y,h*0.46f,rgba_mix(col,1.12f));
        box_y(c,x+hx*0.08f,z+hz*0.06f,hx*0.28f,hz*0.22f,y+h*0.42f,h*0.16f,rgba_mix(col,1.28f));
    }else{
        render_building(c,x,z,hx,hz,h,p.building);
    }
}

static uint32_t turret_color(const odg_turret *t) {
    if (!t || t->owner==ODG_TURRET_NEUTRAL) return palette().neutral_turret;
    return actor_base_color(ODG_ID_FROM_OWNER(t->owner));
}

static void turret_head_direction(const odg_turret *t,int32_t *out_x,int32_t *out_z) {
    static const int32_t dirs[8][2]={{0,32767},{23170,23170},{32767,0},{23170,-23170},{0,-32767},{-23170,-23170},{-32767,0},{-23170,23170}};
    if(!t||!out_x||!out_z)return;
    if(t->carried_by<ODG_MAX_ACTORS){*out_x=g_odg.actors[t->carried_by].face_x_q15;*out_z=g_odg.actors[t->carried_by].face_z_q15;}
    else if(t->owner!=ODG_TURRET_NEUTRAL && (t->head_x_q15!=0||t->head_z_q15!=0)){*out_x=t->head_x_q15;*out_z=t->head_z_q15;}
    else{uint32_t d=(uint32_t)((g_odg.tick/96u+(uint64_t)t->id*3u)%UINT64_C(8));*out_x=dirs[d][0];*out_z=dirs[d][1];}
}

static void billboard_segment(const rcam *c,float x,float y,float z,float ax,float ay,float bx,float by,float scale,uint32_t col){
    line_overlay(c,(rv3){x+c->right_x*ax*scale,y+ay*scale,z+c->right_z*ax*scale},(rv3){x+c->right_x*bx*scale,y+by*scale,z+c->right_z*bx*scale},col);
}
static void billboard_digit(const rcam *c,float x,float y,float z,uint32_t d,float scale,uint32_t col){
    static const uint8_t mask[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};uint8_t m=d<10u?mask[d]:0u;
    if(m&1u) billboard_segment(c,x,y,z,-.35f,.8f,.35f,.8f,scale,col);
    if(m&2u) billboard_segment(c,x,y,z,.35f,.8f,.35f,0.f,scale,col);
    if(m&4u) billboard_segment(c,x,y,z,.35f,0.f,.35f,-.8f,scale,col);
    if(m&8u) billboard_segment(c,x,y,z,-.35f,-.8f,.35f,-.8f,scale,col);
    if(m&16u) billboard_segment(c,x,y,z,-.35f,0.f,-.35f,-.8f,scale,col);
    if(m&32u) billboard_segment(c,x,y,z,-.35f,.8f,-.35f,0.f,scale,col);
    if(m&64u) billboard_segment(c,x,y,z,-.35f,0.f,.35f,0.f,scale,col);
}
static void billboard_number2(const rcam *c,float x,float y,float z,uint32_t v,float scale,uint32_t col){if(v>=10u){billboard_digit(c,x-.22f*scale,y,z,(v/10u)%10u,scale,col);billboard_digit(c,x+.22f*scale,y,z,v%10u,scale,col);}else billboard_digit(c,x,y,z,v,scale,col);}
static void turret_ammo_billboard(const rcam *c,const odg_turret *t,float x,float z,uint32_t col){
    const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];float y;
    if(t->owner!=ODG_OWNER_FROM_ID(ODG_PLAYER_ID)||t->carried_by!=ODG_TURRET_NONE||p->hp==0u)return;
    if(odg_dist2(p->x,p->z,t->x,t->z)>(int64_t)ODG_TURRET_AMMO_LABEL_RANGE_FX*ODG_TURRET_AMMO_LABEL_RANGE_FX)return;
    y=terrain_yf(x,z)+2.05f;billboard_number2(c,x-.18f,y,z,t->ammo,0.32f,col);billboard_segment(c,x,y,z,-.05f,-.65f,.05f,.65f,0.32f,col);billboard_number2(c,x+.18f,y,z,t->max_ammo,0.32f,col);
}


static void render_turrets(const rcam *c) {
    uint32_t i;
    visual_palette p=palette();
    for (i=0u;i<g_odg.turret_count;++i) {
        const odg_turret *t=&g_odg.turrets[i];
        float x,z,fx,fz;
        int32_t hdx=0,hdz=ODG_Q15_ONE;
        uint32_t col;
        float ammo_ratio;
        if (!t->active) continue;
        x=odg_fx_to_float(t->x);z=odg_fx_to_float(t->z);col=turret_color(t);
        ammo_ratio=t->max_ammo!=0u?(float)t->ammo/(float)t->max_ammo:0.0f;
        turret_head_direction(t,&hdx,&hdz);
        fx=(float)hdx/(float)ODG_Q15_ONE;fz=(float)hdz/(float)ODG_Q15_ONE;
        {
            float ground;
            float y0;
            uint32_t core_phase=(uint32_t)((g_odg.tick+(uint64_t)i*11u)%UINT64_C(72));
            float core_wave=(core_phase<36u?(float)core_phase:(float)(72u-core_phase))/36.0f;
            if (t->carried_by<ODG_MAX_ACTORS) {
                const odg_actor *carrier=&g_odg.actors[t->carried_by];
                float ax=odg_fx_to_float(carrier->x),az=odg_fx_to_float(carrier->z);
                float cfx=(float)carrier->face_x_q15/(float)ODG_Q15_ONE;
                float cfz=(float)carrier->face_z_q15/(float)ODG_Q15_ONE;
                float rx=cfz,rz=-cfx;
                x=ax+rx*0.43f-cfx*0.10f;
                z=az+rz*0.43f-cfz*0.10f;
                ground=terrain_yf(ax,az);y0=ground+0.70f;
                oriented_box_y(c,x,z,cfx,cfz,0.0f,0.0f,0.25f,0.22f,y0,0.20f,rgba_mix(col,0.62f));
                oriented_box_y(c,x,z,cfx,cfz,0.0f,0.0f,0.085f,0.085f,y0,0.48f,rgba_mix(col,0.84f));
                oriented_box_y(c,x,z,cfx,cfz,0.0f,0.10f,0.26f,0.13f,y0+0.34f,0.18f,col);
            } else {
                ground=terrain_yf(x,z);y0=ground;
                ground_shadow(c,x,z,0.55f,0.48f,0.82f);
                box_y(c,x,z,0.52f,0.52f,y0+0.04f,0.12f,rgba_mix(col,0.42f));
                octa(c,x,z,0.50f,y0+0.10f,0.36f,rgba_mix(col,0.64f));
                box_y(c,x,z,0.16f,0.16f,y0+0.20f,0.95f,rgba_mix(col,0.82f));
                oriented_box_y(c,x,z,fx,fz,0.0f,0.08f,0.42f,0.27f,y0+1.04f,0.31f,col);
                oriented_box_y(c,x,z,fx,fz,0.0f,0.54f,0.105f,0.40f,y0+1.13f,0.13f,rgba_mix(col,1.14f));
                octa(c,x,z,0.13f,y0+0.75f,0.30f,rgba_mix(p.accent,0.88f+core_wave*0.30f));
                if (ammo_ratio>0.0f) box_y(c,x+0.38f,z-0.36f,0.07f,0.07f,y0+0.05f,0.25f+ammo_ratio*0.70f,p.ammo);
            }
        }
        if (t->target_kind!=ODG_TURRET_TARGET_NONE && t->aim_ticks!=0u && t->last_target_cell<ODG_CELL_COUNT) {
            uint32_t warn=((t->aim_ticks/10u)&1u)!=0u?rgba_mix(p.ammo,0.85f):rgba_mix(p.ammo,1.22f);
            line_overlay(c,(rv3){x,terrain_yf(x,z)+1.42f,z},
                         (rv3){odg_fx_to_float(odg_cell_center_x(t->last_target_cell)),
                               terrain_yf(odg_fx_to_float(odg_cell_center_x(t->last_target_cell)),odg_fx_to_float(odg_cell_center_z(t->last_target_cell)))+0.17f,
                               odg_fx_to_float(odg_cell_center_z(t->last_target_cell))},warn);
        }
        if (t->beam_ticks!=0u && t->last_target_cell<ODG_CELL_COUNT) {
            line_overlay(c,(rv3){x,terrain_yf(x,z)+1.36f,z},
                         (rv3){odg_fx_to_float(odg_cell_center_x(t->last_target_cell)),
                               terrain_yf(odg_fx_to_float(odg_cell_center_x(t->last_target_cell)),odg_fx_to_float(odg_cell_center_z(t->last_target_cell)))+0.11f,
                               odg_fx_to_float(odg_cell_center_z(t->last_target_cell))},rgba_mix(p.ammo,1.25f));
        }
        turret_ammo_billboard(c,t,x,z,rgba_mix(p.ammo,1.18f));
    }
}

static void render_ammo_crates(const rcam *c) {
    uint32_t i;
    visual_palette p=palette();
    for (i=0u;i<g_odg.ammo_crate_count;++i) {
        const odg_ammo_crate *a=&g_odg.ammo_crates[i];
        float x,z,y0,bob;
        uint32_t phase;
        if (!a->active) continue;
        x=odg_fx_to_float(a->x);z=odg_fx_to_float(a->z);
        phase=(uint32_t)((g_odg.tick+(uint64_t)i*13u)%UINT64_C(80));
        if (phase>40u) phase=80u-phase;
        bob=(float)phase/40.0f*0.12f;
        y0=terrain_yf(x,z)+(a->carried_by!=UINT32_MAX?0.50f:0.14f+bob);
        if (a->carried_by==UINT32_MAX) ground_shadow(c,x,z,0.30f,0.24f,0.48f);
        box_y(c,x,z,0.30f,0.24f,y0,0.34f,rgba_mix(p.ammo,0.76f));
        box_y(c,x,z,0.08f,0.32f,y0+0.05f,0.25f,p.ammo);
        octa(c,x,z,0.10f,y0+0.30f,0.27f,rgba_mix(p.accent,1.10f));
    }
}

static void render_chips(const rcam *c){
    uint32_t i;visual_palette p=palette();uint32_t col=rgba_lerp(p.accent,0xc983ffffu,150u);
    for(i=0u;i<g_odg.chip_count;++i){const odg_chip *ch=&g_odg.chips[i];float x,z,y,bob;uint32_t ph;if(!ch->active)continue;x=odg_fx_to_float(ch->x);z=odg_fx_to_float(ch->z);ph=(uint32_t)((g_odg.tick+(uint64_t)i*17u)%64u);if(ph>32u)ph=64u-ph;bob=(float)ph/32.0f*.10f;y=terrain_yf(x,z)+(ch->carried_by<ODG_MAX_ACTORS?.62f:.18f+bob);if(ch->carried_by==UINT32_MAX)ground_shadow(c,x,z,.24f,.18f,.34f);oriented_box_y(c,x,z,0.0f,1.0f,0.0f,0.0f,.24f,.18f,y,.11f,rgba_mix(col,.72f));oriented_box_y(c,x,z,0.0f,1.0f,0.0f,0.0f,.12f,.27f,y+.06f,.06f,col);}
}


static uint32_t decor_hash(uint32_t x,uint32_t z) {
    return visual_hash2(x,z);
}

/* Lightweight world dressing. These pieces are deliberately below actor collision scale:
 * they enrich the domain without creating invisible blockers or changing authoritative
 * navigation. Every placement is deterministic and generated directly by the C renderer. */
static void render_micro_scenery(const rcam *c) {
    uint32_t gz;
    visual_palette p=palette();
    for(gz=3u;gz+3u<ODG_GRID_SIZE;gz+=5u){
        uint32_t gx;
        for(gx=3u;gx+3u<ODG_GRID_SIZE;gx+=5u){
            uint32_t h=decor_hash(gx,gz),cell=gz*ODG_GRID_SIZE+gx;
            float x,z,y,jx,jz;
            uint32_t kind;
            if(g_odg.playable[cell]==0u || (h&7u)>1u) continue;
            if(!world_point_maybe_visible(c,-(float)ODG_WORLD_HALF_CELLS+(float)gx+0.5f,
                                           -(float)ODG_WORLD_HALF_CELLS+(float)gz+0.5f,2.2f)) continue;
            jx=((float)((h>>8u)&255u)/255.0f-0.5f)*2.2f;
            jz=((float)((h>>16u)&255u)/255.0f-0.5f)*2.2f;
            x=-(float)ODG_WORLD_HALF_CELLS+(float)gx+0.5f+jx;
            z=-(float)ODG_WORLD_HALF_CELLS+(float)gz+0.5f+jz;
            if(g_odg.playable[odg_cell_from_world((int32_t)(x*(float)ODG_FX_ONE),(int32_t)(z*(float)ODG_FX_ONE))]==0u) continue;
            y=terrain_yf(x,z);kind=(h>>24u)&3u;
            if(kind==0u){
                uint32_t c0=rgba_mix(p.leaf_high,0.78f+(float)((h>>4u)&7u)*0.045f);
                box_y(c,x-0.07f,z,0.025f,0.025f,y+0.03f,0.22f,c0);
                box_y(c,x+0.06f,z+0.04f,0.022f,0.022f,y+0.03f,0.16f,rgba_mix(c0,1.12f));
            }else if(kind==1u){
                uint32_t shrub=rgba_mix(p.leaf_low,0.86f+(float)((h>>5u)&7u)*0.035f);
                box_y(c,x,z,0.13f,0.11f,y+0.025f,0.18f,shrub);
                octa(c,x+0.07f,z-0.04f,0.09f,y+0.12f,0.16f,rgba_mix(p.leaf_high,0.90f));
            }else if(kind==2u){
                uint32_t rc=rgba_mix(p.rock,0.82f+(float)((h>>6u)&7u)*0.05f);
                octa(c,x,z,0.12f,y+0.025f,0.20f,rc);
            }else{
                box_y(c,x,z,0.040f,0.040f,y+0.03f,0.30f,rgba_mix(p.road,0.76f));
                box_y(c,x,z,0.062f,0.062f,y+0.29f,0.085f,rgba_mix(p.accent,0.78f));
            }
        }
    }
}

static void render_domain_beacons(const rcam *c) {
    static const int8_t points[][2]={{-46,-10},{-31,5},{-18,-25},{-6,34},{8,17},{18,-7},
                                     {29,9},{43,-18},{-38,31},{35,34},{-12,-43},{15,44}};
    uint32_t i;
    visual_palette p=palette();
    for (i=0u;i<(uint32_t)(sizeof(points)/sizeof(points[0]));++i) {
        int32_t fx=(int32_t)points[i][0]*ODG_FX_ONE;
        int32_t fz=(int32_t)points[i][1]*ODG_FX_ONE;
        uint32_t cell=odg_cell_from_world(fx,fz);
        float x=(float)points[i][0],z=(float)points[i][1],y,pulse;
        uint32_t phase;
        if (g_odg.playable[cell]==0u) continue;
        y=terrain_yf(x,z);
        phase=(uint32_t)((g_odg.tick+(uint64_t)i*9u)%UINT64_C(100));
        if (phase>50u) phase=100u-phase;
        pulse=(float)phase/50.0f;
        ground_shadow(c,x,z,0.28f,0.24f,0.42f);
        box_y(c,x,z,0.22f,0.22f,y+0.04f,0.14f,rgba_mix(p.road,0.70f));
        box_y(c,x,z,0.055f,0.055f,y+0.16f,0.88f,rgba_mix(p.building_alt,0.80f));
        box_y(c,x,z,0.12f,0.12f,y+0.96f,0.16f,rgba_mix(p.accent,0.76f+pulse*0.12f));
    }
}

static void render_particles(const rcam *c) {
    uint32_t i;
    for (i = 0u; i < ODG_MAX_PARTICLES; ++i) {
        odg_particle *p = &g_odg.particles[i];
        if (!p->active) continue;
        {
            float x=odg_fx_to_float(p->x),z=odg_fx_to_float(p->z);
            octa(c,x,z,0.055f,terrain_yf(x,z),odg_fx_to_float(p->y_fx)+0.07f,p->color);
        }
    }
}

/* One deterministic, allocation-free finishing pass. The small frozen S-curve works
 * like Music Motion's camera/output boundary: lighting stays in the scene, while final
 * contrast and the restrained lens vignette are applied only after all geometry.
 *
 * The radial term is separable, so cache each normalized axis when the viewport changes.
 * The per-pixel path then contains no division, 64-bit multiply, or color multiply: one
 * byte addition selects a precombined tone/vignette table. This matters on mobile CPUs
 * where the full-resolution finishing pass is otherwise more expensive than the scene. */
static void postprocess_frame(void) {
    static uint8_t axis_x[ODG_MAX_RENDER_WIDTH];
    static uint8_t axis_y[ODG_MAX_RENDER_HEIGHT];
    static uint8_t radial_vignette[257];
    static uint8_t graded[25][256];
    static uint32_t cached_w=UINT32_MAX,cached_h=UINT32_MAX;
    static uint32_t initialized=0u;
    uint32_t x,y,w=g_odg.width,h=g_odg.height;
    uint8_t *pixel=g_odg_framebuffer;
    if(w==0u || h==0u) return;
    if(initialized==0u){
        uint32_t shade,v;
        for(v=0u;v<256u;++v){
            uint32_t smooth=(v*v*(765u-2u*v)+32512u)/65025u;
            uint32_t curve=(v*3u+smooth+2u)/4u;
            for(shade=0u;shade<=24u;++shade)
                graded[shade][v]=(uint8_t)((curve*(256u-shade))>>8u);
        }
        for(v=0u;v<=256u;++v){
            /* 88/128 is the former 11/16 radial threshold; 256/128 is the
             * squared corner radius. The output remains the same subtle 0..24. */
            radial_vignette[v]=(uint8_t)(v<=88u?0u:((v-88u)*24u)/168u);
        }
        initialized=1u;
    }
    if(cached_w!=w){
        uint64_t denom=(uint64_t)w*(uint64_t)w;
        for(x=0u;x<w;++x){
            int64_t dx=(int64_t)(2u*x)-(int64_t)w;
            uint64_t square=(uint64_t)(dx*dx);
            axis_x[x]=(uint8_t)((square*128u+denom/2u)/denom);
        }
        cached_w=w;
    }
    if(cached_h!=h){
        uint64_t denom=(uint64_t)h*(uint64_t)h;
        for(y=0u;y<h;++y){
            int64_t dy=(int64_t)(2u*y)-(int64_t)h;
            uint64_t square=(uint64_t)(dy*dy);
            axis_y[y]=(uint8_t)((square*128u+denom/2u)/denom);
        }
        cached_h=h;
    }
    for(y=0u;y<h;++y){
        uint32_t y_term=axis_y[y];
        for(x=0u;x<w;++x){
            const uint8_t *table=graded[radial_vignette[(uint32_t)axis_x[x]+y_term]];
            pixel[0]=table[pixel[0]];
            pixel[1]=table[pixel[1]];
            pixel[2]=table[pixel[2]];
            pixel+=4;
        }
    }
}

void odg_render_internal(void) {
    rcam c;
    uint32_t i;
    int showcase = g_odg.presentation_mode == ODG_PRESENTATION_SHOWCASE;
    float anchor_x = odg_fx_to_float(g_odg.camera_anchor_x);
    float anchor_z = odg_fx_to_float(g_odg.camera_anchor_z);

    c.w = g_odg.width;
    c.h = g_odg.height;
    c.forward_x = (float)g_odg.camera_dir_x_q15 / (float)ODG_Q15_ONE;
    c.forward_z = (float)g_odg.camera_dir_z_q15 / (float)ODG_Q15_ONE;
    if (c.forward_x == 0.0f && c.forward_z == 0.0f) c.forward_z = 1.0f;
    c.right_x = c.forward_z;
    c.right_z = -c.forward_x;

    if (showcase) {
        /* The start screen is a live C-rendered world, not a static wallpaper. A wider,
         * slightly asymmetric dolly reveals coast, districts, territory, actors and
         * infrastructure behind the glass UI. It is presentation-only and never enters
         * the deterministic state hash or gameplay camera. */
        uint32_t phase=(uint32_t)(g_odg.tick%UINT64_C(720));
        float triangle;
        float side;
        if (phase>360u) phase=720u-phase;
        triangle=((float)phase/360.0f)*2.0f-1.0f;
        side=0.95f+triangle*0.42f;
        anchor_x += c.forward_x*2.60f;
        anchor_z += c.forward_z*2.60f;
        c.sp = 0.285f;
        c.focal = (float)g_odg.width * 0.505f;
        if((float)g_odg.height*0.42f>c.focal)c.focal=(float)g_odg.height*0.42f;
        c.cam_x = anchor_x - c.forward_x*8.20f + c.right_x*side;
        c.cam_z = anchor_z - c.forward_z*8.20f + c.right_z*side;
        {
            float anchor_y=terrain_yf(anchor_x,anchor_z)+4.10f;
            float clearance=terrain_yf(c.cam_x,c.cam_z)+1.45f;
            c.cam_y=anchor_y>clearance?anchor_y:clearance;
        }
    } else {
        c.sp = (float)g_odg.camera_pitch_q15 / (float)ODG_Q15_ONE;
        c.focal = (float)g_odg.width * 0.59f;
        if((float)g_odg.height*0.50f>c.focal)c.focal=(float)g_odg.height*0.50f;
        /* Centered third-person chase camera. Yaw follows the body through the slower
         * deterministic camera turn-rate in sim.c; it never snaps on reverse input. */
        c.cam_x = anchor_x - c.forward_x * ((float)g_odg.camera_distance_fx/(float)ODG_FX_ONE);
        c.cam_z = anchor_z - c.forward_z * ((float)g_odg.camera_distance_fx/(float)ODG_FX_ONE);
        c.cam_y = odg_fx_to_float(g_odg.camera_height_fx);
    }
    /* sqrt(1-sp^2) without libm: the pitch range is small enough that the fourth-order
     * series is visually indistinguishable here and keeps freestanding WASM dependency-free. */
    {
        float s2=c.sp*c.sp;
        c.cp=1.0f-0.5f*s2-0.125f*s2*s2;
    }

    prepare_depth_fog();
    clear_frame(&c);
    ground_and_grid(&c);
    /* Ownership is already folded into the terrain material in ground_and_grid().
     * Roads and architecture remain physical structure above it. */
    render_routes(&c);
    render_distant_landmarks(&c);
    render_territory_edges(&c);
    render_trails(&c);
    render_micro_scenery(&c);
    render_boundaries(&c);
    render_turrets(&c);
    render_ammo_crates(&c);
    render_chips(&c);

    for (i = 0u; i < g_odg.obstacle_count; ++i) render_world_obstacle(&c,&g_odg.obstacles[i]);

    for (i = 0u; i < ODG_MAX_ACTORS; ++i) {
        odg_actor *a = &g_odg.actors[i];
        if (!a->active || a->hp == 0u) continue;
        runner(&c, a);

    }
    render_domain_beacons(&c);
    if (g_odg.player_carried_turret<g_odg.turret_count && g_odg.actors[ODG_PLAYER_ID].hp!=0u) {
        const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
        int32_t tx=0,tz=0;
        int valid=odg_turret_drop_candidate_internal(p,&tx,&tz);
        float px=valid?odg_fx_to_float(tx):odg_fx_to_float(p->x)+(float)p->face_x_q15/(float)ODG_Q15_ONE*1.90f;
        float pz=valid?odg_fx_to_float(tz):odg_fx_to_float(p->z)+(float)p->face_z_q15/(float)ODG_Q15_ONE*1.90f;
        float py=terrain_yf(px,pz)+0.08f;
        uint32_t col=valid?0xffe28affu:0xff6f68ffu;
        /* Preview the exact authoritative deployment socket. The ghost and the action
         * use the same C function, so the turret can no longer land somewhere other
         * than where the player was shown it would land. */
        box_y(&c,px,pz,0.42f,0.42f,py,0.14f,rgba_mix(col,0.58f));
        line_overlay(&c,(rv3){px-0.52f,py+0.03f,pz},(rv3){px+0.52f,py+0.03f,pz},col);
        line_overlay(&c,(rv3){px,py+0.03f,pz-0.52f},(rv3){px,py+0.03f,pz+0.52f},col);
    }
    render_particles(&c);
    postprocess_frame();
}
