#include "filesystem.hpp"

int main(int argc, char** args)
{
    if (argc < 3)
        return -1;

    std::vector<std::string> const extentions = {"wav", "pcm", "wave"};
    auto const& files = filterFiles(getCanonicalDirContents(args[2]), extentions);

    try {
        size_t const numFiles = std::stoi(args[1]);
        if (files.size() == numFiles)
            return 0;
    } catch (...) {
    }
    return -1;
}
