// nuttool - read, unpack and edit Banjo-Kazooie: Nuts & Bolts (Xbox 360) assets.
#include "commands.hpp"

#include <cctype>
#include <cstdio>
#include <functional>

using namespace nut;

namespace {

struct Command {
    const char* name;
    const char* args;
    const char* help;
    std::function<int(const cli::Args&)> fn;
};

const std::vector<Command>& commands() {
    static const std::vector<Command> cmds = {
        {"info", "<file>", "Identify any asset file and summarise its contents", cli::info},
        {"list", "<file>", "List symbols of a CAFF/bundle, entries of a stream bundle or waves of a wave bank", cli::list},
        {"hash", "<asset name>...", "Compute asset ids (and on-disk paths) for aid_ names", cli::hash},
        {"index", "<assets dir> [--json]", "List every asset in Debug\\db_index.txt with its id and file", cli::index},
        {"verify", "<assets dir>", "Round-trip every container to prove lossless parsing", cli::verify},
        {"extract-all", "<assets dir> <out dir> [--png] [--only bundles,streams,text,video,fonts,ini,misc]",
         "Unpack everything into editable files", cli::extractAll},

        {"decompress", "<in> <out>", "Remove XCTD compression from a bundle file", cli::decompress},
        {"compress", "<in> <out>", "Wrap a file in an (uncompressed) XCTD container", cli::compress},
        {"caff-unpack", "<file> <dir>", "Unpack a CAFF/bundle losslessly (caff.json + one file per symbol)", cli::caffUnpack},
        {"caff-pack", "<dir> <file> [--xctd]", "Rebuild a CAFF from caff-unpack output (edited/resized symbols are applied)", cli::caffPack},
        {"sb-unpack", "<file> <dir>", "Unpack a stream bundle into its entries", cli::sbUnpack},
        {"sb-pack", "<dir> <file>", "Rebuild a stream bundle from sb-unpack output", cli::sbPack},
        {"extract", "<file> <#index|name> <out> [--section .gpu]", "Extract one symbol (or stream bundle entry)", cli::extract},
        {"replace", "<file> <#index|name> <data> [-o out] [--section .gpu] [--xctd]", "Replace one symbol or stream bundle entry", cli::replace},

        {"tex-export", "<file> <dir> [--stream <stream bundle>] [--png|--png-only]", "Export textures as DDS (and PNG)", cli::texExport},
        {"tex-import", "<file> <texture name> <image.dds|png> [-o out] [--stream <sb> [--stream-out out]]",
         "Replace a texture (same size/format); --stream also replaces the streamed top mip", cli::texImport},
        {"model-export", "<file> <dir> [--all-groups] [--png]",
         "Export models as OBJ + MTL with a bone list; --all-groups includes every LOD and alternative part, --png writes the file's textures",
         cli::modelExport},
        {"make-nbpatch", "<game dir> <out.nbpatch> --name <name> [--original <clean game dir>] [--version v] [--author a] [--category c] [--description d] [--id id]",
         "Package edited files as a mod for reNut / NB Studio: files with a .bak backup, or every file that differs from --original",
         cli::makeNbpatch},
        {"text-export", "<loctext file> <out.json>", "Export a string table to UTF-8 JSON", cli::textExport},
        {"text-import", "<loctext file> <in.json> [-o out] [--prune]", "Apply edited/added strings from JSON", cli::textImport},
        {"font-export", "<font.sbp> <dir>", "Export a glyph cache to PNG pages + font.json", cli::fontExport},
        {"font-import", "<dir> <out.sbp> [--base original.sbp]", "Rebuild a glyph cache from exported pages", cli::fontImport},
        {"xmv-info", "<file> [copy.wmv]", "Show movie streams (XMV files are plain WMV/ASF)", cli::xmvInfo},
        {"wave-export", "<stream bundle|xwb> <dir>", "Export XACT wave banks and their waves (.wav / .xma)", cli::waveExport},
        {"ini-export", "<file.ini> <out.txt>", "Convert font metadata (UTF-16BE precache lists become UTF-8)", cli::iniExport},
        {"ini-import", "<in.txt> <out.ini> [--utf16]", "Write font metadata back (--utf16 for precache lists)", cli::iniImport},
    };
    return cmds;
}

void usage() {
    std::printf("nuttool - Banjo-Kazooie: Nuts & Bolts asset tool\n\nusage: nuttool <command> [arguments]\n\n");
    for (auto& c : commands()) std::printf("  %-12s %s\n  %12s   %s\n", c.name, c.args, "", c.help);
    std::printf("\nFiles that are modified in place get a one-time <file>.bak backup unless -o is given.\n");
}

cli::Args parseArgs(int argc, char** argv) {
    static const std::set<std::string> valued = {"-o", "--out", "--stream", "--stream-out", "--base", "--section", "--only",
                                                "--name", "--id", "--version", "--author", "--category", "--description", "--original"};
    cli::Args a;
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (valued.count(s)) {
            if (i + 1 >= argc) fail("option " + s + " needs a value");
            a.opts[s == "--out" ? "-o" : s] = argv[++i];
        } else if (s.size() > 1 && s[0] == '-' && !(s.size() > 1 && std::isdigit(uint8_t(s[1])))) {
            a.flags.insert(s);
        } else {
            a.pos.push_back(s);
        }
    }
    return a;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "help" || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
        usage();
        return argc < 2 ? 1 : 0;
    }
    std::string name = argv[1];
    for (auto& c : commands()) {
        if (name != c.name) continue;
        try {
            return c.fn(parseArgs(argc, argv));
        } catch (std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
    std::fprintf(stderr, "unknown command '%s' (run 'nuttool help')\n", name.c_str());
    return 1;
}
