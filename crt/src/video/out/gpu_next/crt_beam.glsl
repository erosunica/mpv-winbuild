// Interval core: Mark Rejhon and Timothy Lottes, Copyright 2024, MIT.
// Port of crt-simulator.glsl at 734786a, with real, latched cycle images.
// Transfers are handled by libplacebo outside this linear-light kernel.
// History order: anteprevious, previous, current CRT cycle.
vec3 budget0 = texture(crt_prev2, crt_uv).rgb * (crt_ratio * crt_gain);
vec3 budget1 = texture(crt_prev1, crt_uv).rgb * (crt_ratio * crt_gain);
vec3 budget2 = texture(crt_current, crt_uv).rgb * (crt_ratio * crt_gain);
float row = clamp(crt_uv.y * crt_scan_map.x + crt_scan_map.y, 0.0, 1.0);
float tube = row * crt_ratio;
float exposure_end = crt_phase + 1.0;
vec3 energy = vec3(0.0);
energy += max(vec3(0.0), min(vec3(tube - crt_ratio) + budget0,
                           vec3(exposure_end))
                       - vec3(max(tube - crt_ratio, crt_phase)));
energy += max(vec3(0.0), min(vec3(tube) + budget1, vec3(exposure_end))
                       - vec3(max(tube, crt_phase)));
energy += max(vec3(0.0), min(vec3(tube + crt_ratio) + budget2,
                           vec3(exposure_end))
                       - vec3(max(tube + crt_ratio, crt_phase)));
color = vec4(energy, 1.0);
