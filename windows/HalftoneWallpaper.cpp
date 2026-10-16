// HalftoneWallpaper — Windows 11 port of the macOS halftone-aurora wallpaper.
//
// Same field as Resources/ripple.html, re-hosted on Direct3D 11 instead of
// Canvas 2D. The frame is ONE fullscreen triangle: the pixel shader reads two
// structured buffers of per-dot params, a baked star layer, a 14px-cell
// under-glow texture, and a 128² grain tile, and composites them in the same
// paint order the Canvas renderer uses.
//
//   CPU per frame (~5k dots, <1 ms): spring sim, impulse queue, fieldAt(),
//     per-dot radius/color → dotA/dotB buffers + glow texture.
//   GPU per pixel:   stars|glow base → 3×3 dot SDF srcOver → fringe/bloom
//     (additive) → grain multiply.
//
// The window parents into WorkerW (behind SHELLDLL_DefView) so it sits below
// the desktop icons, like the .desktopWindow level on macOS.
//
// Flags: --still (frozen frame, ~0 GPU)  --no-pointer  --interactive
//        --install / --uninstall (HKCU Run key autostart)
//
// Build: build.cmd  (cl.exe, Windows SDK, no other deps)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <pbt.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <shellapi.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <vector>
#include <algorithm>

using std::min;
using std::max;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// ============================================================================
// Field — line-for-line port of parts/_field_extract.js (double precision so
// hash2/sin match JS bit-for-bit; GPU sees the packed results).
// ============================================================================
struct RGB { double r, g, b; };
static const RGB PAL_MAGENTA{255, 47, 168}, PAL_VIOLET{139, 77, 255},
                 PAL_ROYAL{59, 91, 255}, PAL_INDIGO{79, 70, 229};
struct FocusDef { double fx, y, r; RGB c; double ph, sp, ax; };
static const FocusDef FOCI[] = {
    {0.14, -0.04, 0.36, PAL_MAGENTA, 0.0, 0.21, 0.055},
    {0.52,  0.00, 0.30, PAL_VIOLET,  2.1, 0.16, 0.045},
    {0.88, -0.03, 0.38, PAL_ROYAL,   4.4, 0.12, 0.060},
};
static const FocusDef FOCI_WIDE[] = {
    {0.04, -0.04, 0.36, PAL_MAGENTA,       0.0, 0.21, 0.055},
    {0.21, -0.03, 0.30, {197, 62, 212},    1.6, 0.17, 0.050},
    {0.50,  0.00, 0.30, PAL_VIOLET,        2.1, 0.16, 0.045},
    {0.79, -0.03, 0.30, {99, 84, 255},     3.2, 0.18, 0.050},
    {0.96, -0.03, 0.38, PAL_ROYAL,         4.4, 0.12, 0.060},
};
constexpr double LOBE_X = 1.4, LOBE_Y = 0.8, RIBBON = 0.2;

static inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
static inline double sstep(double e0, double e1, double x) {
    double t = clampd((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t);
}
static inline double fracd(double v) { return v - floor(v); }
static inline double h11(double p) {
    p = fracd(p * 0.1031); p *= p + 33.33; p *= p + p; return fracd(p);
}
static inline double vn(double x) {
    double i = floor(x), f = x - i, a = h11(i);
    return a + (h11(i + 1) - a) * f * f * (3 - 2 * f);
}
static inline double fbm(double x) {
    return (0.5 * vn(x) + 0.25 * vn(x * 2.03 + 17) + 0.125 * vn(x * 4.1 + 31)) / 0.875;
}
static inline double hash2(double i, double j) {
    return fracd(sin(i * 127.1 + j * 311.7) * 43758.5453);
}

struct Foci { double x, y, rx, ry; RGB c; };
static void fociAt(double t, const FocusDef* set, size_t n, double aspect, std::vector<Foci>& out) {
    out.clear();
    const double narrow = clampd(aspect / 1.5, 0.4, 1);
    const double kx = LOBE_X * narrow, ky = LOBE_Y * (1.6 - 0.6 * narrow);
    for (size_t i = 0; i < n; i++) {
        const FocusDef& f = set[i];
        out.push_back({
            f.fx * aspect + sin(t * f.sp + f.ph) * f.ax,
            f.y + sin(t * f.sp * 0.8 + f.ph * 1.7) * 0.03,
            f.r * kx, f.r * ky, f.c });
    }
}
struct Field { double I; RGB c; double hi; };
static Field fieldAt(double x, double y, double t, const std::vector<Foci>& foci) {
    const double yy = y + 0.04 * sin(x * 5.0 + t * 0.25) + 0.022 * sin(x * 9.0 - t * 0.4)
        + 0.08 * (fbm(x * 1.7 + t * 0.045) - 0.5);
    RGB c{0, 0, 0};
    double sum = 0;
    for (const Foci& f : foci) {
        const double dx = (x - f.x) / f.rx, dy = (yy - f.y) / f.ry;
        const double d2 = dx * dx + dy * dy;
        if (d2 > 12) continue;                       // w < 6e-6: invisible
        const double w = exp(-d2);
        sum += w; c.r += f.c.r * w; c.g += f.c.g * w; c.b += f.c.b * w;
    }
    if (sum > 1e-4) { c.r /= sum; c.g /= sum; c.b /= sum; }
    const double coarse = clampd((fbm(x * 9 + t * 0.04 + 3.1) - 0.28) * 1.6, 0, 1);
    const double fine = 0.6 * vn(x * 48 + t * 0.05 + 9.7) + 0.4 * vn(x * 97 - t * 0.12 + 4.2);
    const double ray = clampd(coarse * (0.25 + fine) + (fine - 0.5) * 0.3, 0, 1);
    const double d = yy - (0.2 + 0.09 * (fbm(x * 3.1 - t * 0.05 + 11) - 0.5));
    const double above = sstep(-0.035, 0.004, d);
    const double env = min(1.0, sum * 1.35);
    const double fade = 1 - sstep(0.04, 0.44 + 0.26 * ray, yy);
    const double diffuse = fade * min(1.0, (sum + RIBBON * exp(-yy * yy / 0.06)) * 1.15)
        * (1 - 0.5 * above * (1 - ray));
    const double curtain = above * exp(-max(0.0, d) / (0.05 + 0.22 * ray)) * (0.2 + 0.9 * ray);
    const double hi = env * exp(-d * d / (d < 0 ? 0.0002 : 0.0025)) * (0.8 + 0.2 * ray);
    const double I = diffuse + env * (0.5 * curtain + 0.45 * hi) * (1 - sstep(0.36, 0.7, yy));
    const double m = 0.4 * above * sstep(0.05, 0.3, d);
    c.r += (PAL_MAGENTA.r - c.r) * m;
    c.g += (PAL_MAGENTA.g - c.g) * m;
    c.b += (PAL_MAGENTA.b - c.b) * m;
    return { I, c, hi };
}
static inline uint8_t qchan(double v) {   // JS: (v > 255 ? 255 : v) & 0xF8
    return (uint8_t)((int32_t)(v > 255 ? 255 : v) & 0xF8);
}

// ============================================================================
// Simulation state (dot springs, impulses, pointer) — the Canvas renderer's
// per-dot state, kept on CPU so the GPU just shades.
// ============================================================================
constexpr double SPACING = 11, RMAX = 4.8, UNDER = 0.22;
constexpr int GLOW_CELL = 14;
constexpr int GRAIN = 128;

struct Dot {
    int i = 0, j = 0;
    double bx = 0, by = 0;                 // base position (css px, y down)
    double dx = 0, dy = 0, vx = 0, vy = 0; // spring displacement/velocity
    double g = 1, l = 1;                   // ink variance
};
struct Impulse { double x, y, vx, vy; };
struct Star { float x, y, r, a, ph, sp; };

static double gW = 0, gH = 0, gAspect = 1, gDpr = 1;   // css size of virtual desktop
static int gPhysW = 0, gPhysH = 0;                     // physical swapchain px
static int gVsX = 0, gVsY = 0;                     // virtual-screen origin (physical)
static int gCols = 0, gRows = 0;
static std::vector<Dot> gDots;
static std::vector<Star> gStars;
static std::vector<Impulse> gImpulses;
static struct {
    double x = -1e4, y = -1e4, px = -1e4, py = -1e4;
    double vx = 0, vy = 0; uint64_t pt = 0; bool down = false;
} gPointer;

static void parkPointer() {
    gPointer.x = gPointer.y = gPointer.px = gPointer.py = -1e4;
    gPointer.vx = gPointer.vy = 0; gPointer.pt = 0; gPointer.down = false;
}
// __feed: velocity from feed cadence, clamped like the JS feed.
static void feedPointer(double x, double y, bool down) {
    const uint64_t now = GetTickCount64();
    const double dt = max(8.0, (double)(now - gPointer.pt)) / 1000.0;
    if (gPointer.pt) {
        gPointer.vx = clampd((x - gPointer.px) / dt, -2600, 2600);
        gPointer.vy = clampd((y - gPointer.py) / dt, -2600, 2600);
    }
    gPointer.px = gPointer.x = x; gPointer.py = gPointer.y = y;
    gPointer.pt = now; gPointer.down = down;
    // impulseQueue(kind="hover"|"drag"): release events never reach us here.
    const double s = down ? 1.0 : 0.30;
    gImpulses.push_back({ x, y, gPointer.vx * s, gPointer.vy * s });
    if (gImpulses.size() > 6) gImpulses.erase(gImpulses.begin());
}

// build(): grid + stars, same loops as the JS renderer. Rebuild on resize.
static void buildWorld() {
    gCols = (int)floor((gW - SPACING * 0.5) / SPACING) + 1;
    gDots.clear(); gStars.clear();
    for (int j = 0; ; j++) {
        const double y = gH + SPACING * 0.5 - j * SPACING;
        if (y < gH * 0.30) break;
        for (int i = 0; ; i++) {
            const double x = SPACING * 0.5 + i * SPACING;
            if (x > gW) break;
            Dot d;
            d.i = i; d.j = j;
            d.bx = x + (hash2(i, j) - 0.5) * SPACING * 0.16;
            d.by = y + (hash2(j + 71, i) - 0.5) * SPACING * 0.16;
            d.g = 0.86 + 0.28 * hash2(i + 13, j + 29);
            d.l = 0.92 + 0.12 * hash2(j + 5, i + 41);
            gDots.push_back(d);
        }
    }
    gRows = (int)(gDots.size() / gCols);
    const int n = (int)(gW * gH / 4500 + 0.5);
    for (int s = 0; s < n; s++) {
        const double m = hash2(s + 0.5, s * 1.3);
        const double y = gH * 0.7 * pow(hash2(7.7, s), 1.3);
        Star st;
        st.x = (float)(hash2(s, 3.3) * gW); st.y = (float)y;
        st.r = (float)(0.45 + 0.85 * m * m);
        st.a = (float)((0.3 + 0.65 * hash2(s * 2.1, 9.2)) * (1 - sstep(gH * 0.4, gH * 0.7, y)));
        st.ph = (float)(m * 40); st.sp = (float)(0.5 + 2.4 * hash2(s, s + 9));
        gStars.push_back(st);
    }
}

// Per-dot render eval (warp spring + impulses + field + pointer glow).
// Fills dotA/dotB upload arrays. Mirrors the JS per-dot block exactly.
static void evalDots(double t, double dt, bool freeze,
                     std::vector<float>& dotA, std::vector<uint32_t>& dotB,
                     const std::vector<Foci>& foci) {
    const double k = 96, c = 13;
    const double pointerOn = gPointer.x > -999;
    for (Dot& d : gDots) {
        if (!freeze) {
            d.vx += (-k * d.dx - c * d.vx) * dt;
            d.vy += (-k * d.dy - c * d.vy) * dt;
            d.dx = clampd(d.dx + d.vx * dt, -18, 18);
            d.dy = clampd(d.dy + d.vy * dt, -18, 18);
            for (const Impulse& im : gImpulses) {
                const double ox = d.bx - im.x, oy = d.by - im.y;
                const double gg = exp(-(ox * ox + oy * oy) / (2 * 130 * 130));
                const double sp = hypot(im.vx, im.vy);
                const double od = max(1.0, hypot(ox, oy));
                d.vx += (im.vx * 0.8 + (ox / od) * sp * 0.5) * gg * 0.12;
                d.vy += (im.vy * 0.8 + (oy / od) * sp * 0.5) * gg * 0.12;
            }
        }
        const double px = d.bx + d.dx, py = d.by + d.dy;
        const Field f = fieldAt(d.bx / gH, (gH - d.by) / gH, t, foci);
        double I = f.I;
        I += (freeze ? 0 : 0.07) * sin(d.bx * 0.02 + d.by * 0.012 - t * 1.15)
            * (1 - sstep(0.1, 0.5, (gH - d.by) / gH));
        if (pointerOn) {
            const double ox = px - gPointer.x, oy = py - gPointer.y;
            I += 0.55 * exp(-(ox * ox + oy * oy) / (2 * 130 * 130)) * (gPointer.down ? 1 : 0.45);
        }
        I = clampd(I, 0, 1.25);
        const double r = RMAX * d.g * pow(min(I, 1.1), 0.78);
        const size_t idx = (size_t)d.j * gCols + d.i;
        float* a = &dotA[idx * 4];
        a[0] = (float)px; a[1] = (float)py;
        if (r < 0.28) { a[2] = a[3] = 0; dotB[idx] = 0; continue; }
        const double lum = (0.55 + 0.45 * min(1.0, I)) * d.l;
        const double wht = clampd(f.hi * 0.3 + (I - 1.05) * 0.5, 0, 0.34);
        const uint8_t R = qchan(f.c.r * lum + (255 - f.c.r * lum) * wht);
        const uint8_t G = qchan(f.c.g * lum + (235 - f.c.g * lum) * wht);
        const uint8_t B = qchan(f.c.b * lum + (255 - f.c.b * lum) * wht);
        a[2] = (float)r;
        a[3] = (I > 0.95 || wht > 0.15) ? (float)(r * 2.1) : 0.0f;
        dotB[idx] = (uint32_t)R | ((uint32_t)G << 8) | ((uint32_t)B << 16)
            | (wht > 0.25 ? 0x01000000u : 0);
    }
    gImpulses.clear();
}

// ============================================================================
// GPU upload structures + shaders
// ============================================================================
struct alignas(16) Params {
    float wh[4];      // W_css, H_css, dpr, t
    float grid[4];    // spacing, cols, rows, freeze
    float glow[4];    // cell, y0_css, gw, gh
    float grain[4];   // ox, oy, -, -
};

static const char* VS_SRC = R"HLSL(
struct VSOut { float4 pos : SV_Position; };
VSOut main(uint id : SV_VertexID) {
    VSOut o;
    o.pos = float4((id == 1) ? 3.0 : -1.0, (id == 2) ? 3.0 : -1.0, 0.0, 1.0);
    return o;
}
)HLSL";

