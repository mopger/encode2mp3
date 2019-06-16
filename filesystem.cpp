#include <algorithm>
#include <iostream>
#include <limits>
#include <string.h>
#include <string>
#include <vector>

#if defined (__linux__) || defined (__linux) || defined (__gnu_linux__)
#include <dirent.h>
#include <sys/stat.h>
#else
#include <windows.h>
#include <tchar.h>
#endif

#include "filesystem.hpp"
#include "encode2mp3.hpp"

using std::cout;
using std::cerr;
using std::string;
using std::vector;


[[noreturn]]
static void printErrorAndAbort(char const* msg)
{
    ::perror(msg);
    ::abort();
}


#if defined (__linux__) || defined (__linux) || defined (__gnu_linux__)
// DIR or FILE or DIE!
static PathType getPathType(char const* path)
{
    if (!path)
        printErrorAndAbort("ERROR: path is NULL!");

    struct stat s;

    if (::stat(path, &s) == 0) {
        if (s.st_mode & S_IFDIR)
            return PathType::Dir;
        if (s.st_mode & S_IFREG)
            return PathType::File;
    }

    printErrorAndAbort("ERROR: giver dir has is neither a file nor a dir object!");
}

// return a vector of the canonicalized absolute pathnames for
// all files and dirs (even . and ..) that belong to the given
// directory dir, terminates the app on error
PathNames getCanonicalDirContents(char const* dir)
{
    if (!dir)
        printErrorAndAbort("ERROR: directory name is NULL!");

    constexpr auto const separator  = "/";
    PathNames pathNames;
    char realPath[PATH_MAX] = { 0, };
    char filePath[PATH_MAX] = { 0, };
    auto const dirLen = ::strlen(dir);
    auto const separatorLen = ::strlen(separator);

    if (DIR* dp = ::opendir(dir)) {
        while (auto entry = ::readdir(dp)) {
            ::strcpy(filePath, dir);
            ::strcpy(filePath + dirLen, separator);
            ::strcpy(filePath + dirLen + separatorLen, entry->d_name);

            if (char const* pth = ::realpath(filePath, realPath)) {
                pathNames.push_back({ getPathType(pth), pth }); // emplace doesn't work
                continue;
            }

            printErrorAndAbort("ERROR: parsing file list failed");
        }

        ::closedir(dp);
    }

    return pathNames;
}
#else
PathNames getCanonicalDirContents(char const* dir)
{
    if (!dir)
        printErrorAndAbort("ERROR: directory name is NULL!");

    auto static const constexpr BUF_SZ     = 4096;
    auto static const constexpr separator  = "\\";
    TCHAR   buffer[BUF_SZ] = TEXT("");
    TCHAR** lppPart        = { nullptr };
    auto rv = ::GetFullPathName(dir, BUF_SZ, buffer, lppPart);

    if (rv == 0) {
        cerr << "ERROR: Can't get full path!\n";
        return {};
    }

    if (::strlen(buffer) > (MAX_PATH - 3)) {
        cerr << "ERROR: directory path is too long!\n";
        return {};
    }

    TCHAR path[MAX_PATH];
    ::strcpy(path, buffer);
    ::strcat(path, "\\*");

    WIN32_FIND_DATA ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    hFind = ::FindFirstFile(path, &ffd);

    if (hFind == INVALID_HANDLE_VALUE) {
        cerr << "ERROR: FindFirstFile wrong status\n";
        return {};
    }

    PathNames pathNames;

    do {
        bool const isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        auto path = std::string(buffer).append(separator).append(ffd.cFileName);
        auto type = isDir ? PathType::Dir : PathType::File;
        pathNames.push_back({ type, std::move(path) });
    } while (::FindNextFile(hFind, &ffd) != 0);

    if (::GetLastError() != ERROR_NO_MORE_FILES) {
        cerr << "ERROR: FindNextFile wrong status\n";
        return {};
    }

    ::FindClose(hFind);
    return pathNames;
}
#endif


// filter set of files by their extentions
PathNames filterFiles(PathNames const& pathNames, vector<string> const& extentions)
{
    PathNames out;

    for (auto const& pathName : pathNames) {
        if (pathName.type != PathType::File)
            continue;

        auto const& name = pathName.name;

        for (auto const& extention : extentions) {
            if (name.size() < extention.size() + 1) // enough to contain '.' + extention
                continue;

            auto const extSize = static_cast<int32_t>(extention.size());
            if (extention.size() > std::numeric_limits<decltype(extSize)>::max())
                throw std::out_of_range("Extention size it too big!");

            if (*(name.rbegin() + extSize) != '.') // check extention has a '.'
                continue;

            string extentionAllLowerBackwards;
            std::transform(name.rbegin(), name.rbegin() + extSize,
                           std::back_inserter(extentionAllLowerBackwards),
                           [](char c) { return ::tolower(c); });

            bool const isEqualExtention = std::equal(
                        extentionAllLowerBackwards.rbegin(),
                        extentionAllLowerBackwards.rbegin() + extSize,
                        extention.begin());

            if (isEqualExtention) {
                out.push_back(pathName);
                break;
            }
        }
    }

    return out;
}


// check if a terminal successfully passed path separators because
// UNIX-type (under Windows too, cygwin, for ex.) terminals strip
// escape character '\' so c:\folder1\folder2 passed as a parameter
// becomes c:folder1folder2
// Windows terminal (cmd.exe) doesn't do that
bool checkPath(const char* rawPath)
{
    if (!rawPath)
        return false;

    while (*rawPath) {
        if (*rawPath == '/' || *rawPath == '\\')
            return true;

        ++rawPath;
    }

    return false;
}
