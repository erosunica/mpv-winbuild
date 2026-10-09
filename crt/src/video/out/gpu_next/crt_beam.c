/*
 * CRT interval algorithm: Copyright 2024 Mark Rejhon (@BlurBusters) and
 * Timothy Lottes (@NOTimothyLottes), MIT. See crt/LICENSE-BlurBusters.txt.
 * Native mpv adapter: Copyright 2026 mpv-winbuild contributors, MIT.
 */
#include <inttypes.h>
#include <math.h>
#include <string.h>

#include <libplacebo/dispatch.h>
#include <libplacebo/shaders/colorspace.h>
#include <libplacebo/shaders/sampling.h>

#include "common/common.h"
#include "common/msg.h"
#include "crt_beam.h"
#include "crt_beam_math.h"

static const char beam_glsl[] =
#include "crt_beam_glsl.h"
;

struct mp_crt_beam {
    struct mp_log *log;
    pl_gpu gpu;
    struct pl_hook hook;
    struct mp_crt_beam_opts opts;
    struct mp_crt_beam_frame frame;
    struct mp_crt_clock clock;
    pl_tex images[3];            // own storage; never retain renderer-owned tex
    uint64_t image_cycle;
    uint64_t image_source;
    bool initialized;
    bool active;
    bool dispatched;
    bool broken;
    bool warned;
    struct pl_color_space color;
    struct pl_color_repr repr;
    pl_rect2df rect;
};

void mp_crt_beam_reset(struct mp_crt_beam *b)
{
    if (!b)
        return;
    b->clock = (struct mp_crt_clock){0};
    b->initialized = b->active = b->dispatched = false;
    b->broken = false;
}

static struct pl_hook_res bypass(struct mp_crt_beam *b, const char *reason)
{
    if (!b->warned) {
        MP_WARN(b, "CRT beam inactive: %s. Showing complete video frames.\n", reason);
        b->warned = true;
    }
    b->active = b->dispatched = false;
    b->initialized = false;
    b->broken = true;
    return (struct pl_hook_res){0};
}

static struct pl_color_space beam_transfer(const struct pl_color_space *color)
{
    struct pl_color_space out = *color;
    // The canonical duration budget is normalized [0, 1], not absolute nits.
    // Avoid libplacebo's inferred 1000:1 black floor: applying that floor to
    // each refresh destroys short near-black phosphor tails on an OLED.
    out.hdr = (struct pl_hdr_metadata){
        .min_luma = PL_COLOR_HDR_BLACK, .max_luma = PL_COLOR_SDR_WHITE,
    };
    return out;
}

static bool capture(struct mp_crt_beam *b, const struct pl_hook_params *p,
                    pl_tex target)
{
    pl_shader sh = pl_dispatch_begin(p->dispatch);
    bool ok = pl_shader_sample_nearest(sh, &(struct pl_sample_src){
        .tex = p->tex,
        .components = p->components,
        .new_w = target->params.w,
        .new_h = target->params.h,
    });
    struct pl_color_repr repr = p->repr;
#if PL_API_VER >= 368
    pl_shader_decode_color_ex(sh, pl_color_decode_args(.repr = &repr));
#else
    pl_shader_decode_color(sh, &repr, NULL);
#endif
    ok = ok && pl_shader_custom(sh, &(struct pl_custom_shader){
        .input = PL_SHADER_SIG_COLOR, .output = PL_SHADER_SIG_COLOR,
        .body = "color.rgb = clamp(color.rgb, vec3(0.0), vec3(1.0));",
    });
    struct pl_color_space transfer = beam_transfer(&p->color);
    pl_shader_linearize(sh, &transfer);
    if (!ok) {
        pl_dispatch_abort(p->dispatch, &sh);
        return false;
    }
    return pl_dispatch_finish(p->dispatch, &(struct pl_dispatch_params){
        .shader = &sh, .target = target,
    });
}

static struct pl_hook_res render(void *priv, const struct pl_hook_params *p)
{
    struct mp_crt_beam *b = priv;
    if (!b->active || b->dispatched)
        return (struct pl_hook_res){0};
    if (pl_color_transfer_is_hdr(p->color.transfer) ||
        (p->orig_color && pl_color_transfer_is_hdr(p->orig_color->transfer)))
        return bypass(b, "only SDR input and output are supported");
    if (p->repr.sys != PL_COLOR_SYSTEM_RGB || p->color.transfer == PL_COLOR_TRC_UNKNOWN)
        return bypass(b, "output RGB transfer is not defined");