// Pixel shader — direct port of the verified CPU mirror (.derived/refrender.mjs).
static const char* PS_SRC = R"HLSL(
Texture2D<float4>  starTex  : register(t0);   // r=a*cov, g=sp, b=ph  (css res)
Texture2D<float4>  glowTex  : register(t1);   // premult rgb, a=1     (cell grid)
Texture2D<float>   grainTex : register(t2);   // 128² tile, byte/255
StructuredBuffer<float4> dotA : register(t3); // x,y,r,haloR (css px)
StructuredBuffer<uint>   dotB : register(t4); // rgb8 | fringe<<24
SamplerState samLin : register(s0);

cbuffer Params : register(b0) {
    float4 wh;     // W, H (css), dpr, t
    float4 grid;   // spacing, cols, rows, freeze
    float4 glowP;  // cell, y0, gw, gh
    float4 grainP; // ox, oy, -, -
};

float4 main(float4 pos : SV_Position) : SV_Target {
    float W = wh.x, H = wh.y, dpr = wh.z, t = wh.w;
    float spacing = grid.x;
    bool freeze = grid.w > 0.5;
    float2 css = pos.xy / dpr;

    // base: stars below the glow band, glow overwrites its region (JS draw order)
    float3 col;
    if (css.y >= glowP.y) {
        float2 guv = float2((css.x / glowP.x + 0.5) / glowP.z,
                            ((css.y - glowP.y) / glowP.x + 0.5) / glowP.w);
        col = glowTex.SampleLevel(samLin, guv, 0).rgb;
    } else {
        float4 s = starTex.SampleLevel(samLin, css / float2(W, H), 0);
        float tw = freeze ? 1.0 : (0.65 + 0.35 * sin(t * s.g + s.b));
        col = float3(230, 233, 255) / 255.0 * s.r * tw;
    }

    // dots: nearest cell center ±1 (3×3), painter order = linear index
    int jr = (int)round((H + spacing * 0.5 - css.y) / spacing);
    int ir = (int)round((css.x - spacing * 0.5) / spacing);
    const int cols = (int)grid.y, rows = (int)grid.z;
    float covs[9]; float4 pa[9]; uint pb[9];
    float un = 1.0;
    [unroll] for (int dj = -1; dj <= 1; dj++) {
        int j = jr + dj;
        [unroll] for (int di = -1; di <= 1; di++) {
            int i = ir + di;
            int k = (dj + 1) * 3 + (di + 1);
            int idx = j * cols + i;
            float4 a = (j < 0 || j >= rows || i < 0 || i >= cols) ? float4(0, 0, 0, 0) : dotA[idx];
            pa[k] = a; pb[k] = a.z > 0 ? dotB[idx] : 0u;
            covs[k] = a.z > 0 ? saturate(a.z + 0.5 - distance(css, a.xy)) : 0;
            un *= 1.0 - covs[k];
        }
    }
    float3 fringe = 0, halo = 0;
    [unroll] for (int k2 = 0; k2 < 9; k2++) {
        float4 a = pa[k2];
        if (a.z <= 0) continue;
        float dist = distance(css, a.xy);
        float3 rgb = float3(pb[k2] & 255u, (pb[k2] >> 8) & 255u, (pb[k2] >> 16) & 255u) / 255.0;
        if (pb[k2] & 0x01000000u) {
            float crv = saturate(a.z + 0.5 - distance(css, a.xy + float2(0.8, 0))) * un;
            float cbv = saturate(a.z + 0.5 - distance(css, a.xy - float2(0.8, 0))) * un;
            fringe += (float3(255, 40, 110) * crv + float3(40, 140, 255) * cbv) * 0.75 / 255.0;
        }
        col = lerp(col, rgb, covs[k2]);
        if (a.w > 0) {
            halo += rgb * saturate(a.w + 0.5 - dist);
        }
    }
    col += fringe;          // 'lighter'; survives × Π(1−cov) where dots don't cover
    col += halo * 0.1;      // bloom, additive over dots

    // grain multiply (physical px + animated offset, exact JS tile)
    uint2 gp = ((uint2)pos.xy + uint2((uint)grainP.x, (uint)grainP.y)) & 127u;
    col *= grainTex.Load(int3((int2)gp, 0));
    return float4(col, 1.0);
}
)HLSL";

