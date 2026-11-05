// Build helper: make_icon <in.png> <out.ico> turns the program's PNG icon into a Windows icon.
// Each standard size up to the PNG's is an uncompressed 32-bit entry, which every resource compiler takes.
#include "image.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>

using namespace nut;

namespace {

// Area-weighted resize to s x s, averaging colours by alpha so transparent pixels leave no dark fringe.
image::Rgba resize(const image::Rgba& src, uint32_t s) {
    image::Rgba out;
    out.width = out.height = s;
    out.pixels.resize(size_t(s) * s * 4);
    double sx = double(src.width) / s, sy = double(src.height) / s;
    for (uint32_t y = 0; y < s; ++y)
        for (uint32_t x = 0; x < s; ++x) {
            double x0 = x * sx, x1 = x0 + sx, y0 = y * sy, y1 = y0 + sy;
            double acc[4] = {}, area = 0;
            for (uint32_t yy = uint32_t(y0); yy < src.height && yy < y1; ++yy) {
                double wy = std::min(y1, yy + 1.0) - std::max(y0, double(yy));
                for (uint32_t xx = uint32_t(x0); xx < src.width && xx < x1; ++xx) {
                    double w = wy * (std::min(x1, xx + 1.0) - std::max(x0, double(xx)));
                    if (w <= 0) continue;
                    const uint8_t* p = &src.pixels[(size_t(yy) * src.width + xx) * 4];
                    double a = p[3] / 255.0;
                    for (int k = 0; k < 3; ++k) acc[k] += w * a * p[k];
                    acc[3] += w * a;
                    area += w;
                }
            }
            uint8_t* o = &out.pixels[(size_t(y) * s + x) * 4];
            for (int k = 0; k < 3; ++k) o[k] = uint8_t(acc[3] > 0 ? std::clamp(acc[k] / acc[3] + 0.5, 0.0, 255.0) : 0);
            o[3] = uint8_t(area > 0 ? std::clamp(acc[3] / area * 255 + 0.5, 0.0, 255.0) : 0);
        }
    return out;
}

// An icon entry: BITMAPINFOHEADER (height doubled for the mask), bottom-up BGRA rows, then the 1-bit AND mask.
Bytes dibEntry(const image::Rgba& img) {
    uint32_t s = img.width, maskRow = (s + 31) / 32 * 4;
    Writer w(false);
    w.u32(40);
    w.u32(s);
    w.u32(s * 2);
    w.u16(1);
    w.u16(32);
    w.u32(0);
    w.u32(s * s * 4 + maskRow * s);
    w.zeros(16);
    for (uint32_t y = s; y-- > 0;)
        for (uint32_t x = 0; x < s; ++x) {
            const uint8_t* p = &img.pixels[(size_t(y) * s + x) * 4];
            w.u8(p[2]);
            w.u8(p[1]);
            w.u8(p[0]);
            w.u8(p[3]);
        }
    for (uint32_t y = s; y-- > 0;) {
        Bytes row(maskRow, 0);
        for (uint32_t x = 0; x < s; ++x)
            if (img.pixels[(size_t(y) * s + x) * 4 + 3] == 0) row[x / 8] |= uint8_t(0x80 >> (x % 8));
        w.bytes(row);
    }
    return w.buf();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: make_icon <in.png> <out.ico>\n");
        return 2;
    }
    try {
        image::Rgba src = image::decodePng(readFile(argv[1]));
        std::vector<Bytes> entries;
        std::vector<uint32_t> sizes;
        for (uint32_t s : {16u, 20u, 24u, 32u, 40u, 48u, 64u, 96u, 128u, 256u})
            if (s <= std::max(src.width, src.height) || s == 16) {
                sizes.push_back(s);
                entries.push_back(dibEntry(resize(src, s)));
            }
        Writer w(false);
        w.u16(0);
        w.u16(1); // icon
        w.u16(uint16_t(entries.size()));
        uint32_t offset = 6 + 16 * uint32_t(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            w.u8(uint8_t(sizes[i] >= 256 ? 0 : sizes[i]));
            w.u8(uint8_t(sizes[i] >= 256 ? 0 : sizes[i]));
            w.u8(0);
            w.u8(0);
            w.u16(1);
            w.u16(32);
            w.u32(uint32_t(entries[i].size()));
            w.u32(offset);
            offset += uint32_t(entries[i].size());
        }
        for (auto& e : entries) w.bytes(e);
        writeFile(argv[2], w.buf());
        return 0;
    } catch (std::exception& e) {
        std::fprintf(stderr, "make_icon: %s\n", e.what());
        return 1;
    }
}