    int w = p->tex->params.w, h = p->tex->params.h;
    bool geometry_changed = !b->images[0] || b->images[0]->params.w != w ||
                            b->images[0]->params.h != h;
    if (geometry_changed || (b->initialized &&
        (!pl_color_space_equal(&b->color, &p->color) ||
         !pl_color_repr_equal(&b->repr, &p->repr) ||
         memcmp(&b->rect, &p->rect, sizeof(p->rect)))))
    {
        b->initialized = false;
        b->clock.present = b->clock.cycle = 0;
        b->clock.phase = 0;
    }

    pl_fmt fmt = pl_find_fmt(b->gpu, PL_FMT_FLOAT, 4, 16, 0,
                            PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE);
    if (!fmt)
        return bypass(b, "no sampleable/renderable RGBA float history format");
    for (int i = 0; i < 3; i++) {
        if (!pl_tex_recreate(b->gpu, &b->images[i], &(struct pl_tex_params){
                .w = w, .h = h, .format = fmt,
                .sampleable = true, .renderable = true,
                .blit_src = !!(fmt->caps & PL_FMT_CAP_BLITTABLE),
                .blit_dst = !!(fmt->caps & PL_FMT_CAP_BLITTABLE),
            }))
            return bypass(b, "failed allocating history textures");
    }

    if (!b->initialized) {
        // Defined warm start: assume the first image existed before this epoch.
        // Three independent initialized images avoid undefined reads/seams.
        for (int i = 0; i < 3; i++) {
            if (!capture(b, p, b->images[i]))
                return bypass(b, "failed initializing history");
        }
        b->image_cycle = b->clock.cycle;
        b->image_source = b->frame.source_id;
        b->color = p->color;
        b->repr = p->repr;
        b->rect = p->rect;
        b->initialized = true;
    } else if (b->image_cycle != b->clock.cycle) {
        pl_tex oldest = b->images[0];
        b->images[0] = b->images[1];
        b->images[1] = b->images[2];
        b->images[2] = oldest;
        if (!capture(b, p, b->images[2]))
            return bypass(b, "failed capturing CRT cycle image");
        b->image_cycle = b->clock.cycle;
        b->image_source = b->frame.source_id;
    }

    static const char *names[] = {"crt_prev2", "crt_prev1", "crt_current"};
    struct pl_shader_desc descs[3];
    for (int i = 0; i < 3; i++) {
        descs[i] = (struct pl_shader_desc){
            .desc = {.name = names[i], .type = PL_DESC_SAMPLED_TEX},
            .binding = {.object = b->images[i], .sample_mode = PL_TEX_SAMPLE_NEAREST},
        };
    }
    float ratio = b->clock.ratio, phase = b->clock.phase;
    float gain = b->opts.gain > 0 ? b->opts.gain : 1.0 / b->clock.ratio;
    float rect_h = p->rect.y1 - p->rect.y0;
    float screen_h = b->frame.screen_height;
    if (rect_h == 0 || screen_h <= 0)
        return bypass(b, "invalid output geometry");
    float scale = (p->dst_rect.y1 - p->dst_rect.y0) / (rect_h * screen_h);
    float scan_map[] = {h * scale, p->dst_rect.y0 / screen_h - p->rect.y0 * scale};
    struct pl_shader_var vars[] = {
        {.var = pl_var_float("crt_ratio"), .data = &ratio, .dynamic = true},
        {.var = pl_var_float("crt_phase"), .data = &phase, .dynamic = true},
        {.var = pl_var_float("crt_gain"), .data = &gain, .dynamic = true},
        {.var = pl_var_vec2("crt_scan_map"), .data = scan_map, .dynamic = true},
    };
    const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    struct pl_shader_va va = {
        .attr = {.name = "crt_uv", .fmt = pl_find_vertex_fmt(b->gpu, PL_FMT_FLOAT, 2)},
        .data = {uv[0], uv[1], uv[2], uv[3]},
    };
    pl_shader sh = pl_dispatch_begin(p->dispatch);
    if (!pl_shader_custom(sh, &(struct pl_custom_shader){
            .description = "Blur Busters CRT electron beam",
            .body = beam_glsl, .input = PL_SHADER_SIG_NONE,
            .output = PL_SHADER_SIG_COLOR, .output_w = w, .output_h = h,
            .descriptors = descs, .num_descriptors = 3,
            .variables = vars, .num_variables = MP_ARRAY_SIZE(vars),
            .vertex_attribs = &va, .num_vertex_attribs = 1,
        }))
    {
        pl_dispatch_abort(p->dispatch, &sh);
        return bypass(b, "failed building CRT shader");
    }
    struct pl_color_space transfer = beam_transfer(&p->color);
    pl_shader_delinearize(sh, &transfer);
    pl_shader_encode_color(sh, &p->repr);
    pl_tex output = p->get_tex(p->priv, w, h);
    if (!output || !pl_dispatch_finish(p->dispatch, &(struct pl_dispatch_params){
            .shader = &sh, .target = output,
        }))
    {
        if (sh)
            pl_dispatch_abort(p->dispatch, &sh);
        return bypass(b, "failed dispatching CRT shader");
    }
    b->dispatched = true;
    b->warned = false;
    if (b->opts.debug) {
        MP_INFO(b, "CRT present=%"PRIu64" cycle=%"PRIu64" source=%"PRIu64
                " latched=%"PRIu64" pts=%.9f phase=%.9f ratio=%.9f gain=%.6f\n",
                b->clock.present, b->clock.cycle, b->frame.source_id,
                b->image_source, b->frame.pts, b->clock.phase, b->clock.ratio, gain);
    }
    return (struct pl_hook_res){
        .output = PL_HOOK_SIG_TEX, .tex = output, .repr = p->repr,
        .color = p->color, .components = p->components, .rect = p->rect,
    };
}