// ============================================================================
// D3D11 device + resources
// ============================================================================
static ID3D11Device* gDev = nullptr;
static ID3D11DeviceContext* gCtx = nullptr;
static IDXGISwapChain* gSwap = nullptr;
static ID3D11RenderTargetView* gRtv = nullptr;
static ID3D11VertexShader* gVs = nullptr;
static ID3D11PixelShader* gPs = nullptr;
static ID3D11SamplerState* gSam = nullptr;
static ID3D11Buffer* gCb = nullptr;
static ID3D11Buffer* gDotA = nullptr;   // float4 per dot
static ID3D11Buffer* gDotB = nullptr;   // uint per dot
static ID3D11ShaderResourceView* gDotASrv = nullptr;
static ID3D11ShaderResourceView* gDotBSrv = nullptr;
static ID3D11Texture2D* gStarTex = nullptr;
static ID3D11ShaderResourceView* gStarSrv = nullptr;
static ID3D11Texture2D* gGlowTex = nullptr;
static ID3D11ShaderResourceView* gGlowSrv = nullptr;
static ID3D11Texture2D* gGrainTex = nullptr;
static ID3D11ShaderResourceView* gGrainSrv = nullptr;
static int gGlowW = 0, gGlowH = 0, gGlowY0 = 0;

