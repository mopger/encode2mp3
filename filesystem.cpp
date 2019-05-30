#include <iostream>
#include <string.h>
#include <string>

#ifdef __linux__
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


#ifdef __linux__
// DIR or FILE or DIE!
static PathType getPathType(char const* path)
{
    struct stat s;

    if (::stat(path, &s) == 0) {
        if (s.st_mode & S_IFDIR)
            return PathType::Dir;
        if (s.st_mode & S_IFREG)
            return PathType::File;
    }

    printErrorAndAbort("Path is neither a file nor a dir");
}


PathNames getCanonicalDirContents(char const* dir)
{
    constexpr auto const separator  = "/";
    PathNames pathNames;
    char realPath[PATH_MAX] = { 0, };
    char tmpBuf[PATH_MAX] = { 0, };
    auto const dirLen = ::strlen(dir);
    auto const separatorLen = ::strlen(separator);

    if (DIR* dp = ::opendir(dir)) {
        while (auto entry = ::readdir(dp)) {
            ::strcpy(tmpBuf, dir);
            ::strcpy(tmpBuf + dirLen, separator);
            ::strcpy(tmpBuf + dirLen + separatorLen, entry->d_name);

            if (char const* pth = ::realpath(tmpBuf, realPath)) {
                pathNames.push_back({ getPathType(pth), pth }); // emplace doesn't work
                continue;
            }

            printErrorAndAbort("Got an error when parsing file list");
        }

        ::closedir(dp);
    }

    return pathNames;
}
#else
// return a vector of the canonicalized absolute pathnames for
// all files and dirs (even . and ..) that belong to the given
// directory dir, terminates the app on error
PathNames getCanonicalDirContents(char const* dir)
{
    auto static const constexpr BUF_SZ     = 4096;
    auto static const constexpr separator  = "\\";
    char tmpBuf[MAX_PATH] = { 0, };
    TCHAR   buffer[BUF_SZ] = TEXT("");
    TCHAR** lppPart        = { nullptr };
    auto rv = ::GetFullPathName(dir, BUF_SZ, buffer, lppPart);

    if (rv == 0) {
        cout << "Error!" << std::endl;
        return {};
    }

    if (::strlen(buffer) > (MAX_PATH - 3)) {
        cerr << "Directory path is too long!\n";
        return {};
    }

    TCHAR path[MAX_PATH];
    ::strcpy(path, buffer);
    ::strcat(path, "\\*");

    WIN32_FIND_DATA ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    hFind = ::FindFirstFile(path, &ffd);

    if (hFind == INVALID_HANDLE_VALUE) {
        cerr << "FindFirstFile\n";
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
        cerr << "FindNextFile\n";
        return {};
    }

    ::FindClose(hFind);
    return pathNames;
}
#endif
