// Write a C/C++ command line application that encodes a set of WAV files to MP3
// Requirements:
//   (1) application is called with pathname as argument, e.g.
//     <applicationname> F:\MyWavCollection all WAV-files contained directly in that folder are to be encoded to MP3
//   (2) use all available CPU cores for the encoding process in an efficient way by utilizing multi-threading
//   (3) statically link to lame encoder library
//   (4) application should be compliable and runnable on Windows and Linux
//   (5) the resulting MP3 files are to be placed within the same directory as the source WAV files, the filename extension should be changed appropriately to .MP3
//   (6) non-WAV files in the given folder shall be ignored
//   (7) multithreading shall be implemented by means of using Posix Threads (there exist implementations for Windows)
//   (8) the Boost library shall not be used
//   (9) the LAME encoder should be used with reasonable standard settings (e.g. quality based encoding with quality level "good")

#include <pthread.h>
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <string.h>
#include <string>
#include <vector>
#include <limits>
#include <errno.h>
#include <sys/stat.h>
#include <assert.h>

#include <lame/lame.h>

using std::vector;
using std::string;
using std::cout;

enum class PathType : uint8_t { File, Dir };

struct Worker
{
    int       status;
    pthread_t thread;
};

struct PathName
{
    PathType type;
    string   name;
};

struct PcmHeader {

};

using PathNames = vector<PathName>;

static pthread_mutex_t encodeMtx;
static vector<string> extentions = { "wav", "wave", "pcm" };

[[noreturn]]
void printErrorAndAbort(char const* msg)
{
    ::perror(msg);
    ::abort();
}


PathType getPathType(char const* path) {
    struct stat s;

    if (::stat(path, &s) == 0) {
        if (s.st_mode & S_IFDIR)
            return PathType::Dir;
        if (s.st_mode & S_IFREG)
            return PathType::File;
    }

    printErrorAndAbort("Path is not a file nor a dir");
}


// return a vector of the canonicalized absolute pathnames for
// all files and dirs (even . and ..) that belong to the given
// directory dir, terminates the app on error
PathNames getCanonicalDirContents(char const* dir)
{
    constexpr auto const separator  = "/"; // supported by both linux and windows
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
                pathNames.push_back({ getPathType(pth), pth });
                continue;
            }

            printErrorAndAbort("Got an error when parsing file list");
        }
        ::closedir(dp);
    }

    return pathNames;
}


PathNames filterFiles(PathNames const& pathNames, vector<string> const& extentions)
{
    PathNames out;

    for (auto const& pathName : pathNames) {
        if (pathName.type != PathType::File)
            continue;

        auto const& name = pathName.name;
        bool addItd = false;

        for (auto const& extention : extentions) {
            if (name.size() < extention.size() + 1) // enough to contain '.' + extention
                continue;

            auto const extSize = static_cast<int>(extention.size());
            assert(extention.size() <= std::numeric_limits<decltype(extSize)>::max());

            if (*(name.rbegin() + extSize) != '.') // check extention has a '.'
                continue;

            addItd = std::equal(name.rbegin(), name.rbegin() + extSize, extention.rbegin());

            if (addItd) {
                out.push_back(pathName);
                break;
            }
        }
    }
    return out;
}


PcmHeader parsePchHeader(std::ifstream& pcm)
{
    return {};
}


void* encode2mp3Worker(void* file)
{
    auto inFileName = static_cast<char const*>(file);
    auto outFileName = string(inFileName);
    while (outFileName.back() != '.')
        outFileName.pop_back();
    outFileName.append("mp3");

    std::ifstream inPcm(inFileName, std::ifstream::in);
    std::ofstream outMp3(outFileName.c_str(), std::ofstream::out);
    lame_t pLameGlobalFlags = lame_init();
    auto header = parsePchHeader(inPcm);


    pthread_mutex_lock(&encodeMtx); // no need to lock
    cout << "in: " << inFileName << '\n' << "out: " << outFileName << "\n";
    pthread_mutex_unlock(&encodeMtx);

    inPcm.close();
    outMp3.close();
    lame_close(pLameGlobalFlags);
    return nullptr;
}


int encodeAll2Mp3(PathNames const& files)
{
    vector<Worker> workers(files.size());

    for (size_t idx = 0; idx < files.size(); ++idx) {
        void* file = reinterpret_cast<void*>(const_cast<char*>(files[idx].name.c_str()));

        if (::pthread_create(&workers[idx].thread, nullptr, &encode2mp3Worker, file) != 0)
            throw std::runtime_error("pthread_create() failed");
    }

    for (auto& worker : workers) {
        void* pstatus = &worker.status;
        ::pthread_join(worker.thread, &pstatus);
    }

    return 0;
}


int main(int argNum, char** args)
{
    if (argNum != 2) {
        cout << "Error: folder not specified!\n"
                "Usage: encode2mp3 folder_name\n"
                "Supported extentions: ";

        for (auto const& extention : extentions)
            cout << "." << extention << " ";
        cout << std::endl;
        return -1;
    }

    cout << "argument: " << args[1] << std::endl;

    auto const& files = filterFiles(getCanonicalDirContents(args[1]), extentions);

    if (files.empty()) {
        cout << "The directory is not exists or contain any supported file!" << std::endl;
        return -1;
    }

    return encodeAll2Mp3(files);
}