template<typename T> static inline void rls(T*& p) { if (p) { p->Release(); p = nullptr; } }

static HRESULT compileShader(const char* src, const char* entry, const char* target, ID3DBlob** out) {
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
    if (FAILED(hr)) {
        const char* msg = err ? (const char*)err->GetBufferPointer() : "unknown";
        char buf[2048]; snprintf(buf, sizeof buf, "Shader compile failed (%s): %s", entry, msg);
        MessageBoxA(nullptr, buf, "HalftoneWallpaper", MB_ICONERROR);
        if (err) err->Release();
    }
    return hr;
}

static HRESULT makeSB(ID3D11Device* dev, UINT count, UINT stride,
                      ID3D11Buffer** buf, ID3D11ShaderResourceView** srv) {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = count * stride;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = stride;
    HRESULT hr = dev->CreateBuffer(&bd, nullptr, buf);
    if (FAILED(hr)) return hr;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.Buffer.NumElements = count;
    return dev->CreateShaderResourceView(*buf, &sd, srv);
}

// Bake the star layer: per css-pixel accumulated alpha + dominant star's twinkle
// params (overlaps are rare; the dominant star's params drive the mix).
static HRESULT buildStarTex() {
    const int W = (int)ceil(gW), H = (int)ceil(gH);
    std::vector<float> px((size_t)W * H * 4, 0.0f);
    for (const Star& s : gStars) {
        const int x0 = max(0, (int)floor(s.x - s.r - 1)), x1 = min(W - 1, (int)ceil(s.x + s.r + 1));
        const int y0 = max(0, (int)floor(s.y - s.r - 1)), y1 = min(H - 1, (int)ceil(s.y + s.r + 1));
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            const float d = (float)hypot(x + 0.5 - s.x, y + 0.5 - s.y);
            const float cov = (float)clampd(s.r + 0.5 - d, 0.0, 1.0);
            if (cov <= 0) continue;
            float* p = &px[((size_t)y * W + x) * 4];
            const float add = s.a * cov;
            if (add > p[0]) { p[1] = s.sp; p[2] = s.ph; }
            p[0] = min(1.0f, p[0] + add);
        }
    }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = { px.data(), (UINT)(W * 8), 0 };
    rls(gStarSrv); rls(gStarTex);
    HRESULT hr = gDev->CreateTexture2D(&td, &init, &gStarTex);
    if (FAILED(hr)) return hr;
    return gDev->CreateShaderResourceView(gStarTex, nullptr, &gStarSrv);
}

static HRESULT buildGlowTex() {
    gGlowY0 = (int)floor(gH * 0.25);
    gGlowW = (int)ceil(gW / GLOW_CELL) + 1;
    gGlowH = (int)ceil((gH - gGlowY0) / GLOW_CELL) + 1;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = gGlowW; td.Height = gGlowH; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    rls(gGlowSrv); rls(gGlowTex);
    HRESULT hr = gDev->CreateTexture2D(&td, nullptr, &gGlowTex);
    if (FAILED(hr)) return hr;
    return gDev->CreateShaderResourceView(gGlowTex, nullptr, &gGlowSrv);
}

// Exact JS grain tile (double hash2) — bit-identical to the Canvas pattern.
static HRESULT buildGrainTex() {
    uint8_t px[GRAIN * GRAIN];
    for (int i = 0; i < GRAIN * GRAIN; i++)
        px[i] = (uint8_t)(218 + 37 * hash2((i & 127) + 0.37, (i >> 7) + 0.71) + 0.5);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = GRAIN; td.Height = GRAIN; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = { px, GRAIN, 0 };
    HRESULT hr = gDev->CreateTexture2D(&td, &init, &gGrainTex);
    if (FAILED(hr)) return hr;
    return gDev->CreateShaderResourceView(gGrainTex, nullptr, &gGrainSrv);
}

static HRESULT createSwapChainResources() {
    rls(gRtv);
    HRESULT hr = gSwap->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return hr;
    ID3D11Texture2D* bb = nullptr;
    hr = gSwap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    if (FAILED(hr)) return hr;
    hr = gDev->CreateRenderTargetView(bb, nullptr, &gRtv);
    bb->Release();
    return hr;
}

static HRESULT initD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = gPhysW; sd.BufferDesc.Height = gPhysH;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, nullptr, &gCtx);
    if (FAILED(hr)) {   // WARP fallback: wallpaper must run everywhere
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, nullptr, &gCtx);
    }
    if (FAILED(hr)) return hr;
    hr = createSwapChainResources();
    if (FAILED(hr)) return hr;

    ID3DBlob* vsb = nullptr;
    hr = compileShader(VS_SRC, "main", "vs_5_0", &vsb);
    if (FAILED(hr)) return hr;
    hr = gDev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &gVs);
    vsb->Release();
    if (FAILED(hr)) return hr;

    ID3DBlob* psb = nullptr;
    hr = compileShader(PS_SRC, "main", "ps_5_0", &psb);
    if (FAILED(hr)) return hr;
    hr = gDev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &gPs);
    psb->Release();
    if (FAILED(hr)) return hr;

    D3D11_SAMPLER_DESC sam = {};
    sam.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sam.AddressU = sam.AddressV = sam.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = gDev->CreateSamplerState(&sam, &gSam);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC cd = {};
    cd.ByteWidth = sizeof(Params);
    cd.Usage = D3D11_USAGE_DYNAMIC;
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = gDev->CreateBuffer(&cd, nullptr, &gCb);
    if (FAILED(hr)) return hr;

    hr = makeSB(gDev, (UINT)gDots.size(), 16, &gDotA, &gDotASrv);
    if (FAILED(hr)) return hr;
    hr = makeSB(gDev, (UINT)gDots.size(), 4, &gDotB, &gDotBSrv);
    if (FAILED(hr)) return hr;
    hr = buildStarTex();
    if (FAILED(hr)) return hr;
    hr = buildGlowTex();
    if (FAILED(hr)) return hr;
    return buildGrainTex();
}

