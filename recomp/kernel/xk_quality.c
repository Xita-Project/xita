/* Halo Xbox cache version 5 visual quality overrides, applied to freshly read
 * tag memory before the engine processes it. Cache files and saves are untouched.
 * Field layouts: Invader HEK definitions, checked against the user's Xbox maps.
 */
#include "xk.h"
#include "xk_quality.h"
#include "../../runtime/xv_quality_settings.h"
#include <math.h>

#define TAG_BASE 0x803A6000u
#define TAG_GROUP(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(d))
typedef struct { uint32_t end, array, count; } quality_tags;

static int span(const quality_tags *t, uint32_t p, uint32_t n)
{
    return p >= TAG_BASE && p <= t->end && n <= t->end - p;
}
static uint32_t tag_data(const quality_tags *t, uint32_t id, uint32_t group, uint32_t bytes)
{
    unsigned i = id & 0xffffu;
    if (id == UINT32_MAX || i >= t->count) return 0;
    uint32_t e = t->array + i * 32u;
    if (X_M32(e) != group || X_M32(e+12) != id || X_M32(e+24)) return 0;
    uint32_t p = X_M32(e+20);
    return span(t,p,bytes) ? p : 0;
}
static float get_float(uint32_t p)
{
    uint32_t u = X_M32(p); float f; memcpy(&f,&u,4); return f;
}
static void put_float(uint32_t p, float f)
{
    uint32_t u; memcpy(&u,&f,4); X_M32(p) = u;
}
static void null_bitmap(uint32_t p) { X_M32(p+12) = UINT32_MAX; }

static int name_prefix(const quality_tags *t, uint32_t entry, const char *prefix)
{
    uint32_t p = X_M32(entry+16);
    for (unsigned i=0; prefix[i]; ++i)
        if (!span(t,p,i+1) || X_M8(p+i)!=(unsigned char)prefix[i]) return 0;
    return 1;
}

static int model_has_lods(const quality_tags *t, uint32_t p)
{
    unsigned nr=X_M32(p+0xc4), ng=X_M32(p+0xd0);
    uint32_t regions=X_M32(p+0xc8), geometry=X_M32(p+0xd4);
    if (!nr || nr>32 || !ng || ng>256 || !span(t,regions,nr*76u) ||
        !span(t,geometry,ng*48u)) return 0;
    int alternate=0;
    for (unsigned r=0; r<nr; ++r) {
        uint32_t region=regions+r*76u, permutations=X_M32(region+68);
        unsigned count=X_M32(region+64);
        if (!count || count>32 || !span(t,permutations,count*88u)) return 0;
        for (unsigned j=0; j<count; ++j) {
            uint32_t perm=permutations+j*88u;
            unsigned first=X_M16(perm+64);
            for (unsigned lod=0; lod<5; ++lod) {
                unsigned index=X_M16(perm+64+lod*2);
                /* Retain original selection for incomplete LOD tables. */
                if (index>=ng) return 0;
                if (index!=first) alternate=1;
            }
        }
    }
    return alternate;
}

