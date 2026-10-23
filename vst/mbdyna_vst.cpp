/* =============================================================================
 * mbdyna_vst.cpp - MB Dyna: LSP's Multiband Dynamics Processor (lsp-plugins-mb-dyna-processor, stereo) as a VST2
 * insert effect for the MPC OS plugin host (Force, MPC Live/One/X/Key), armhf.
 *
 * The audio is processed by LSP's own plugin class, unchanged (lsp/plugin/src/main/plug/mb_dyna_processor.cpp)
 * with LSP's DSP libraries (lsp-dsp-lib with its NEON code, lsp-dsp-units). This file only translates the MPC page's
 * controls into the plugin's own ports, the way LSP's UI would set them:
 *   - four bands: splits 1..3 are LSP's split points 2..4 (cbe_1..3 on, cbe_4..7 off), bands 1..4 are LSP's 0..3;
 *   - each band is a compressor: LSP's first curve point at THRESHOLD (threshold = gain, so the curve is unity
 *     below it), the high-level ratio as RATIO, low-level ratio 1, the default attack/release times, makeup;
 *   - one DETECTOR mode, REACT time and SC BOOST for all bands; SC BOOST defaults to NONE here (LSP's default is
 *     PINK BT, which weights the detector towards the lows), so a band's threshold reads as its level;
 *   - the FFT analysis for LSP's graphs is switched off.
 * Everything else stays at LSP's defaults. The shell (fxshell.h) handles the VST2 side, slots and the page.
 *
 * GNU Lesser General Public License v3 or later (see ../LICENSE), as LSP. Unofficial port; LSP Project:
 * https://lsp-plug.in, (C) Vladimir Sadovnikov and the LSP contributors. Not affiliated with the LSP Project.
 * ========================================================================== */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "params.h"
#include "lsp_host.h"
#include "fxhooks.h"
#include <private/plugins/mb_dyna_processor.h>

using namespace lsp;

