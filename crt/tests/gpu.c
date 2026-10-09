/* Integration test: real libplacebo hook, textures and pixel readback. */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libplacebo/log.h>
#include <libplacebo/renderer.h>
#ifdef CRT_TEST_D3D11
#define COBJMACROS
#include <libplacebo/d3d11.h>
#else
#include <libplacebo/vulkan.h>
#endif

#include "common/msg.h"
#include "video/out/gpu_next/crt_beam.h"

/* Only replace mpv's logger. The adapter, allocator and shader are production code. */
void mp_msg(struct mp_log *log, int lev, const char *fmt, ...)
{
    (void)log;
    if (lev > MSGL_WARN)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

enum { W = 8, H = 32, N = 120 };

#ifdef CRT_TEST_D3D11
static ID3D11Texture2D *readback_source;
/* Wine omits CPU_LOCKABLE from format capabilities. Read a wrapped render
 * target with the standard D3D11 staging API, without changing GPU caps. */
static bool download(pl_gpu gpu, pl_tex tex, void *dst)
{
    (void)tex;
    ID3D11Device *dev = pl_d3d11_get(gpu)->device;
    ID3D11DeviceContext *ctx;
    ID3D11Device_GetImmediateContext(dev, &ctx);
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(readback_source, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging = NULL;
    HRESULT hr = ID3D11Device_CreateTexture2D(dev, &desc, NULL, &staging);
    assert(SUCCEEDED(hr));
    pl_gpu_finish(gpu);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging,
                                    (ID3D11Resource *)readback_source);
    D3D11_MAPPED_SUBRESOURCE map;
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0,
                               D3D11_MAP_READ, 0, &map);
    assert(SUCCEEDED(hr));
    for (int y = 0; y < H; y++)
        memcpy((char *)dst + y * W * 16, (char *)map.pData + y * map.RowPitch, W * 16);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    ID3D11Texture2D_Release(staging);
    ID3D11DeviceContext_Release(ctx);
    return true;
}
#else
static bool download(pl_gpu gpu, pl_tex tex, void *dst)
{
    return pl_tex_download(gpu, pl_tex_transfer_params(.tex = tex, .ptr = dst));
}
#endif

static double signal(int source, int x, int channel)
{
    static const double v[] = {0.0, 0.1, 1.0, 0.25, 0.7, 0.0, 0.9};
    if (source < 0)
        source = 0; // documented warm start
    return v[(source + x + channel) % 7];
}

static double encode(double v, enum pl_color_transfer trc)
{
    if (trc == PL_COLOR_TRC_LINEAR)
        return v;
    if (trc == PL_COLOR_TRC_SRGB)
        return v <= .0031308 ? v * 12.92 : 1.055 * pow(v, 1 / 2.4) - .055;
    return pow(v, 1 / (trc == PL_COLOR_TRC_GAMMA24 ? 2.4 : 2.2));
}

static double decode(double v, enum pl_color_transfer trc)
{
    if (trc == PL_COLOR_TRC_LINEAR)
        return v;
    if (trc == PL_COLOR_TRC_SRGB)
        return v <= .04045 ? v / 12.92 : pow((v + .055) / 1.055, 2.4);
    return pow(v, trc == PL_COLOR_TRC_GAMMA24 ? 2.4 : 2.2);
}

/* Independent absolute-time reference: enumerate emitted phosphor intervals. */
static double reference(int n, double ratio, double gain, int scans,
                        int x, double row, int channel)
{
    double result = 0;
    int cycle = floor(n / ratio + 1e-10);
    for (int k = cycle - 5; k <= cycle + 2; k++) {
        /* A cycle is latched at the first submitted refresh belonging to it. */
        int latch = k < 0 ? 0 : ceil(k * ratio - 1e-9);
        int source = floor(latch / (ratio * scans) + 1e-10);
        double start = (k + 1 + row) * ratio;
        double end = start + signal(source, x, channel) * ratio * gain;
        result += fmax(0, fmin(end, n + 1.0) - fmax(start, n));
    }
    return result;
}