static void model_lod_quality(const quality_tags *t, int level)
{
    if (level==2) return;
    enum { ELIGIBLE=1, PROTECTED=2 };
    uint8_t *uses=calloc(t->count,1);
    if (!uses) return;
    unsigned changed=0, protected=0, unchanged=0;
    /* Resolve all object owners first. A cinematic scenery tag can share a
     * weapon or vehicle model; that model must retain its original cutoffs. */
    for (unsigned i=0; i<t->count; ++i) {
        uint32_t entry=t->array+i*32u, group=X_M32(entry), p;
        if (group!=TAG_GROUP('o','b','j','e') &&
            X_M32(entry+4)!=TAG_GROUP('o','b','j','e') &&
            X_M32(entry+8)!=TAG_GROUP('o','b','j','e')) continue;
        if (!(p=tag_data(t,X_M32(entry+12),group,380))) goto done;
        uint32_t model=X_M32(p+0x34);
        if (!tag_data(t,model,TAG_GROUP('m','o','d','e'),232)) continue;
        int eligible=(group==TAG_GROUP('s','c','e','n') ||
                      group==TAG_GROUP('b','i','p','d')) &&
                     !name_prefix(t,entry,"cinematics\\");
        uses[model&0xffffu] |= eligible ? ELIGIBLE : PROTECTED;
    }
    for (unsigned i=0; i<t->count; ++i) {
        if (!(uses[i]&ELIGIBLE)) continue;
        if (uses[i]&PROTECTED) { ++protected; continue; }
        uint32_t entry=t->array+i*32u;
        if (!name_prefix(t,entry,"scenery\\") &&
            !name_prefix(t,entry,"characters\\")) { ++protected; continue; }
        uint32_t p=tag_data(t,X_M32(entry+12),TAG_GROUP('m','o','d','e'),232);
        float cutoff[5]; int valid=p!=0;
        if (!p) continue;
        for (unsigned j=0; j<5; ++j) {
            cutoff[j]=get_float(p+8+j*4);
            if (!isfinite(cutoff[j]) || cutoff[j]<0 || cutoff[j]>1000000 ||
                (j && cutoff[j]<cutoff[j-1])) valid=0;
        }
        if (!valid || cutoff[4]==0 || !model_has_lods(t,p)) { ++unchanged; continue; }
        /* Xbox 3925's selector at 0xA2800 reads increasing cutoffs from +8.
         * Raise only the four mesh-transition thresholds: the original
         * minimum visibility cutoff, geometry, bounds and collision stay intact. */
        float scale=level==1 ? 1.25f : (5.0f/3.0f);
        for (unsigned j=1; j<5; ++j) put_float(p+8+j*4,cutoff[j]*scale);
        ++changed;
    }
    XK_LOG("[model-lod] detail %d: %u models adjusted, %u protected, %u without usable transitions; minimum visibility retained\n",
        level,changed,protected,unchanged);
done:
    free(uses);
}
static int cosmetic_particle(const quality_tags *t, uint32_t entry, uint32_t p)
{
    /* Preserve any particle with collision, death or material effects, and all
     * projectile/energy particles. Only shorten named cosmetic smoke/sparks. */
    if (X_M32(p+0x30) != UINT32_MAX || X_M32(p+0x54) != UINT32_MAX ||
        X_M32(p+0x64) != UINT32_MAX) return 0;
    uint32_t name = X_M32(entry+16);
    char text[256]; unsigned n = 0;
    for (; n < sizeof text - 1 && span(t,name+n,1); ++n) {
        unsigned char c = X_M8(name+n);
        text[n] = (char)tolower(c);
        if (!c) break;
    }
    if (n == sizeof text-1 || !span(t,name+n,1) || X_M8(name+n)) return 0;
    text[n] = 0;
    if (strstr(text,"plasma") || strstr(text,"projectile") || strstr(text,"bullet") ||
        strstr(text,"tracer") || strstr(text,"energy")) return 0;
    return strstr(text,"smoke") || strstr(text,"spark") || strstr(text,"dust") || strstr(text,"steam");
}

static int temporary_decal(const quality_tags *t, uint32_t id)
{
    uint32_t p=tag_data(t,id,TAG_GROUP('d','e','c','a'),268);
    if (!p || X_M16(p+2)==3 || (X_M16(p)&16)) return 0;
    float life=get_float(p+0x78), upper=get_float(p+0x7c);
    return isfinite(life) && life>0 && isfinite(upper) && upper>=life && upper<=86400;
}

/* Remove only optional emitters from freshly loaded effect tags. Retain events,
 * their timing/locations, and the order of all remaining parts (including
 * damage, sounds, lights and projectiles). Zero particle lifetimes are NOT an
 * off switch. Each selected emitter is removed before it allocates anything. */