// Per-frame glow texture (premultiplied bytes, like drawGlow's ImageData).
static void updateGlowTex(double t, const std::vector<Foci>& foci) {
    gGlowPx.assign((size_t)gGlowW * gGlowH * 4, 0);
    uint8_t* px = gGlowPx.data();
    for (int j = 0; j < gGlowH; j++) for (int i = 0; i < gGlowW; i++) {
        const Field f = fieldAt(i * GLOW_CELL / gH, (gH - gGlowY0 - j * GLOW_CELL) / gH, t, foci);
        const double a = min(1.0, f.I) * UNDER;
        uint8_t* p = &px[((size_t)j * gGlowW + i) * 4];
        p[0] = (uint8_t)clampd(f.c.r * a + 0.5, 0, 255);
        p[1] = (uint8_t)clampd(f.c.g * a + 0.5, 0, 255);
        p[2] = (uint8_t)clampd(f.c.b * a + 0.5, 0, 255);
        p[3] = 255;
    }
    gCtx->UpdateSubresource(gGlowTex, 0, nullptr, px, (UINT)(gGlowW * 4), 0);
}

// ============================================================================
// App state
// ============================================================================
static HWND gHwnd = nullptr;
static NOTIFYICONDATAW gNid = {};
static bool gStill = false, gNoPointer = false, gInteractive = false;
static bool gFrozen = false;              // effective freeze (still|battery|display off)
static bool gOnAC = true, gDisplayOn = true;
static uint64_t gT0 = 0;                  // ms epoch for t
static std::vector<float> gDotBufA;
static std::vector<uint32_t> gDotBufB;
static std::vector<uint8_t> gGlowPx;
static std::vector<Foci> gFoci;
static const FocusDef* gFociSet = FOCI;
static size_t gFociN = 3;
static bool gNeedsFrame = true;              // frozen → render once, then idle

static double nowSec() { return GetTickCount64() / 1000.0; }
static double curT() { return gFrozen ? 4.2 : nowSec() - gT0 / 1000.0; }

static void recomputeFreeze() {
    const bool fr = gStill || !gOnAC || !gDisplayOn;
    if (fr == gFrozen) return;
    gFrozen = fr;
    gNeedsFrame = true;                      // repaint the frame at the new t
    if (gFrozen) parkPointer();
}