namespace {

enum { MODE, INPUT, OUTPUT, MIX, SCMODE, REACT, BOOST, SPLIT1, SPLIT2, SPLIT3, BAND0 };
enum { B_ON, B_SOLO, B_THRESH, B_RATIO, B_ATTACK, B_RELEASE, B_MAKEUP, B_COUNT };
const int NBANDS = 4;

int key_index(const char *key) {
    for (int i = 0; i < NPARAMS; i++) if (!std::strcmp(PARAMS[i].key, key)) return i;
    return -1;
}
float db_to_gain(float db) { return std::pow(10.0f, db / 20.0f); }

struct Engine {
    lsphost::LspHost host;
    int ix[BAND0 + NBANDS * B_COUNT];   /* VST index of each control above */
};

int band_param(int b, int what) { return BAND0 + b * B_COUNT + what; }

void *create(const char *) {
    lsphost::dsp_init_once();
    Engine *e = new Engine();
    static const char *glob[] = {"mode", "input", "output", "mix", "scmode", "react", "boost", "split1", "split2", "split3"};
    static const char *band[] = {"on", "solo", "thresh", "ratio", "attack", "release", "makeup"};
    for (int i = 0; i < BAND0; i++) e->ix[i] = key_index(glob[i]);
    for (int b = 0; b < NBANDS; b++)
        for (int k = 0; k < B_COUNT; k++) {
            char key[32];
            std::snprintf(key, sizeof key, "%s%d", band[k], b + 1);
            e->ix[band_param(b, k)] = key_index(key);
        }
    const meta::plugin_t *meta = &meta::mb_dyna_processor_stereo;
    e->host.bind(new plugins::mb_dyna_processor(meta, false, plugins::mb_dyna_processor::MBDP_STEREO), meta);
    /* no graphs on MPC: switch LSP's FFT analysis off */
    for (const char *id : {"ife_l", "ofe_l", "ife_r", "ofe_r"}) e->host.set(id, 0.0f);
    /* four bands: split points 2..4 on, the others off */
    for (int s = 1; s <= 7; s++) {
        char id[16];
        std::snprintf(id, sizeof id, "cbe_%d", s);
        e->host.set(id, s <= NBANDS - 1 ? 1.0f : 0.0f);
    }
    return e;
}
void destroy(void *p) { delete (Engine *)p; }
void rate(void *p, float sr) { ((Engine *)p)->host.sample_rate(sr); }

void apply(void *p, const float *v) {
    Engine *e = (Engine *)p;
    lsphost::LspHost &h = e->host;
    auto val = [&](int which) { return v[e->ix[which]]; };
    h.set("mode", val(MODE) >= 0.5f ? 1.0f : 0.0f);   /* 0 Classic, 1 Modern (2 Linear Phase is left out) */
    h.set("g_in", db_to_gain(val(INPUT)));
    h.set("g_out", db_to_gain(val(OUTPUT)));
    h.set("drywet", val(MIX));
    h.set("envb", val(BOOST));   /* side-chain envelope boost: 0 None, 1 Pink BT (LSP's default), 2 Pink MT, 3 Brown BT, 4 Brown MT */
    float split[3] = {val(SPLIT1), val(SPLIT2), val(SPLIT3)};
    for (int s = 0; s < 3; s++) {
        char id[16];
        std::snprintf(id, sizeof id, "sf_%d", s + 1);
        h.set(id, split[s]);
    }
    for (int b = 0; b < NBANDS; b++) {
        char id[24];
        auto set = [&](const char *name, float x) { std::snprintf(id, sizeof id, "%s_%d", name, b); h.set(id, x); };
        const float thr = db_to_gain(val(band_param(b, B_THRESH)));
        set("pe", val(band_param(b, B_ON)) >= 0.5f ? 1.0f : 0.0f);
        set("bs", val(band_param(b, B_SOLO)) >= 0.5f ? 1.0f : 0.0f);
        set("tl0", thr);
        set("gl0", thr);
        set("pe0", 1.0f);
        set("llr", 1.0f);
        set("hlr", val(band_param(b, B_RATIO)));
        set("atd", val(band_param(b, B_ATTACK)));
        set("rtd", val(band_param(b, B_RELEASE)));
        set("mk", db_to_gain(val(band_param(b, B_MAKEUP))));
        set("scm", val(SCMODE));
        set("scr", val(REACT));
    }
}

void process(void *p, const float *il, const float *ir, float *ol, float *orr, int n) {
    lsphost::LspHost &h = ((Engine *)p)->host;
    h.port("in_l")->buf = (void *)il;
    h.port("in_r")->buf = (void *)ir;
    h.port("out_l")->buf = ol;
    h.port("out_r")->buf = orr;
    h.sync();
    h.mod->process((size_t)n);
}

int log_scale(int i) {
    const char *k = PARAMS[i].key;
    return !std::strncmp(k, "split", 5) || !std::strncmp(k, "ratio", 5) || !std::strncmp(k, "attack", 6) ||
           !std::strncmp(k, "release", 7) || !std::strcmp(k, "react");
}

int text(int i, float v, char *buf, int len) {
    const char *k = PARAMS[i].key;
    if (!std::strncmp(k, "split", 5)) {
        if (v >= 1000.0f) std::snprintf(buf, len, v >= 10000.0f ? "%.1f kHz" : "%.2f kHz", v / 1000.0f);
        else std::snprintf(buf, len, "%.0f Hz", v);
        return 1;
    }
    if (!std::strncmp(k, "ratio", 5)) { std::snprintf(buf, len, v < 10.0f ? "%.2f:1" : "%.1f:1", v); return 1; }
    if (!std::strncmp(k, "attack", 6) || !std::strncmp(k, "release", 7) || !std::strcmp(k, "react")) {
        if (v >= 1000.0f) std::snprintf(buf, len, "%.2f s", v / 1000.0f);
        else std::snprintf(buf, len, v < 10.0f ? "%.1f ms" : "%.0f ms", v);
        return 1;
    }
    if (!std::strncmp(k, "makeup", 6) || !std::strcmp(k, "input") || !std::strcmp(k, "output")) {
        std::snprintf(buf, len, "%+.1f dB", v);
        return 1;
    }
    if (!std::strncmp(k, "thresh", 6)) { std::snprintf(buf, len, "%.1f dB", v); return 1; }
    if (!std::strcmp(k, "slot")) { std::snprintf(buf, len, "%02d", (int)v); return 1; }
    return 0;
}

} // namespace

static const FxHooks HOOKS = {create, destroy, rate, apply, process, nullptr, text, log_scale, nullptr};
#include "fxshell.h"