static void effect_quality(const quality_tags *t, int decals, int cosmetics)
{
    unsigned removed_parts=0, removed_particles=0;
    for (unsigned i=0;i<t->count;++i) {
        uint32_t e=t->array+i*32u;
        uint32_t p=tag_data(t,X_M32(e+12),TAG_GROUP('e','f','f','e'),64);
        if (!p) continue;
        unsigned count=X_M32(p+0x34); uint32_t events=X_M32(p+0x38);
        if (count>32 || !span(t,events,count*68u)) continue;
        int valid=1;
        for (unsigned j=0;j<count;++j) {
            uint32_t event=events+j*68u;
            unsigned np=X_M32(event+0x2c), nq=X_M32(event+0x38);
            if (np>32 || nq>32 || (np && !span(t,X_M32(event+0x30),np*104u)) ||
                (nq && !span(t,X_M32(event+0x3c),nq*232u))) valid=0;
        }
        if (!valid) continue; /* no partial mutation of a malformed effect */
        for (unsigned j=0;j<count;++j) {
            uint32_t event=events+j*68u;
            for (unsigned kind=0;kind<2;++kind) {
                if (kind ? cosmetics : decals) continue;
                uint32_t header=event+(kind ? 0x38u : 0x2cu);
                unsigned n=X_M32(header), dst=0, stride=kind ? 232u : 104u;
                uint32_t list=X_M32(header+4);
                for (unsigned k=0;k<n;++k) {
                    uint32_t record=list+k*stride, ref=record+(kind ? 0x54u : 0x18u);
                    uint32_t id=X_M32(ref+12); int remove=0;
                    if (kind && X_M32(ref)==TAG_GROUP('p','a','r','t')) {
                        uint32_t particle=tag_data(t,id,TAG_GROUP('p','a','r','t'),356);
                        if (particle) remove=cosmetic_particle(t,t->array+(id&0xffffu)*32u,particle);
                    } else if (!kind && X_M32(ref)==TAG_GROUP('d','e','c','a')) {
                        remove=temporary_decal(t,id);
                    }
                    if (remove) {
                        if (kind) ++removed_particles; else ++removed_parts;
                        continue;
                    }
                    if (dst!=k) {
                        uint8_t copy[232]; x_guest_read(copy,record,stride);
                        x_guest_write(list+dst*stride,copy,stride);
                    }
                    ++dst;
                }
                X_M32(header)=dst;
            }
        }
    }
    XK_LOG("[quality] effect emitters removed: %u temporary decals, %u cosmetic particles\n",
        removed_parts,removed_particles);
}

