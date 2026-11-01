// Higher-level asset operations shared by the CLI and the GUI: texture pairing and
// import, unpack/pack of containers, wave bank export, and the whole-folder jobs.
#pragma once

#include "caff.hpp"
#include "common.hpp"
#include "streambundle.hpp"
#include "texture.hpp"

#include <atomic>
#include <functional>
#include <optional>
#include <set>

namespace nut::ops {

// ---- naming ------------------------------------------------------------------

std::string safeName(std::string s, size_t maxLen = 96);
bool validSection(const caff::Caff& c, const caff::Symbol& s);
std::string sectionTag(const caff::Caff& c, const caff::Symbol& s);
// "#12" (1-based index) or a case-insensitive substring of the symbol name.
std::vector<size_t> selectSymbols(const caff::Caff& c, const std::string& sel, const std::string& section);

// ---- progress reporting ---------------------------------------------------------

struct Progress {
    std::function<void(const std::string&)> log;              // informational / error lines
    std::function<void(double, const std::string&)> step;     // fraction 0..1 and a label
    const std::atomic<bool>* cancel = nullptr;

    void info(const std::string& s) const { if (log) log(s); }
    void at(double f, const std::string& label) const { if (step) step(f, label); }
    bool cancelled() const { return cancel && cancel->load(); }
};

// ---- textures ------------------------------------------------------------------

struct TexRef {
    caff::Caff* caff = nullptr;
    size_t header = 0, gpu = 0; // symbol indices
    std::string name;           // base asset name
};

std::vector<TexRef> findTextures(caff::Caff& c);
// The texture whose header or GPU symbol is `symbol`, if any.
std::optional<TexRef> textureForSymbol(caff::Caff& c, size_t symbol);
// "..._0x<hash>mip" <-> "..._0x<hash>top"; empty when `n` does not end with `from`.
std::string partnerName(const std::string& n, const char* from, const char* to);
// Strips the trailing "mip"/"top" of split texture names.
std::string fullName(const std::string& n);
texture::Header asFullChain(texture::Header h);

// A texture decoded for viewing/exporting, with the streamed top level merged in when found.
struct DecodedTexture {
    texture::Header header;            // describes `levels` as a full chain when merged
    std::vector<texture::Level> levels;
    bool merged = false;
    std::string name;                  // output name (split suffix removed when merged)
};
DecodedTexture decodeTexture(const TexRef& t, const std::vector<caff::Caff*>& streams);

void writeTextureFiles(const fs::path& dir, const std::string& name, const texture::Header& h, const std::vector<texture::Level>& levels,
                       bool dds, bool png);

struct TexStats {
    int written = 0, merged = 0, skipped = 0;
};
TexStats exportTextures(const std::vector<caff::Caff*>& mains, const std::vector<caff::Caff*>& streams, const fs::path& dir, bool dds, bool png,
                        std::set<std::string>* mergedTops, const Progress* progress = nullptr);

// Build GPU levels for `h`/`c` from a .dds or .png file.
std::vector<texture::Level> levelsFromImage(const texture::Header& h, const texture::Contents& c, const fs::path& img);

// Replace a texture's pixels from an image. For the mip half of a split texture, pass the
// paired stream bundle so level 0 is updated too (`streamChanged` reports whether it was).
// Returns a one-line summary.
std::string importTexture(caff::Caff& c, const TexRef& t, const fs::path& img, streambundle::StreamBundle* stream, bool* streamChanged);

// ---- containers --------------------------------------------------------------------

void unpackCaff(const caff::Caff& c, bool wasXctd, const fs::path& dir);
caff::Caff packCaff(const fs::path& dir, bool* wasXctd, int* changed);
std::string entryName(const streambundle::Entry& e);
std::string entryExt(const streambundle::Entry& e);
void unpackStreamBundle(const streambundle::StreamBundle& sb, const fs::path& dir);
streambundle::StreamBundle packStreamBundle(const fs::path& dir);
// The bank (<base>.xwb) and its waves (<base>/NNN_<name>.xma/.wav). With `wav`, waves are decoded
// to 16-bit WAV (kept as they are when they cannot be). Returns the number decoded.
int exportWaveBank(const Bytes& data, const fs::path& dir, const std::string& base, bool wav = false);
Bytes precacheToUtf8(const Bytes& d);
Bytes utf8ToPrecache(const std::string& text);

// ---- whole-folder jobs -----------------------------------------------------------

struct ExtractOptions {
    bool png = false;
    std::set<std::string> only; // empty = everything; else bundles,streams,text,video,fonts,ini,misc
};
// Returns the number of failures (each is reported through progress.log).
int extractAll(const fs::path& root, const fs::path& out, const ExtractOptions& opt, const Progress& progress);

struct VerifyResult {
    int passed = 0, failed = 0;
};
VerifyResult verifyAll(const fs::path& root, const Progress& progress);

} // namespace nut::ops