static void run(pl_gpu gpu, pl_renderer rr, pl_tex input, pl_tex output,
                struct mp_crt_beam *beam, double ratio, int scans,
                enum pl_color_transfer transfer, float gain_opt, int border)
{
    float in[H][W][4], out[H][W][4];
    struct pl_color_space color = pl_color_space_srgb;
    color.transfer = transfer;
    struct pl_frame source = {
        .num_planes = 1,
        .planes = {{.texture = input, .components = 4,
                    .component_mapping = {0, 1, 2, 3}}},
        .repr = pl_color_repr_rgb, .color = color,
    };
    struct pl_frame target = source;
    target.planes[0].texture = output;
    target.crop = (pl_rect2df){0, border, W, H - border};
    const struct pl_hook *hook = mp_crt_beam_hook(beam);
    struct pl_render_params params = {
        .hooks = &hook, .num_hooks = 1,
        .border = PL_CLEAR_SKIP,
    };
    struct mp_crt_beam_opts opts = {
        .enabled = true, .scans = scans, .gain = gain_opt,
    };
    double gain = gain_opt > 0 ? gain_opt : 1.0 / ratio;
    double worst = 0, worst_encoded = 0;
    mp_crt_beam_reset(beam);
    pl_renderer_flush_cache(rr);
    for (int n = 0; n < N; n++) {
        int id = floor(n / (ratio * scans) + 1e-10);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                for (int c = 0; c < 3; c++)
                    in[y][x][c] = encode(signal(id, x, c), transfer);
                in[y][x][3] = 1;
            }
        }
        assert(pl_tex_upload(gpu, pl_tex_transfer_params(.tex = input, .ptr = in)));
        mp_crt_beam_prepare(beam, &opts, &(struct mp_crt_beam_frame){
            .timed = true, .source_id = id, .pts = id / 25.0,
            .source_fps = 25, .display_hz = ratio * scans * 25,
            .screen_height = H,
        });
        assert(pl_render_image(rr, &source, &target, &params));
        assert(download(gpu, output, out));
        for (int y = border; y < H - border; y++) {
            for (int x = 0; x < W; x++) {
                for (int c = 0; c < 3; c++) {
                    double expected = encode(reference(n, ratio, gain, scans, x,
                                                       (y + .5) / H, c), transfer);
                    // Half-float intermediates can amplify tiny energy errors
                    // near black after gamma encoding. Bound emitted energy.
                    double error = fabs(decode(fmax(0, out[y][x][c]), transfer)
                                        - decode(expected, transfer));
                    worst = fmax(worst, error);
                    worst_encoded = fmax(worst_encoded, fabs(out[y][x][c] - expected));
                    if (!isfinite(error) || error > .0035) {
                        fprintf(stderr, "FAIL R=%g scans=%d trc=%d gain=%g n=%d"
                                " pixel=%d,%d,%d got=%g expected=%g\n",
                                ratio, scans, transfer, gain, n, x, y, c,
                                out[y][x][c], expected);
                        abort();
                    }
                }
            }
        }
        mp_crt_beam_present(beam, true);
    }
    printf("GPU R=%g scans=%d trc=%d gain=%g border=%d: %d pixels, energy error %.6g, encoded %.6g\n",
           ratio, scans, transfer, gain, border,
           N * W * (H - 2 * border) * 3, worst, worst_encoded);

    /* Pause must reveal a complete image instead of freezing a partial sweep. */
    mp_crt_beam_prepare(beam, &opts, &(struct mp_crt_beam_frame){
        .timed = false, .stationary = true,
        .source_fps = 25, .display_hz = ratio * scans * 25,
        .screen_height = H,
    });
    assert(pl_render_image(rr, &source, &target, &params));
    assert(download(gpu, output, out));
    for (int y = border; y < H - border; y++)
        for (int x = 0; x < W; x++)
            for (int c = 0; c < 3; c++)
                assert(fabs(out[y][x][c] - in[y][x][c]) < .001);

    struct mp_crt_beam_frame moving = {
        .timed = true, .source_fps = 25, .display_hz = ratio * scans * 25,
        .screen_height = H,
    };
    mp_crt_beam_prepare(beam, &opts, &moving);
    assert(pl_render_image(rr, &source, &target, &params));
    mp_crt_beam_present(beam, true);
    mp_crt_beam_skipped(beam, 1);
    for (int n = 0; n < 2; n++) {
        // Failure remains inactive on subsequent frames until reset.
        mp_crt_beam_prepare(beam, &opts, &moving);
        assert(pl_render_image(rr, &source, &target, &params));
        assert(download(gpu, output, out));
        for (int y = border; y < H - border; y++)
            for (int x = 0; x < W; x++)
                for (int c = 0; c < 3; c++)
                    assert(fabs(out[y][x][c] - in[y][x][c]) < .001);
    }
    mp_crt_beam_reset(beam);
    moving.display_hz = 1.5 * scans * 25; // reject insufficient native refresh
    mp_crt_beam_prepare(beam, &opts, &moving);
    assert(pl_render_image(rr, &source, &target, &params));
    assert(download(gpu, output, out));
    for (int y = border; y < H - border; y++)
        for (int x = 0; x < W; x++)
            for (int c = 0; c < 3; c++)
                assert(fabs(out[y][x][c] - in[y][x][c]) < .001);
}