struct mp_crt_beam *mp_crt_beam_create(void *parent, struct mp_log *log, pl_gpu gpu)
{
    struct mp_crt_beam *b = talloc_zero(parent, struct mp_crt_beam);
    b->log = log;
    b->gpu = gpu;
    b->hook = (struct pl_hook){
        .stages = PL_HOOK_OUTPUT, .input = PL_HOOK_SIG_TEX, .priv = b,
        .hook = render, .signature = UINT64_C(0x4352544245414d01),
    };
    return b;
}

const struct pl_hook *mp_crt_beam_hook(struct mp_crt_beam *b)
{
    return b ? &b->hook : NULL;
}

void mp_crt_beam_prepare(struct mp_crt_beam *b,
                         const struct mp_crt_beam_opts *opts,
                         const struct mp_crt_beam_frame *frame)
{
    if (!b)
        return;
    bool changed = b->opts.enabled != opts->enabled || b->opts.scans != opts->scans ||
                   b->opts.gain != opts->gain;
    b->opts = *opts;
    b->frame = *frame;
    b->dispatched = false;
    if (changed) {
        mp_crt_beam_reset(b);
        b->warned = false;
    }
    if (!opts->enabled) {
        mp_crt_beam_reset(b);
        return;
    }
    if (frame->stationary) {
        mp_crt_beam_reset(b);
        return;
    }
    if (!frame->timed || !isfinite(frame->source_fps) || frame->source_fps <= 0 ||
        !isfinite(frame->display_hz) || frame->display_hz <= 0)
    {
        mp_crt_beam_reset(b);
        bypass(b, "requires display-sync, known video cadence, and no interpolation");
        return;
    }
    double ratio = frame->display_hz / (opts->scans * frame->source_fps);
    // A nominal 100 Hz mode can be measured a few ppm below 100 Hz. Accept
    // only 0.1% clock tolerance at the 2:1 boundary, never an integer divisor.
    if (ratio < 2.0 * (1.0 - 0.001)) {
        mp_crt_beam_reset(b);
        bypass(b, "native:CRT ratio is below 2:1");
        return;
    }
    ratio = fmax(2.0, ratio);
    if (!b->clock.ratio || fabs(ratio / b->clock.ratio - 1.0) > 0.001) {
        mp_crt_beam_reset(b);
        b->clock.ratio = ratio;
        MP_INFO(b, "CRT beam: %.6f fps, %.6f Hz display, %d scans, R=%.9f.\n",
                frame->source_fps, frame->display_hz, opts->scans, ratio);
    }
    // Keep an epoch's ratio stable against noisy estimates; never round to integer.
    b->active = !b->broken;
}

void mp_crt_beam_present(struct mp_crt_beam *b, bool success)
{
    if (!b || !b->active || !b->dispatched)
        return;
    if (!success) {
        bypass(b, "presentation failed");
        b->broken = true;
        return;
    }
    mp_crt_clock_tick(&b->clock);
    b->dispatched = false;
}

void mp_crt_beam_skipped(struct mp_crt_beam *b, int64_t skipped)
{
    if (b && b->active && skipped > 0) {
        bypass(b, "presentation feedback reported skipped refreshes");
        b->broken = true;
    }
}

void mp_crt_beam_destroy(struct mp_crt_beam **ptr)
{
    struct mp_crt_beam *b = *ptr;
    if (!b)
        return;
    for (int i = 0; i < 3; i++)
        pl_tex_destroy(b->gpu, &b->images[i]);
    talloc_free(b);
    *ptr = NULL;
}
