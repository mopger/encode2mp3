#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "encode2mp3.hpp"

using PathNames = std::vector<PathName>;

PathNames filterFiles(PathNames const& pathNames, std::vector<std::string> const& extentions);
PathNames getCanonicalDirContents(char const* dir);
bool checkPath(const char* rawPath);

#endif // FILESYSTEM_H
