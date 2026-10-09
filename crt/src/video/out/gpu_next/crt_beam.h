/* MIT; see crt/LICENSE-BlurBusters.txt. */
#ifndef MPV_CRT_BEAM_H
#define MPV_CRT_BEAM_H

#include <stdbool.h>
#include <stdint.h>
#include <libplacebo/shaders/custom.h>

struct mp_log;
struct mp_crt_beam;

struct mp_crt_beam_opts {
    bool enabled;
    int scans;
    float gain;                 // 0 = automatic (1 / native:CRT ratio)
    bool debug;
};

struct mp_crt_beam_frame {
    bool timed;                 // display-sync, moving SDR video, no interpolation
    bool stationary;            // pause/still: quietly show a complete image
    uint64_t source_id;
    double pts;
    double source_fps;
    double display_hz;
    int screen_height;
};

struct mp_crt_beam *mp_crt_beam_create(void *parent, struct mp_log *log, pl_gpu gpu);
void mp_crt_beam_destroy(struct mp_crt_beam **beam);
void mp_crt_beam_reset(struct mp_crt_beam *beam);
void mp_crt_beam_prepare(struct mp_crt_beam *beam,
                         const struct mp_crt_beam_opts *opts,
                         const struct mp_crt_beam_frame *frame);
const struct pl_hook *mp_crt_beam_hook(struct mp_crt_beam *beam);
void mp_crt_beam_present(struct mp_crt_beam *beam, bool success);
void mp_crt_beam_skipped(struct mp_crt_beam *beam, int64_t skipped);

#endif
