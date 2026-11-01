// Command implementations for the nuttool CLI.
#pragma once

#include "common.hpp"

#include <map>
#include <set>

namespace nut::cli {

struct Args {
    std::vector<std::string> pos;
    std::map<std::string, std::string> opts;
    std::set<std::string> flags;

    const std::string& at(size_t i, const char* what) const;
    std::string opt(const std::string& k, const std::string& def = "") const;
    bool flag(const std::string& f) const { return flags.count(f) != 0; }
};

int info(const Args& a);
int hash(const Args& a);
int index(const Args& a);
int list(const Args& a);
int decompress(const Args& a);
int compress(const Args& a);
int extract(const Args& a);
int replace(const Args& a);
int caffUnpack(const Args& a);
int caffPack(const Args& a);
int sbUnpack(const Args& a);
int sbPack(const Args& a);
int texExport(const Args& a);
int texImport(const Args& a);
int modelExport(const Args& a);
int textExport(const Args& a);
int textImport(const Args& a);
int fontExport(const Args& a);
int fontImport(const Args& a);
int xmvInfo(const Args& a);
int waveExport(const Args& a);
int iniExport(const Args& a);
int iniImport(const Args& a);
int extractAll(const Args& a);
int verify(const Args& a);
int makeNbpatch(const Args& a);

} // namespace nut::cli