void xk_quality_map_read(uint32_t address, uint32_t bytes)
{
    static int configured, material, glow, particles, decal_seconds, model_detail;
    static int decals_on, cosmetics_on, reflections_on, shadows_on;
    if (!configured) {
        material = xv_quality_int("XV_MATERIAL_QUALITY",2,0,2);
        glow = xv_quality_int("XV_GLOW_QUALITY",2,0,2);
        particles = xv_quality_int("XV_PARTICLE_QUALITY",2,0,2);
        decal_seconds = xv_quality_int("XV_DECAL_SECONDS",0,0,300);
        model_detail = xv_quality_int("XV_MODEL_DETAIL",2,0,2);
        decals_on = xv_quality_int("XV_TEMP_DECALS",1,0,1);
        cosmetics_on = xv_quality_int("XV_COSMETIC_EFFECTS",1,0,1);
        reflections_on = xv_quality_int("XV_REFLECTIONS",1,0,1);
        shadows_on = xv_quality_int("XV_OBJECT_SHADOWS",1,0,1);
        configured = 1;
    }
    if (address != TAG_BASE || bytes < 0x28 || bytes > 32u*1024*1024) return;
    if (X_M32(address+0x20) != TAG_GROUP('t','a','g','s')) return;
    quality_tags t = {address+bytes,X_M32(address),X_M32(address+12)};
    if (!t.count || t.count > 8192 || !span(&t,t.array,t.count*32u)) return;
    /* Validate the complete index before any mutation. A truncated/foreign
     * cache must not accidentally turn a pointer into a material field. */
    for (unsigned i = 0; i < t.count; ++i)
        if ((X_M32(t.array+i*32+12)&0xffffu) != i) return;
    model_lod_quality(&t,model_detail);
    if (!decals_on || !cosmetics_on) effect_quality(&t,decals_on,cosmetics_on);
    unsigned materials=0,flares=0,shortened=0,decals=0,shadows=0;
    for (unsigned i = 0; i < t.count; ++i) {
        uint32_t entry=t.array+i*32u, group=X_M32(entry), id=X_M32(entry+12), p;
        if (!shadows_on && (group==TAG_GROUP('o','b','j','e') ||
            X_M32(entry+4)==TAG_GROUP('o','b','j','e') || X_M32(entry+8)==TAG_GROUP('o','b','j','e')) &&
            (p=tag_data(&t,id,group,380))) {
            /* ObjectFlags.does_not_cast_shadow; no bounds/collision changes. */
            if (!(X_M16(p+2)&1)) { X_M16(p+2)|=1; ++shadows; }
        }
        if ((material < 2 || !reflections_on) && group == TAG_GROUP('s','e','n','v') &&
            (p=tag_data(&t,id,group,836))) {
            /* Keep base color, lightmaps, cutout flags, self illumination and
             * diffuse dynamic lighting. These optional maps have valid null
             * paths in the original material builder. */
            if (material<2) { null_bitmap(p+0xb8); null_bitmap(p+0xcc); null_bitmap(p+0xfc); }
            if (!material || !reflections_on) {
                null_bitmap(p+0x324);
                put_float(p+0x2f4,0); put_float(p+0x2f8,0);
            }
            if (!material) {
                put_float(p+0x290,0);
                /* Bump alpha can be a specular mask; keep that interpretation
                 * intact when enabled, otherwise use the engine's flat path. */
                if (!(X_M16(p+0x28)&2)) null_bitmap(p+0x128);
            }
            ++materials;
        } else if ((material < 2 || !reflections_on) && group == TAG_GROUP('s','o','s','o') &&
                   (p=tag_data(&t,id,group,440))) {
            if (material<2) null_bitmap(p+0xdc);
            if (!material || !reflections_on) {
                null_bitmap(p+0x164); put_float(p+0x144,0); put_float(p+0x154,0);
            }
            ++materials;
        } else if (glow < 2 && group == TAG_GROUP('l','e','n','s') &&
                   (p=tag_data(&t,id,group,240))) {
            unsigned count=X_M32(p+0xc4), dst=0; uint32_t list=X_M32(p+0xc8);
            if (count > 32 || !span(&t,list,count*128u)) continue;
            if (glow) for (unsigned j=0;j<count;++j) {
                /* On-axis reflections are the source glow; off-axis layers are
                 * cosmetic lens ghosts. Never disable the occlusion flags. */
                float pos=get_float(list+j*128u+0x1c);
                if (!isfinite(pos) || fabsf(pos)>0.0001f) continue;
                if (dst != j) {
                    uint8_t reflection[128]; x_guest_read(reflection,list+j*128u,128);
                    x_guest_write(list+dst*128u,reflection,128);
                }
                ++dst;
            }
            X_M32(p+0xc4)=dst; ++flares;
        } else if (particles < 2 && group == TAG_GROUP('p','a','r','t') &&
                   (p=tag_data(&t,id,group,356)) && cosmetic_particle(&t,entry,p)) {
            float v[4], scale=particles ? .75f : .5f; int valid=1;
            for (unsigned j=0;j<4;++j) {
                v[j]=get_float(p+0x38+j*4);
                if (!isfinite(v[j]) || v[j]<0 || v[j]>3600) valid=0;
            }
            if (!valid || v[0]<=0 || v[1]<v[0]) continue;
            for (unsigned j=0;j<4;++j) put_float(p+0x38+j*4,v[j]*scale);
            ++shortened;
        } else if (decal_seconds && group == TAG_GROUP('d','e','c','a') &&
                   (p=tag_data(&t,id,group,268)) && X_M16(p+2)!=3 && !(X_M16(p)&16)) {
            float life[4]; int valid=1;
            for (unsigned j=0;j<4;++j) {
                life[j]=get_float(p+0x78+j*4);
                if (!isfinite(life[j]) || life[j]<0 || life[j]>86400) valid=0;
            }
            if (!valid || life[0]<=0 || life[1]<life[0] || life[3]<life[2]) continue;
            for (unsigned j=0;j<4;++j)
                put_float(p+0x78+j*4,fminf(life[j],j<2 ? (float)decal_seconds : 1.0f));
            ++decals;
        }
    }
    XK_LOG("[quality] loaded tags: materials %u (level %d), flares %u (level %d), cosmetic particles %u (level %d), decal lifetimes %u (cap %d s)\n",
        materials,material,flares,glow,shortened,particles,decals,decal_seconds);
    XK_LOG("[quality] switches: temporary decals %d, cosmetic effects %d, material reflections %d, object shadows %d (%u tags disabled)\n",
        decals_on,cosmetics_on,reflections_on,shadows_on,shadows);
}

