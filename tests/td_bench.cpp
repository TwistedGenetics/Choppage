// Build: g++ -O2 -std=c++17 -Ichoppage-core/include tests/td_bench.cpp <build>/choppage-core/libchoppage-core.a -o td_bench
// Run:   ./td_bench [sensitivity]   (DIAG=1: misses by kind, FP timing; DIAG2=K: each missed kick; SEED=n: one break)
// Transient-detector bench: synthetic breaks with exact hit starts (varied kits, levels, humanised timing,
// flams, ghost notes, open hats, noise floor, room tail, "sampled" grit), plus REX files' own slice points.
#include "choppage/TransientDetector.h"
#include "choppage/AudioFile.h"
#include "choppage/Dsp.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>
using namespace bc;

struct Hit { int at; char kind; float level; };
struct Case { SamplePtr buf; std::vector<int> truth; std::string name; std::string kinds; };

static Case synth(int seed)
{
    Rng r(seed);
    const double sr = 44100.0;
    const double bpm = 130 + r.nextInt(60);
    const int steps = 32;
    const double stepLen = sr * 60.0 / bpm / 4.0;
    const int n = (int)(stepLen * steps) + (int)(sr * 0.3);
    auto b = std::make_shared<SampleData>();
    b->allocate(2, n);
    b->sampleRate = sr;
    std::vector<Hit> hits;
    for (int st = 0; st < steps; ++st)
    {
        const float p = r.nextFloat();
        char c = '.';
        if (st % 8 == 0 && p < 0.9f) c = 'K';
        else if (st % 8 == 4 && p < 0.9f) c = 'S';
        else if (p < 0.25f) c = 'K';
        else if (p < 0.40f) c = 'G';
        else if (p < 0.62f) c = 'H';
        else if (p < 0.67f) c = 'O';
        else if (p < 0.72f) c = 'S';
        if (c == '.') continue;
        int at = (int)std::round(st * stepLen) + (int)((r.nextFloat() - 0.5f) * sr * 0.012);   // +-6 ms humanise
        at = std::max(0, at);
        const float lvl = c == 'G' ? 0.12f + 0.15f * r.nextFloat() : c == 'H' ? 0.2f + 0.3f * r.nextFloat() : 0.6f + 0.4f * r.nextFloat();
        hits.push_back({ at, c, lvl });
        if ((c == 'S' || c == 'G') && r.nextFloat() < 0.15f)                                   // a flam: one hit, two strokes
            hits.push_back({ at + (int)(sr * (0.012 + 0.01 * r.nextFloat())), 'f', lvl * 0.6f });
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.at < b.at; });
    float* L = b->ch[0].data(); float* R = b->ch[1].data();
    const double tw = 2.0 * kPi;
    const float kickF = 40 + 25 * r.nextFloat(), snF = 160 + 80 * r.nextFloat();
    for (const auto& h : hits)
    {
        const int len = std::min(n - h.at, (int)(sr * (h.kind == 'O' ? 0.4 : 0.3)));
        double ph = 0; float lp = 0, hp1 = 0, hpx = 0;
        const char c = h.kind == 'f' ? 'S' : h.kind;
        for (int i = 0; i < len; ++i)
        {
            const double t = i / sr;
            const float nz = r.nextFloat() * 2 - 1;
            float v = 0;
            if (c == 'K') { const double f = kickF + 110 * std::exp(-t * 35); ph += tw * f / sr;
                            v = (float)(std::sin(ph) * std::exp(-t * 8)) + (i < 40 ? nz * 0.5f * (1 - i / 40.f) : 0); }
            else if (c == 'S' || c == 'G') { ph += tw * snF / sr; lp += (nz - lp) * 0.5f;
                            v = 0.5f * (float)(std::sin(ph) * std::exp(-t * 28)) + 0.8f * lp * (float)std::exp(-t * 14); }
            else { hpx = nz - hp1; hp1 = nz; v = hpx * 0.5f * (float)std::exp(-t * (c == 'O' ? 7 : 45)); }
            // the first sample is the hit; a 0.3 ms rise as on a real recording
            const float rise = std::min(1.0f, (float)i / (float)(sr * 0.0003));
            v *= h.level * rise;
            L[h.at + i] += v; R[h.at + i] += v * 0.9f;
        }
    }
    // room tail, noise floor, glue (soft clip), sometimes "sampled": 12-bit, low-passed
    std::vector<float> tail(n, 0.0f);
    const int d1 = (int)(sr * 0.023), d2 = (int)(sr * 0.041);
    for (int i = 0; i < n; ++i)
    {
        float t = 0;
        if (i >= d1) t += tail[i - d1] * 0.35f + L[i - d1] * 0.15f;
        if (i >= d2) t += tail[i - d2] * 0.25f;
        tail[i] = t;
    }
    const bool gritty = r.nextBool();
    float lpL = 0;
    const float floor = 0.001f + 0.004f * r.nextFloat();
    for (int i = 0; i < n; ++i)
    {
        float v = L[i] + tail[i] * 0.5f + (r.nextFloat() * 2 - 1) * floor;
        v = std::tanh(v * 1.4f);
        if (gritty) { lpL += (v - lpL) * 0.35f; v = std::round(lpL * 2048.f) / 2048.f; }
        L[i] = v; R[i] = v;
    }
    Case cs;
    cs.buf = b; cs.name = "synth " + std::to_string(seed) + (gritty ? " (gritty)" : "");
    for (const auto& h : hits) if (h.kind != 'f') { cs.truth.push_back(h.at); cs.kinds += h.kind; }
    return cs;
}