static void renderFrame() {
    const double t = curT();
    static double last = 0;
    const double dt = min(0.033, last ? nowSec() - last : 0.016);
    last = nowSec();

    fociAt(t, gFociSet, gFociN, gAspect, gFoci);
    evalDots(t, dt, gFrozen, gDotBufA, gDotBufB, gFoci);
    updateGlowTex(t, gFoci);

    gCtx->UpdateSubresource(gDotA, 0, nullptr, gDotBufA.data(), 0, 0);
    gCtx->UpdateSubresource(gDotB, 0, nullptr, gDotBufB.data(), 0, 0);

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(gCtx->Map(gCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        Params* p = (Params*)ms.pData;
        p->wh[0] = (float)gW; p->wh[1] = (float)gH; p->wh[2] = (float)gDpr; p->wh[3] = (float)t;
        p->grid[0] = (float)SPACING; p->grid[1] = (float)gCols; p->grid[2] = (float)gRows;
        p->grid[3] = gFrozen ? 1.0f : 0.0f;
        p->glow[0] = (float)GLOW_CELL; p->glow[1] = (float)gGlowY0;
        p->glow[2] = (float)gGlowW; p->glow[3] = (float)gGlowH;
        // drawGrain offset: f = freeze?0:floor(t*24); ox=h11(f+.5)*128, oy=h11(f+77.7)*128
        const double f = gFrozen ? 0 : floor(t * 24);
        p->grain[0] = (float)((int)(h11(f + 0.5) * GRAIN) & 127);
        p->grain[1] = (float)((int)(h11(f + 77.7) * GRAIN) & 127);
        gCtx->Unmap(gCb, 0);
    }

    const float clear[4] = { 0, 0, 0, 1 };
    gCtx->OMSetRenderTargets(1, &gRtv, nullptr);
    gCtx->ClearRenderTargetView(gRtv, clear);
    D3D11_VIEWPORT vp = { 0, 0, (float)gPhysW, (float)gPhysH, 0, 1 };
    gCtx->RSSetViewports(1, &vp);
    gCtx->VSSetShader(gVs, nullptr, 0);
    gCtx->PSSetShader(gPs, nullptr, 0);
    gCtx->PSSetConstantBuffers(0, 1, &gCb);
    ID3D11ShaderResourceView* srvs[5] = { gStarSrv, gGlowSrv, gGrainSrv, gDotASrv, gDotBSrv };
    gCtx->PSSetShaderResources(0, 5, srvs);
    gCtx->PSSetSamplers(0, 1, &gSam);
    gCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gCtx->Draw(3, 0);
    gSwap->Present(1, 0);
}

// Rebuild after display/DPI change: resize the window + swapchain + world.
static void relayout() {
    gVsX = GetSystemMetrics(SM_XVIRTUALSCREEN); gVsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return;
    SetWindowPos(gHwnd, nullptr, gVsX, gVsY, vw, vh, SWP_NOZORDER | SWP_NOACTIVATE);
    gDpr = GetDpiForSystem() / 96.0;
    gPhysW = vw; gPhysH = vh;
    gW = vw / gDpr; gH = vh / gDpr; gAspect = gW / gH;
    gFociSet = gAspect >= 2.6 ? FOCI_WIDE : FOCI;
    gFociN = gAspect >= 2.6 ? 5 : 3;
    buildWorld();
    gDotBufA.resize(gDots.size() * 4);
    gDotBufB.resize(gDots.size());
    if (gSwap) {
        rls(gDotASrv); rls(gDotA); rls(gDotBSrv); rls(gDotB);
        createSwapChainResources();
        buildStarTex(); buildGlowTex();
        makeSB(gDev, (UINT)gDots.size(), 16, &gDotA, &gDotASrv);
        makeSB(gDev, (UINT)gDots.size(), 4, &gDotB, &gDotBSrv);
        gNeedsFrame = true;
    }
}

// ============================================================================
// Desktop embedding: the WorkerW trick (same family of technique Lively uses).
// Message 0x052C makes Progman spawn a WorkerW *behind* SHELLDLL_DefView;
// parenting our window there puts it under the icons, over the wallpaper.
// ============================================================================
static HWND findDesktopHost() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    DWORD_PTR res = 0; (void)res;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &res);
    HWND worker = nullptr;
    EnumWindows([](HWND top, LPARAM lp) -> BOOL {
        if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {
            *reinterpret_cast<HWND*>(lp) = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&worker));
    return worker ? worker : progman;      // fallback: Progman itself
}

// ============================================================================
// Tray icon — procedural 16px "halftone dots" glyph, no resource file needed.
// ============================================================================
static HICON makeIcon() {
    const int S = 32;
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof bi; bi.bV5Width = S; bi.bV5Height = -S;
    bi.bV5Planes = 1; bi.bV5BitCount = 32; bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!dib) return LoadIcon(nullptr, IDI_APPLICATION);
    uint32_t* px = (uint32_t*)bits;
    // bottom-anchored aurora dots: radius falls off with height, magenta→royal left→right
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        const int cx = (x / 4) * 4 + 2, cy = (y / 4) * 4 + 2;
        const float d = (float)hypot(x + 0.5 - cx, y + 0.5 - cy);
        const float r = 1.6f * (float)(y / (float)S);
        const float cov = clampd(r + 0.5 - d, 0, 1);
        const float u = x / (float)S;
        const uint8_t R = (uint8_t)(255 * (1 - u) + 59 * u);
        const uint8_t G = (uint8_t)(47 * (1 - u) + 91 * u);
        const uint8_t B = (uint8_t)(168 * (1 - u) + 255 * u);
        px[y * S + x] = ((uint32_t)(cov * 255) << 24) | (R << 16) | (G << 8) | B;
    }
    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    ICONINFO ii = { TRUE, 0, 0, mask, dib };
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(dib); DeleteObject(mask);
    return ic ? ic : LoadIcon(nullptr, IDI_APPLICATION);
}

enum { IDM_QUIT = 1, IDM_STILL, WM_TRAY = WM_APP + 1 };
static HMENU makeMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (gStill ? MF_CHECKED : 0), IDM_STILL, L"Freeze field (--still)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_QUIT, L"Quit");
    return m;
}

