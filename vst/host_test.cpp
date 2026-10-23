/* Offline x86 test of MB Dyna (test.sh builds it, the shell with ASan/UBSan): unity at ratio 1 and at MIX 0, a band
 * compressing only its own range, SOLO, MAKEUP, the split points, the 32 slots (factory names, PRESET pick, a re-pick
 * ignored, SAVE/LOAD through a file), the project chunk, option stepping, no UpdateDisplay on plain host writes,
 * NaN/denormal-free output. With "bench" as second argument: CPU load only. Prints PASSED/FAILED. */
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include "vsthost.h"

/* knob travel as in fxshell.h: linear, or logarithmic for the log_scale() keys of mbdyna_vst.cpp */
static void setv(AEffect *e, const char *name, double v, double lo, double hi, bool lg = false) {
    double n = lg ? std::log(v / lo) / std::log(hi / lo) : (v - lo) / (hi - lo);
    pset(e, name, (float)n);
}
static void band(AEffect *e, int b, double thr, double ratio, double makeup = 0) {
    char k[32];
    std::snprintf(k, sizeof k, "Thresh %d", b); setv(e, k, thr, -60, 0);
    std::snprintf(k, sizeof k, "Ratio %d", b); setv(e, k, ratio, 1, 20, true);
    std::snprintf(k, sizeof k, "Makeup %d", b); setv(e, k, makeup, -12, 24);
}
static void flat(AEffect *e) {
    e->dispatcher(e, 2, 0, 1, nullptr, 0);   /* slot 2 first, so that slot 1 is not a re-pick */
    e->dispatcher(e, 2, 0, 0, nullptr, 0);   /* INIT */
    for (int b = 1; b <= 4; b++) band(e, b, -18, 1);
}