struct Score { int tp = 0, fp = 0, fn = 0; std::vector<double> err; std::vector<double> fpAfter, fnGap; };

static void score(const std::vector<int>& found, const std::vector<int>& truth, double sr, Score& s, double tolMs = 15)
{
    std::vector<bool> used(found.size(), false);
    const int tol = (int)(sr * tolMs / 1000);
    for (int t : truth)
    {
        int best = -1, bd = tol + 1;
        for (size_t i = 0; i < found.size(); ++i) if (!used[i] && std::abs(found[i] - t) < bd) { bd = std::abs(found[i] - t); best = (int)i; }
        if (best >= 0) { used[(size_t)best] = true; ++s.tp; s.err.push_back((found[(size_t)best] - t) * 1000.0 / sr); }
        else ++s.fn;
    }
    for (size_t i = 0; i < found.size(); ++i) if (!used[i])
    {
        ++s.fp;
        int prev = -1000000; for (int t : truth) if (t <= found[i] && t > prev) prev = t;
        int nxt = 1000000000; for (int t : truth) if (t > found[i] && t < nxt) nxt = t;
        s.fpAfter.push_back((found[i] - prev) * 1000.0 / sr);
        s.fnGap.push_back((nxt - found[i]) * 1000.0 / sr);
    }
}

static void report(const char* label, Score& s)
{
    const double p = s.tp / std::max(1.0, (double)(s.tp + s.fp)), rc = s.tp / std::max(1.0, (double)(s.tp + s.fn));
    std::vector<double> a; for (double e : s.err) a.push_back(std::fabs(e));
    std::sort(a.begin(), a.end());
    double mean = 0, bias = 0; for (double e : s.err) { mean += std::fabs(e); bias += e; }
    int in1 = 0, late = 0; for (double e : s.err) { if (std::fabs(e) <= 1.0) ++in1; if (e > 0.5) ++late; }
    const size_t m = a.size();
    std::printf("%-10s F %.3f (P %.3f R %.3f)  |err| mean %.2f ms  median %.2f  p90 %.2f  bias %+.2f  within 1ms %.0f%%  late>0.5ms %.0f%%\n",
                label, 2 * p * rc / std::max(1e-9, p + rc), p, rc, m ? mean / m : 0, m ? a[m / 2] : 0, m ? a[m * 9 / 10] : 0,
                m ? bias / m : 0, m ? 100.0 * in1 / m : 0, m ? 100.0 * late / m : 0);
}

