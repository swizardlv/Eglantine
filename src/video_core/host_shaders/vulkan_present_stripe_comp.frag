// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460 core

layout (location = 0) in vec2 frag_tex_coord;

layout (location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D color_texture;

void main() {
    color = texture(color_texture, frag_tex_coord);

    // The guest (Nintendo 64 - Nintendo Classics app, OoT, program id
    // 0x0400486024DEC000) bakes a horizontal blue-channel gain into its
    // 2560x1440 composite image: B is multiplied by
    //   g(u) = 1 + 0.326*sin(2*pi*80*u + 0.068)
    //            + 0.181*sin(2*pi*40*u + 2.422)
    //            + 0.063*sin(2*pi*120*u - 2.30),
    // fitted on two independent 3840x2160 screenshots (build56 and the
    // 27757f9236 merged build): the 120-cycle term (32 px period, a=0.063)
    // is the largest residual once the 80/40-cycle terms are divided out.
    // where u is the layer-x fraction (frag_tex_coord.x). Measured from two
    // independent 3840x2160 screenshots (build56 and the 27757f9236 merged
    // build): 48 px period (80 cycles across the frame), amplitude/mean
    // 0.35, phase ~4 deg, identical in both builds even though the shader
    // blit changed from 0.75x to 1.5x and the present blit from 2x to 1:1, so
    // the modulation is in the guest image upstream of this blit, not in
    // Eden's present pipeline. It is absent from the intro video frame, the
    // PS5 home screen and the launcher UI, which all share this present
    // path, and present in the guest's own 2D UI bar too, so it comes from
    // the guest's N64 video processing (VI dither/dedither + upscale), not
    // from Eden. Divide B back out. Only selected for this title (see
    // BlitScreen::SetWindowAdaptPass); every other title uses
    // vulkan_present.frag untouched.
    // NOTE: the intro video shares this path and is clean, so this also
    // divides the video's B by g(u) while it plays. Disable it with
    // EDEN_NX2_STRIPE_COMP=0 if that matters more than the gameplay stripes.
    float g = 1.0 + 0.326 * sin(6.2831853 * 80.0 * frag_tex_coord.x + 0.068)
                  + 0.181 * sin(6.2831853 * 40.0 * frag_tex_coord.x + 2.422)
                  + 0.063 * sin(6.2831853 * 120.0 * frag_tex_coord.x - 2.30);
    color.b /= max(g, 0.25);
}