int main(int argc, char **argv) {
    if (argc < 2) { std::printf("usage: host_test plugin.so [bench]\n"); return 2; }
    AEffect *e = load_plugin(argv[1]);
    if (!e) return 1;
    std::vector<float> L, R;

    if (argc > 2 && !std::strcmp(argv[2], "bench")) {
        e->dispatcher(e, 2, 0, 2, nullptr, 0);   /* TECHNO MASTER */
        run(e, {{55, 0.4}, {440, 0.2}, {3000, 0.1}}, 0.5, L, R);
        for (const char *mode : {"CLASSIC", "MODERN"}) {
            pset(e, "Mode", std::strcmp(mode, "CLASSIC") ? 1.0f : 0.0f);
            auto t0 = std::chrono::steady_clock::now();
            run(e, {{55, 0.4}, {440, 0.2}, {3000, 0.1}}, 10.0, L, R);
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("bench %s: 10 s of stereo audio in %.3f s = %.2f %% of one x86 core\n", mode, s, s * 10);
        }
        return 0;
    }

    CHECK(e->magic == 0x56737450, "magic");
    CHECK(e->numInputs == 2 && e->numOutputs == 2, "2 in / 2 out");
    CHECK(e->numPrograms == 32, "32 programs, got %d", e->numPrograms);
    CHECK(e->dispatcher(e, 35, 0, 0, nullptr, 0) == 1, "category effect");
    CHECK(prog_name(e, 0) == "01 INIT", "slot 1 name '%s'", prog_name(e, 0).c_str());
    CHECK(prog_name(e, 3) == "04 GOA MASTER", "slot 4 name '%s'", prog_name(e, 3).c_str());
    CHECK(prog_name(e, 31) == "32 EMPTY", "slot 32 name '%s'", prog_name(e, 31).c_str());

    /* 1. INIT, ratio 1 everywhere: the crossover sums flat */
    flat(e);
    for (double f : {50.0, 300.0, 2000.0, 9000.0}) {
        run(e, {{f, 0.25}}, 1.0, L, R);
        double g = db(tone_amp(L, f, 0.5) / 0.25), g2 = db(tone_amp(R, f, 0.5) / 0.25);
        std::printf("unity %5.0f Hz: L %+.2f dB, R %+.2f dB\n", f, g, g2);
        CHECK(std::fabs(g) < 1.0 && std::fabs(g2) < 1.0, "unity at %.0f Hz: %+.2f dB", f, g);
    }

    /* 2. band 1 compresses a 50 Hz tone, not a 2 kHz one */
    band(e, 1, -36, 8);
    run(e, {{50, 0.5}}, 2.0, L, R);
    double low = db(tone_amp(L, 50, 1.5) / 0.5);
    run(e, {{2000, 0.5}}, 2.0, L, R);
    double mid = db(tone_amp(L, 2000, 1.5) / 0.5);
    std::printf("band 1 at -36 dB, 8:1: 50 Hz %+.2f dB, 2 kHz %+.2f dB\n", low, mid);
    /* RMS of the sine -9 dB, 27 dB over: 27/8 left, about -23.6 dB of gain change */
    CHECK(low < -19.0 && low > -28.0, "50 Hz: -36 dB / 8:1 should take about 23 dB off: %+.2f dB", low);
    CHECK(std::fabs(mid) < 1.0, "2 kHz should pass: %+.2f dB", mid);
    CHECK(pdisplay(e, "Ratio 1") == "8.00:1", "ratio text '%s'", pdisplay(e, "Ratio 1").c_str());
    CHECK(pdisplay(e, "Thresh 1") == "-36.0 dB", "threshold text '%s'", pdisplay(e, "Thresh 1").c_str());

    /* 3. MIX 0 = dry */
    setv(e, "Mix", 0, 0, 100);
    run(e, {{50, 0.5}}, 1.5, L, R);
    double dry = db(tone_amp(L, 50, 1.0) / 0.5);
    std::printf("mix 0: 50 Hz %+.2f dB\n", dry);
    CHECK(std::fabs(dry) < 0.5, "mix 0 should be dry: %+.2f dB", dry);
    setv(e, "Mix", 100, 0, 100);

    /* 4. makeup on band 3, solo band 4 */
    flat(e);
    band(e, 3, -18, 1, 6);
    run(e, {{2000, 0.1}}, 1.0, L, R);
    double mk = db(tone_amp(L, 2000, 0.5) / 0.1);
    std::printf("band 3 makeup +6: 2 kHz %+.2f dB\n", mk);
    CHECK(std::fabs(mk - 6.0) < 1.0, "makeup +6 dB: %+.2f dB", mk);
    flat(e);
    pset(e, "Solo 4", 1.0f);
    run(e, {{80, 0.25}, {12000, 0.25}}, 1.0, L, R);
    double s_lo = db(tone_amp(L, 80, 0.5) / 0.25), s_hi = db(tone_amp(L, 12000, 0.5) / 0.25);
    std::printf("solo band 4: 80 Hz %+.1f dB, 12 kHz %+.1f dB\n", s_lo, s_hi);
    CHECK(s_lo < -30 && std::fabs(s_hi) < 1.5, "solo band 4");
    pset(e, "Solo 4", 0.0f);

    /* 5. a split point moves what band 1 catches */
    flat(e);
    band(e, 1, -40, 10);
    setv(e, "Split 1", 400, 40, 1000, true);
    CHECK(pdisplay(e, "Split 1") == "400 Hz", "split text '%s'", pdisplay(e, "Split 1").c_str());
    run(e, {{250, 0.5}}, 2.0, L, R);
    double moved = db(tone_amp(L, 250, 1.5) / 0.5);
    setv(e, "Split 1", 100, 40, 1000, true);
    run(e, {{250, 0.5}}, 2.0, L, R);
    double back = db(tone_amp(L, 250, 1.5) / 0.5);
    std::printf("250 Hz with split 1 at 400 Hz: %+.1f dB, at 100 Hz: %+.1f dB\n", moved, back);
    CHECK(moved < -10 && std::fabs(back) < 1.5, "split point");

    /* 6. presets as programs; a re-pick of the current one is ignored */
    e->dispatcher(e, 2, 0, 3, nullptr, 0);   /* GOA MASTER */
    CHECK(e->dispatcher(e, 3, 0, 0, nullptr, 0) == 3, "current program 3");
    CHECK(pdisplay(e, "Thresh 4") == "-24.0 dB", "GOA thresh 4 '%s'", pdisplay(e, "Thresh 4").c_str());
    CHECK(pdisplay(e, "Split 1") == "150 Hz", "GOA split 1 '%s'", pdisplay(e, "Split 1").c_str());
    band(e, 4, -10, 2);
    e->dispatcher(e, 2, 0, 3, nullptr, 0);   /* same again: ignored, the tweak stays */
    CHECK(pdisplay(e, "Thresh 4") == "-10.0 dB", "re-pick must not reload: '%s'", pdisplay(e, "Thresh 4").c_str());

    /* 7. plain host writes: no UpdateDisplay; a program load: one, after the block */
    run(e, {{100, 0.1}}, 0.2, L, R);
    g_update = 0;
    for (int i = 0; i < 20; i++) { band(e, 2, -20 - i, 2); run(e, {{100, 0.1}}, 0.01, L, R); }
    CHECK(g_update == 0, "host writes sent %d UpdateDisplay", g_update);
    e->dispatcher(e, 2, 0, 1, nullptr, 0);
    CHECK(g_update == 0, "no UpdateDisplay inside effSetProgram");
    run(e, {{100, 0.1}}, 0.2, L, R);
    CHECK(g_update == 1, "one UpdateDisplay after a program load, got %d", g_update);

    /* 8. option and whole-number stepping: one Q-Link event = one step */
    int mode = pidx(e, "Mode");
    e->setParameter(e, mode, 0.0f);
    e->setParameter(e, mode, 1.0f / 128);
    CHECK(pdisplay(e, "Mode") == "MODERN", "mode nudge up: '%s'", pdisplay(e, "Mode").c_str());
    int slot = pidx(e, "Slot");
    e->setParameter(e, slot, 0.0f);
    e->setParameter(e, slot, 1.0f / 128);
    CHECK(pdisplay(e, "Slot") == "02", "slot nudge: '%s'", pdisplay(e, "Slot").c_str());

    /* 9. SAVE and LOAD through the slot file next to the .so */
    setv(e, "Slot", 20, 1, 32);
    CHECK(pdisplay(e, "Slot") == "20", "slot 20: '%s'", pdisplay(e, "Slot").c_str());
    band(e, 2, -33, 5);
    g_update = 0;
    pset(e, "Save", 1.0f);
    run(e, {{100, 0.1}}, 0.1, L, R);
    CHECK(prog_name(e, 19) == "20 USER", "saved slot name '%s'", prog_name(e, 19).c_str());
    CHECK(g_update >= 1, "save asks for a redraw of the names");
    band(e, 2, -5, 1);
    pset(e, "Load", 1.0f);
    run(e, {{100, 0.1}}, 0.1, L, R);
    CHECK(pdisplay(e, "Thresh 2") == "-33.0 dB", "loaded slot thresh 2 '%s'", pdisplay(e, "Thresh 2").c_str());
    CHECK(pget(e, "Load") == 0.0f, "LOAD button released");
    {
        AEffect *e2 = load_plugin(argv[1]);   /* a second instance reads the file */
        CHECK(e2 && prog_name(e2, 19) == "20 USER", "second instance sees slot 20");
        e2->dispatcher(e2, 2, 0, 19, nullptr, 0);
        CHECK(pdisplay(e2, "Ratio 2") == "5.00:1", "second instance loads slot 20: '%s'", pdisplay(e2, "Ratio 2").c_str());
        e2->dispatcher(e2, 1, 0, 0, nullptr, 0);
    }

    /* 10. project chunk round trip */
    band(e, 3, -27, 3, 2);
    void *chunk = nullptr;
    intptr_t len = e->dispatcher(e, 23, 0, 0, &chunk, 0);
    std::string saved((const char *)chunk, (size_t)len);
    band(e, 3, -10, 1, 0);
    e->dispatcher(e, 24, 0, (intptr_t)saved.size(), (void *)saved.data(), 0);
    CHECK(pdisplay(e, "Thresh 3") == "-27.0 dB" && pdisplay(e, "Makeup 3") == "+2.0 dB", "chunk restore: '%s' '%s'",
          pdisplay(e, "Thresh 3").c_str(), pdisplay(e, "Makeup 3").c_str());

    /* 11. loud and silent input stay finite */
    e->dispatcher(e, 2, 0, 8, nullptr, 0);   /* LOUD */
    run(e, {{60, 4.0}, {7000, 2.0}}, 1.0, L, R);
    run(e, {}, 3.0, L, R);
    CHECK(!g_bad, "NaN, Inf or denormal in the output");

    e->dispatcher(e, 1, 0, 0, nullptr, 0);
    std::printf(g_fails ? "FAILED (%d)\n" : "PASSED\n", g_fails);
    return g_fails ? 1 : 0;
}