typedef struct { uint32_t record, age; } quality_decal;
static int oldest_decal(const void *a, const void *b)
{
    const quality_decal *x=a, *y=b;
    if (x->age != y->age) return x->age > y->age ? -1 : 1;
    return x->record < y->record ? -1 : x->record != y->record;
}
static int physical_span(uint32_t p, unsigned bytes)
{
    return p >= 0x80000000u && p <= 0x84000000u && bytes <= 0x84000000u-p;
}
void xk_quality_decal_budget(void)
{
    static int limit=-1;
    if (limit<0) limit=xv_quality_int("XV_DECAL_LIMIT",0,0,2048);
    if (!limit) return;
    /* Called immediately before Halo 3925's original decal update (0x114C50).
     * Expire excess non-permanent records, then let that updater free vertex
     * storage and repair the engine's lists. Never free a guest object here. */
    uint32_t pool=X_M32(0x2FAB34), clock=X_M32(0x2F8CA0);
    if (!physical_span(pool,0x38) || !physical_span(clock,0x10)) return;
    if (X_M16(pool+0x20)!=2048 || X_M16(pool+0x22)!=0x38 || !X_M8(pool+0x24)) return;
    uint32_t data=X_M32(pool+0x34), now=X_M32(clock+0xc);
    if (!physical_span(data,2048u*0x38u)) return;
    quality_decal records[2048]; unsigned count=0;
    for (unsigned i=0;i<2048;++i) {
        uint32_t p=data+i*0x38u;
        if (!X_M16(p) || (X_M16(p+2)&2)) continue;
        float life=get_float(p+0x1c);
        if (!isfinite(life) || life<=0) continue; /* zero lifetime is permanent */
        records[count++]=(quality_decal){p,now-X_M32(p+0x14)};
    }
    if (count<=(unsigned)limit) return;
    qsort(records,count,sizeof records[0],oldest_decal);
    for (unsigned i=0;i<count-(unsigned)limit;++i) {
        uint32_t p=records[i].record;
        put_float(p+0x1c,0.000001f); put_float(p+0x20,0);
        X_M32(p+0x14)=now-1u;
    }
    static unsigned reports;
    if (reports++<8) XK_LOG("[quality] decal budget %d: expiring %u old impact records\n",limit,count-(unsigned)limit);
}