int main(void)
{
    pl_log log = pl_log_create(PL_API_VER, pl_log_params(
        .log_cb = pl_log_simple, .log_level = PL_LOG_WARN));
#ifdef CRT_TEST_D3D11
    pl_d3d11 backend = pl_d3d11_create(log, pl_d3d11_params(.allow_software = true));
#else
    pl_vulkan backend = pl_vulkan_create(log, pl_vulkan_params(.allow_software = true));
#endif
    assert(backend);
    pl_gpu gpu = backend->gpu;
    enum pl_fmt_caps caps = PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE;
#ifndef CRT_TEST_D3D11
    caps |= PL_FMT_CAP_HOST_READABLE;
#endif
    pl_fmt fmt = pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 32, 0, caps);
    assert(fmt && fmt->texel_size == 16);
    pl_tex input = pl_tex_create(gpu, pl_tex_params(
        .w = W, .h = H, .format = fmt, .sampleable = true, .host_writable = true));
#ifdef CRT_TEST_D3D11
    D3D11_TEXTURE2D_DESC desc = {
        .Width = W, .Height = H, .MipLevels = 1, .ArraySize = 1,
        .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc.Count = 1,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    };
    assert(SUCCEEDED(ID3D11Device_CreateTexture2D(backend->device, &desc,
                                                NULL, &readback_source)));
    pl_tex output = pl_d3d11_wrap(gpu, pl_d3d11_wrap_params(
        .tex = (ID3D11Resource *)readback_source));
#else
    pl_tex output = pl_tex_create(gpu, pl_tex_params(
        .w = W, .h = H, .format = fmt, .renderable = true, .host_readable = true));
#endif
    assert(input && output);
    pl_renderer rr = pl_renderer_create(log, gpu);
    struct mp_crt_beam *beam = mp_crt_beam_create(NULL, NULL, gpu);
    const double ratios[] = {2, 2.5, 4, 5, 2.4, 4.8};
    for (unsigned i = 0; i < sizeof(ratios) / sizeof(ratios[0]); i++) {
        run(gpu, rr, input, output, beam, ratios[i], 1, PL_COLOR_TRC_LINEAR, 0, 0);
        run(gpu, rr, input, output, beam, ratios[i], 2, PL_COLOR_TRC_GAMMA22, 0, 0);
    }
    run(gpu, rr, input, output, beam, 2.5, 2, PL_COLOR_TRC_GAMMA22, .7, 0);
    run(gpu, rr, input, output, beam, 2.5, 2, PL_COLOR_TRC_GAMMA22, 1, 0);
    run(gpu, rr, input, output, beam, 2.5, 2, PL_COLOR_TRC_LINEAR, 0, 4);
    run(gpu, rr, input, output, beam, 2.5, 2, PL_COLOR_TRC_GAMMA24, 0, 0);
    run(gpu, rr, input, output, beam, 2.5, 2, PL_COLOR_TRC_SRGB, 0, 0);
    mp_crt_beam_destroy(&beam);
    pl_renderer_destroy(&rr);
    pl_tex_destroy(gpu, &input);
    pl_tex_destroy(gpu, &output);
#ifdef CRT_TEST_D3D11
    ID3D11Texture2D_Release(readback_source);
    pl_d3d11_destroy(&backend);
#else
    pl_vulkan_destroy(&backend);
#endif
    pl_log_destroy(&log);
    puts("GPU interval reference and complete-frame pause passed.");
}
