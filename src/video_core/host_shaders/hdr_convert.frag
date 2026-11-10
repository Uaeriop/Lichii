// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#version 460

// Converts a present layer between SDR (sRGB, BT.709) and HDR10 (PQ, BT.2020) before it is drawn
// to a target of the other kind (video_core/hdr_output.h, renderer_vulkan/present/hdr_convert.h).

layout (location = 0) in vec2 texcoord;
layout (location = 0) out vec4 frag_color;

layout (binding = 0) uniform sampler2D input_texture;

// The vertex stage's tex_scale and tex_offset take the first 16 bytes.
layout (push_constant) uniform Parameters {
    layout (offset = 16) int mode;          // 1: sRGB -> PQ, 2: PQ -> sRGB
    layout (offset = 20) float paper_white; // nits of SDR white
    layout (offset = 24) float peak;        // nits of the HDR peak
};

const int MODE_SRGB_TO_PQ = 1;
const int MODE_PQ_TO_SRGB = 2;

// SMPTE ST 2084
const float PQ_M1 = 2610.0 / 16384.0;
const float PQ_M2 = 2523.0 / 4096.0 * 128.0;
const float PQ_C1 = 3424.0 / 4096.0;
const float PQ_C2 = 2413.0 / 4096.0 * 32.0;
const float PQ_C3 = 2392.0 / 4096.0 * 32.0;
const float PQ_PEAK = 10000.0;

const float TONEMAP_KNEE = 0.75;

// Linear BT.709 <-> linear BT.2020 (ITU-R BT.2087)
const mat3 BT709_TO_BT2020 = mat3(
    0.6274, 0.0691, 0.0164,
    0.3293, 0.9195, 0.0880,
    0.0433, 0.0114, 0.8956);
const mat3 BT2020_TO_BT709 = mat3(
     1.6605, -0.1246, -0.0182,
    -0.5876,  1.1329, -0.1006,
    -0.0728, -0.0083,  1.1187);

vec3 SrgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 LinearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

vec3 PqEncode(vec3 nits) {
    vec3 y = pow(clamp(nits / PQ_PEAK, 0.0, 1.0), vec3(PQ_M1));
    return pow((PQ_C1 + PQ_C2 * y) / (1.0 + PQ_C3 * y), vec3(PQ_M2));
}

vec3 PqDecode(vec3 signal) {
    vec3 p = pow(clamp(signal, 0.0, 1.0), vec3(1.0 / PQ_M2));
    return PQ_PEAK * pow(max(p - PQ_C1, 0.0) / (PQ_C2 - PQ_C3 * p), vec3(1.0 / PQ_M1));
}

// VideoCore::HdrOutput::TonemapToSdr: the identity up to the knee, then a curve that reaches SDR
// white at the HDR peak.
float TonemapToSdr(float value, float peak_over_white) {
    if (value <= TONEMAP_KNEE || peak_over_white <= 1.0) {
        return min(value, 1.0);
    }
    float span = max(peak_over_white - TONEMAP_KNEE, 1e-6);
    float t = min((value - TONEMAP_KNEE) / span, 1.0);
    return TONEMAP_KNEE + (1.0 - TONEMAP_KNEE) * (1.0 - (1.0 - t) * (1.0 - t));
}

void main() {
    vec4 color = texture(input_texture, texcoord);
    if (mode == MODE_SRGB_TO_PQ) {
        vec3 rgb = BT709_TO_BT2020 * SrgbToLinear(clamp(color.rgb, 0.0, 1.0));
        color.rgb = PqEncode(rgb * paper_white);
    } else if (mode == MODE_PQ_TO_SRGB) {
        // In units of paper white; the luminance is tonemapped and the color scaled with it, so
        // highlights keep their hue.
        vec3 rgb = max(BT2020_TO_BT709 * PqDecode(color.rgb) / paper_white, 0.0);
        float luminance = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        if (luminance > 0.0) {
            rgb *= TonemapToSdr(luminance, peak / paper_white) / luminance;
        }
        color.rgb = LinearToSrgb(clamp(rgb, 0.0, 1.0));
    }
    frag_color = color;
}
