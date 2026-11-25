#include "choppage/AudioFile.h"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace bc::AudioFile
{
    namespace
    {
        uint32_t le32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
        uint16_t le16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
        uint32_t be32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; }
        uint16_t be16 (const uint8_t* p) { return (uint16_t) ((p[0] << 8) | p[1]); }

        // 80-bit IEEE extended (AIFF sample rate) -> double
        double ext80 (const uint8_t* p)
        {
            const int expo = ((p[0] & 0x7F) << 8) | p[1];
            uint64_t mant = 0;
            for (int i = 0; i < 8; ++i) mant = (mant << 8) | p[2 + i];
            if (expo == 0 && mant == 0) return 0.0;
            const double v = (double) mant * std::pow (2.0, expo - 16383 - 63);
            return (p[0] & 0x80) ? -v : v;
        }

        struct Layout
        {
            bool ok = false, aiff = false, isFloat = false, littleEndian = true;
            int channels = 0, bits = 0;
            double rate = 0;
            int64_t dataOffset = 0, dataBytes = 0;
        };

        Layout parse (std::ifstream& f)
        {
            Layout L;
            uint8_t h[12];
            if (! f.read ((char*) h, 12)) return L;
            if (std::memcmp (h, "RIFF", 4) == 0 && std::memcmp (h + 8, "WAVE", 4) == 0)
            {
                bool haveFmt = false;
                for (;;)
                {
                    uint8_t ch[8];
                    if (! f.read ((char*) ch, 8)) break;
                    const uint32_t size = le32 (ch + 4);
                    const auto pos = (int64_t) f.tellg();
                    if (std::memcmp (ch, "fmt ", 4) == 0)
                    {
                        uint8_t fmt[40] {};
                        f.read ((char*) fmt, std::min<uint32_t> (size, 40));
                        int tag = le16 (fmt);
                        L.channels = le16 (fmt + 2);
                        L.rate = le32 (fmt + 4);
                        L.bits = le16 (fmt + 14);
                        if (tag == 0xFFFE && size >= 26) tag = le16 (fmt + 24);
                        L.isFloat = tag == 3;
                        haveFmt = tag == 1 || tag == 3;
                    }
                    else if (std::memcmp (ch, "data", 4) == 0)
                    {
                        L.dataOffset = pos;
                        L.dataBytes = size;
                        L.ok = haveFmt;
                        return L;
                    }
                    f.seekg (pos + size + (size & 1));
                    if (! f) break;
                }
                return L;
            }
            if (std::memcmp (h, "FORM", 4) == 0 && (std::memcmp (h + 8, "AIFF", 4) == 0 || std::memcmp (h + 8, "AIFC", 4) == 0))
            {
                L.aiff = true;
                L.littleEndian = false;
                const bool aifc = std::memcmp (h + 8, "AIFC", 4) == 0;
                bool haveComm = false;
                for (;;)
                {
                    uint8_t ch[8];
                    if (! f.read ((char*) ch, 8)) break;
                    const uint32_t size = be32 (ch + 4);
                    const auto pos = (int64_t) f.tellg();
                    if (std::memcmp (ch, "COMM", 4) == 0)
                    {
                        uint8_t c[26] {};
                        f.read ((char*) c, std::min<uint32_t> (size, 26));
                        L.channels = be16 (c);
                        L.bits = be16 (c + 6);
                        L.rate = ext80 (c + 8);
                        haveComm = true;
                        if (aifc && size >= 22)
                        {
                            if (std::memcmp (c + 18, "sowt", 4) == 0) L.littleEndian = true;
                            else if (std::memcmp (c + 18, "NONE", 4) != 0) haveComm = false;   // compressed: no
                        }
                    }
                    else if (std::memcmp (ch, "SSND", 4) == 0)
                    {
                        uint8_t off[8];
                        f.read ((char*) off, 8);
                        L.dataOffset = pos + 8 + be32 (off);
                        L.dataBytes = size - 8 - be32 (off);
                        L.ok = haveComm;
                        return L;
                    }
                    f.seekg (pos + size + (size & 1));
                    if (! f) break;
                }
            }
            return L;
        }

        float sampleAt (const uint8_t* p, const Layout& L)
        {
            const bool le = L.littleEndian;
            switch (L.bits)
            {
                case 8:  return L.aiff ? (float) (int8_t) p[0] / 128.0f : ((float) p[0] - 128.0f) / 128.0f;
                case 16: return (float) (int16_t) (le ? le16 (p) : be16 (p)) / 32768.0f;
                case 24:
                {
                    int32_t v = le ? (p[0] | (p[1] << 8) | (p[2] << 16)) : ((p[0] << 16) | (p[1] << 8) | p[2]);
                    if (v & 0x800000) v |= ~0xFFFFFF;
                    return (float) v / 8388608.0f;
                }
                case 32:
                {
                    const uint32_t u = le ? le32 (p) : be32 (p);
                    if (L.isFloat) { float f; std::memcpy (&f, &u, 4); return f; }
                    return (float) ((double) (int32_t) u / 2147483648.0);
                }
                case 64:
                {
                    uint64_t u = 0;
                    for (int i = 7; i >= 0; --i) u = (u << 8) | p[i];
                    double d; std::memcpy (&d, &u, 8); return (float) d;
                }
                default: return 0.0f;
            }
        }
    }

    bool isAudioFile (const std::string& path)
    {
        auto dot = path.find_last_of ('.');
        if (dot == std::string::npos) return false;
        std::string e = path.substr (dot + 1);
        for (auto& c : e) c = (char) std::tolower ((unsigned char) c);
        return e == "wav" || e == "aif" || e == "aiff";
    }

    std::string baseName (const std::string& path)
    {
        auto slash = path.find_last_of ("/\\");
        std::string n = slash == std::string::npos ? path : path.substr (slash + 1);
        auto dot = n.find_last_of ('.');
        return dot == std::string::npos ? n : n.substr (0, dot);
    }

    Info readInfo (const std::string& path)
    {
        Info i;
        std::ifstream f (path, std::ios::binary);
        if (! f) return i;
        const auto L = parse (f);
        if (! L.ok || L.channels <= 0 || L.bits <= 0) return i;
        i.ok = true;
        i.channels = L.channels;
        i.sampleRate = L.rate;
        i.frames = L.dataBytes / ((L.bits / 8) * L.channels);
        return i;
    }

    std::shared_ptr<SampleData> load (const std::string& path, Status& status, double maxSeconds)
    {
        std::ifstream f (path, std::ios::binary);
        if (! f) { status = Status::missing; return nullptr; }
        const auto L = parse (f);
        const int bps = L.bits / 8;
        if (! L.ok || L.channels <= 0 || bps <= 0 || L.rate <= 0 || (L.isFloat && L.bits != 32 && L.bits != 64)
            || (! L.isFloat && L.bits > 32)) { status = Status::unreadable; return nullptr; }
        const int64_t frames = L.dataBytes / (bps * L.channels);
        if (frames <= 0) { status = Status::empty; return nullptr; }
        if ((double) frames > maxSeconds * L.rate) { status = Status::tooLong; return nullptr; }

        std::vector<uint8_t> raw ((size_t) (frames * bps * L.channels));
        f.seekg (L.dataOffset);
        f.read ((char*) raw.data(), (std::streamsize) raw.size());
        const int64_t got = (int64_t) f.gcount() / (bps * L.channels);
        if (got <= 0) { status = Status::unreadable; return nullptr; }

        auto s = std::make_shared<SampleData>();
        const int outCh = L.channels == 1 ? 1 : 2;
        s->allocate (outCh, (int) got);
        s->sampleRate = L.rate;
        s->path = path;
        s->name = baseName (path);
        for (int64_t i = 0; i < got; ++i)
            for (int c = 0; c < outCh; ++c)
                s->ch[c][(size_t) i] = sampleAt (raw.data() + (size_t) ((i * L.channels + c) * bps), L);
        s->buildOverview();
        status = Status::ok;
        return s;
    }

    bool writeWav24 (const std::string& path, const float* const* chans, int numCh, int n, double sr)
    {
        std::ofstream f (path, std::ios::binary);
        if (! f) return false;
        const uint32_t dataBytes = (uint32_t) (n * numCh * 3);
        auto u32 = [&f] (uint32_t v) { uint8_t b[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; f.write ((char*) b, 4); };
        auto u16 = [&f] (uint16_t v) { uint8_t b[2] = { (uint8_t) v, (uint8_t) (v >> 8) }; f.write ((char*) b, 2); };
        f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
        f.write ("fmt ", 4); u32 (16); u16 (1); u16 ((uint16_t) numCh); u32 ((uint32_t) sr);
        u32 ((uint32_t) sr * (uint32_t) numCh * 3); u16 ((uint16_t) (numCh * 3)); u16 (24);
        f.write ("data", 4); u32 (dataBytes);
        std::vector<uint8_t> buf ((size_t) dataBytes);
        size_t k = 0;
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < numCh; ++c)
            {
                const int32_t v = (int32_t) std::lround (clampv (chans[c][i], -1.0f, 1.0f) * 8388607.0f);
                buf[k++] = (uint8_t) v; buf[k++] = (uint8_t) (v >> 8); buf[k++] = (uint8_t) (v >> 16);
            }
        f.write ((char*) buf.data(), (std::streamsize) buf.size());
        return (bool) f;
    }
}
