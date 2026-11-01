// nuttool - read, unpack and edit Banjo-Kazooie: Nuts & Bolts (Xbox 360) assets.
#include "commands.hpp"

#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace nut;
    if (argc < 2 || std::string(argv[1]) == "help" || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
        cli::usage();
        return argc < 2 ? 1 : 0;
    }
    return cli::run(std::vector<std::string>(argv + 1, argv + argc));
}