int main(int argc, char** argv)
{
    const float sens = argc > 1 ? (float)std::atof(argv[1]) : 0.62f;
    Score syn; int kindN[256] = {}, kindMiss[256] = {};
    const int s0 = std::getenv("SEED") ? std::atoi(std::getenv("SEED")) : 1, s1 = std::getenv("SEED") ? s0 : 60;
    for (int seed = s0; seed <= s1; ++seed)
    {
        auto c = synth(seed);
        std::vector<int> f;
        for (const auto& o : TransientDetector::findOnsets(*c.buf, sens)) f.push_back(o.position);
        f.push_back(0);   // the sample start is always slice 1
        std::sort(f.begin(), f.end()); f.erase(std::unique(f.begin(), f.end()), f.end());
        score(f, c.truth, c.buf->sampleRate, syn);
        if (std::getenv("DIAG3"))
            for (int x : f)
            {
                int bk = -1, bd = 1 << 30; for (size_t k = 0; k < c.truth.size(); ++k) if (std::abs(c.truth[k] - x) < std::abs(bd)) { bd = x - c.truth[k]; bk = (int)k; }
                if (std::abs(bd) > 661 && std::abs(bd) < 3000)
                    std::printf("   FP %s seed %d at %d: %+.1f ms from %c (level?) %s\n", c.name.find("gritty") != std::string::npos ? "GRITTY" : "clean", seed, x, bd / 44.1, bk >= 0 ? c.kinds[(size_t)bk] : '-', "");
            }
        if (std::getenv("DIAG"))
            for (size_t k = 0; k < c.truth.size(); ++k)
            {
                bool hit = false; for (int x : f) if (std::abs(x - c.truth[k]) <= 661) hit = true;
                kindN[(unsigned char)c.kinds[k]]++; if (!hit) kindMiss[(unsigned char)c.kinds[k]]++;
                if (!hit && std::getenv("DIAG2") && c.kinds[k] == std::getenv("DIAG2")[0])
                {
                    int nd = 1 << 30; for (int x : f) if (std::abs(x - c.truth[k]) < std::abs(nd)) nd = x - c.truth[k];
                    int pd = -1, nx = -1; if (k > 0) pd = c.truth[k] - c.truth[k - 1]; if (k + 1 < c.truth.size()) nx = c.truth[k + 1] - c.truth[k];
                    std::printf("   %s miss %c seed %d at %d: nearest det %+.1f ms, prev hit %c %.1f ms before, next %.1f ms\n", c.name.find("gritty") != std::string::npos ? "GRITTY" : "clean", c.kinds[k], seed, c.truth[k], nd / 44.1,
                                k > 0 ? c.kinds[k - 1] : '-', pd / 44.1, nx / 44.1);
                }
            }
    }
    report("SYNTH", syn);
    if (std::getenv("DIAG")) { std::vector<int> h(12, 0); for (double d : syn.fpAfter) h[std::min(11, (int)(d / 10))]++;
        std::printf("FP ms after previous true hit (10 ms buckets):"); for (int v : h) std::printf(" %d", v); std::printf("\n");
        { std::vector<int> h2(12, 0); for (double d : syn.fnGap) h2[std::min(11, (int)(d / 10))]++;
          std::printf("FP ms before the next true hit:"); for (int v : h2) std::printf(" %d", v); std::printf("\n"); }
        for (char k : std::string("KSGHO")) std::printf("  %c missed %d/%d", k, kindMiss[(unsigned char)k], kindN[(unsigned char)k]); std::printf("\n"); }
    Score rex;
    for (const char* p : { "/home/claude/choppage/tests/data/120Stereo.rx2", "/home/claude/choppage/tests/data/120RexTest.rex" })
    {
        AudioFile::Status st;
        auto b = AudioFile::load(p, st);
        if (!b || b->fileMarkers.empty()) { std::printf("(no %s)\n", p); continue; }
        std::vector<int> f;
        for (const auto& o : TransientDetector::findOnsets(*b, sens)) f.push_back(o.position);
        f.push_back(0);
        std::sort(f.begin(), f.end());
        Score one; score(f, b->fileMarkers, b->sampleRate, one); score(f, b->fileMarkers, b->sampleRate, rex);
        std::printf("  %s: %zu markers, %zu found\n", AudioFile::baseName(p).c_str(), b->fileMarkers.size(), f.size());
    }
    report("REX", rex);
}