// ============================================================================
// Window proc + main
// ============================================================================
static HPOWERNOTIFY gPwrNotify = nullptr;
static const GUID GUID_CONSOLE_DISPLAY_STATE_L =
    { 0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 } };

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
            POINT p; GetCursorPos(&p);
            HMENU m = makeMenu();
            SetForegroundWindow(h);
            const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, h, nullptr);
            DestroyMenu(m);
            if (cmd == IDM_QUIT) PostQuitMessage(0);
            else if (cmd == IDM_STILL) { gStill = !gStill; recomputeFreeze(); }
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMPOWERSTATUSCHANGE) {
            SYSTEM_POWER_STATUS sps;
            if (GetSystemPowerStatus(&sps)) { gOnAC = sps.ACLineStatus != 0; recomputeFreeze(); }
        } else if (wp == PBT_POWERSETTINGCHANGE) {
            const POWERBROADCAST_SETTING* s = (const POWERBROADCAST_SETTING*)lp;
            if (s && IsEqualGUID(s->PowerSetting, GUID_CONSOLE_DISPLAY_STATE_L) && s->DataLength >= 4) {
                gDisplayOn = *(const DWORD*)s->Data != 0;
                recomputeFreeze();
            }
        }
        return TRUE;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        relayout();
        return 0;
    case WM_MOUSEMOVE:
        if (gInteractive) {                  // real input stays live even with --no-pointer
            const bool dn = (wp & MK_LBUTTON) != 0;
            feedPointer(LOWORD(lp) / gDpr, HIWORD(lp) / gDpr, dn);
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_KEYDOWN:
        if (gInteractive && wp == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static bool hasFlag(const wchar_t* cmd, const wchar_t* flag) {
    return cmd && wcsstr(cmd, flag) != nullptr;
}

static void installAutostart(bool remove) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (remove) {
        RegDeleteValueW(k, L"HalftoneWallpaper");
    } else {
        wchar_t raw[MAX_PATH];
        GetModuleFileNameW(nullptr, raw, MAX_PATH);
        wchar_t path[MAX_PATH + 4];
        swprintf_s(path, _countof(path), L"\"%s\"", raw);
        RegSetValueExW(k, L"HalftoneWallpaper", 0, REG_SZ,
            (const BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(k);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (hasFlag(cmd, L"--uninstall")) { installAutostart(true); return 0; }
    if (hasFlag(cmd, L"--install")) installAutostart(false);
    gStill = hasFlag(cmd, L"--still");
    gNoPointer = hasFlag(cmd, L"--no-pointer");
    gInteractive = hasFlag(cmd, L"--interactive");

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\HalftoneWallpaper");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    // world metrics
    gDpr = GetDpiForSystem() / 96.0;
    gPhysW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    gPhysH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    gW = gPhysW / gDpr; gH = gPhysH / gDpr; gAspect = gW / gH;
    gFociSet = gAspect >= 2.6 ? FOCI_WIDE : FOCI;
    gFociN = gAspect >= 2.6 ? 5 : 3;
    buildWorld();
    gDotBufA.resize(gDots.size() * 4);
    gDotBufB.resize(gDots.size());

    // window
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"HalftoneWallpaper";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    gVsX = GetSystemMetrics(SM_XVIRTUALSCREEN); gVsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (gInteractive) {
        gHwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Halftone Wallpaper",
            WS_POPUP | WS_VISIBLE, gVsX, gVsY, gPhysW, gPhysH,
            nullptr, nullptr, inst, nullptr);
    } else {
        HWND host = findDesktopHost();
        gHwnd = CreateWindowExW(0, wc.lpszClassName, L"Halftone Wallpaper",
            WS_CHILD | WS_VISIBLE, gVsX, gVsY, gPhysW, gPhysH,
            host, nullptr, inst, nullptr);
    }
    if (!gHwnd) { MessageBoxW(nullptr, L"CreateWindow failed", L"HalftoneWallpaper", MB_ICONERROR); return 1; }

    if (FAILED(initD3D(gHwnd))) {
        MessageBoxW(nullptr, L"Direct3D 11 init failed", L"HalftoneWallpaper", MB_ICONERROR);
        return 1;
    }

    // tray icon — the app's only chrome, like the macOS ◐ status item
    gNid.cbSize = sizeof gNid;
    gNid.hWnd = gHwnd; gNid.uID = 1;
    gNid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    gNid.uCallbackMessage = WM_TRAY;
    gNid.hIcon = makeIcon();
    wcscpy_s(gNid.szTip, L"Halftone Wallpaper");
    Shell_NotifyIconW(NIM_ADD, &gNid);

    // power watchers: battery freeze + display-off freeze (mac parity)
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) gOnAC = sps.ACLineStatus != 0;
    gPwrNotify = RegisterPowerSettingNotification(gHwnd, &GUID_CONSOLE_DISPLAY_STATE_L,
        DEVICE_NOTIFY_WINDOW_HANDLE);
    recomputeFreeze();
    gT0 = GetTickCount64();

    // pointer feed cadence ~30 Hz (matches the macOS NSEvent timer)
    uint64_t lastFeed = 0;

    // message loop: MsgWaitForMultipleObjectsEx sleeps until input or frame deadline —
    // ~0% CPU while frozen, vsync-paced otherwise.
    for (;;) {
        const DWORD wait = gFrozen && !gNeedsFrame ? INFINITE : 16;
        MsgWaitForMultipleObjectsEx(0, nullptr, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) goto done;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (gFrozen) {
            if (gNeedsFrame) { renderFrame(); gNeedsFrame = false; }
            continue;                        // sleep until input/power change
        }
        const uint64_t now = GetTickCount64();
        if (!gInteractive && !gNoPointer && now - lastFeed >= 33) {
            POINT cp;
            if (GetCursorPos(&cp))
                feedPointer((cp.x - gVsX) / gDpr, (cp.y - gVsY) / gDpr, false);
            lastFeed = now;
        }
        renderFrame();
        gNeedsFrame = false;
    }
done:
    Shell_NotifyIconW(NIM_DELETE, &gNid);
    if (gPwrNotify) UnregisterPowerSettingNotification(gPwrNotify);
    if (single) { ReleaseMutex(single); CloseHandle(single); }
    return 0;
}
