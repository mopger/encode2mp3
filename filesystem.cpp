#include <iostream>
#include <string.h>
#include <string>

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
    char tmpBuf[MAX_PATH] = { 0, };
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
        bool const isDir = ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY != 0;
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
