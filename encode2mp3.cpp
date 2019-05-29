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

#include <algorithm>
#include <iostream>  // standard C++
#include <fstream>
#include <string>
#include <vector>
#include <limits>
#include <assert.h>
#include <string.h>  // standard C
#include <errno.h>
#include <pthread.h> // POSIX

#ifdef __linux__
  #include <dirent.h>
  #include <sys/stat.h>
#else
  #include <windows.h>
  #include <tchar.h>
#endif

#include <lame/lame.h>

using std::vector;
using std::string;
using std::cout;

enum class PathType : uint8_t { File, Dir };

struct Worker
{
    int32_t   status;
    pthread_t thread;
};

struct PathName
{
    PathType type;
    string   name;
};

#pragma pack(push, 1)
struct PcmHeader
{
    char     chunkID[4]; // all char[] here is text fields
    uint32_t chunkSize;
    char     format[4];
    char     subchunk1ID[4];
    uint32_t subchunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    int32_t  sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char     subchunk2ID[4];
    uint32_t subchunk2Size;
};
#pragma pack (pop)

using PathNames = vector<PathName>;

static pthread_mutex_t consoleMtx;
static vector<string> extentions = { "wav", "wave", "pcm" }; // lower case


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


static PathNames getCanonicalDirContents(char const* dir)
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
                pathNames.push_back({ getPathType(pth), pth }); // emplace don't work
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
static PathNames getCanonicalDirContents(char const* dir)
{
    constexpr auto const separator  = "/"; // supported by both linux and windows
    PathNames pathNames;
    //char realPath[MAX_PATH] = { 0, };
    char tmpBuf[MAX_PATH] = { 0, };
    auto const dirLen = ::strlen(dir);
    auto const separatorLen = ::strlen(separator);

    #define BUFSIZE 4096

    TCHAR   buffer[BUFSIZE] = TEXT("");
    //TCHAR   buf[BUFSIZE]    = TEXT("");
    TCHAR** lppPart         = {NULL};

    auto rv = ::GetFullPathName(dir, BUFSIZE, buffer, lppPart);
    if (rv == 0)
        cout << "Error!" << std::endl;
    else {
        cout << buffer << std::endl;
        if (lppPart)
            cout << lppPart << std::endl;
    }

    return {};
}
#endif

// filter set of files by their extentions
static PathNames filterFiles(PathNames const& pathNames, vector<string> const& extentions)
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

            if (std::equal(extentionAllLowerBackwards.rbegin(), extentionAllLowerBackwards.rbegin() + extSize,
                           extention.begin())) {
                out.push_back(pathName);
                break;
            }
        }
    }

    return out;
}

// read PCM file into PcmHeader structure
static PcmHeader readPcmHeader(std::ifstream& pcm)
{
    PcmHeader pcmHeader;
    static_assert (sizeof(PcmHeader) == 44, "Wrong PCM header structure!");
    pcm.read(reinterpret_cast<char*>(&pcmHeader), sizeof(PcmHeader));
    return pcmHeader;
}

// check if PCM header contains required data
static bool isValid(PcmHeader const& h)
{
    return h.audioFormat   == 1
        && h.bitsPerSample >= 8
        && h.numChannels    > 0
        && h.sampleRate     > 0
        && h.bitsPerSample  > 0;
}

// test lame functions for success or throw with details
static void okOrThrow(int32_t status, int line)
{
    if (status != lame_errorcodes_t::LAME_OKAY)
        throw std::runtime_error("Lame failed with code " + std::to_string(status) + " at line " + std::to_string(line));
}

// transform 8 bit samples to 16 bit to pass it to lame encoder
// 8 bits are bad for mp3 so that's the reason why the encoder
// doesn't accept 8 bit samples
// maybe I shouldn't do that...
static void transform8To16Bit(int16_t* arr16Bit, int64_t arrSz16Bit)
{
    int8_t* arr8Bit = reinterpret_cast<int8_t*>(arr16Bit);
    std::copy_backward(arr8Bit, arr8Bit + arrSz16Bit, arr16Bit + arrSz16Bit);
    //while (--arrSz16Bit >=0) arr16Bit[arrSz16Bit] = arr8Bit[arrSz16Bit];
}

// replace file extention with "mp3"
static string changeExtention(string fileName)
{
    while (fileName.back() != '.')
        fileName.pop_back();

    fileName.append("mp3");
    return fileName;
}

// thread worker: 1 file - 1 worker
// sadly, lame doesn't support multithread encoding for a singlle file...
static void* encode2mp3Worker(void* file)
{
    auto inFileName = static_cast<char const*>(file);
    auto outFileName = changeExtention(inFileName);

    std::ifstream inPcm(inFileName, std::ifstream::in);
    auto pcmHeader = readPcmHeader(inPcm);

    pthread_mutex_lock(&consoleMtx);

    if (!isValid(pcmHeader)) {
        inPcm.close();

        if (pcmHeader.audioFormat != 1)
            cout << "Unsupported audio format: " << inFileName << std::endl;
        else
            cout << "Can't encode: broken header in " << inFileName << std::endl;

        pthread_mutex_unlock(&consoleMtx);
        return nullptr;
    }
    else
        cout << "Encoding file " << inFileName << " to " << outFileName << std::endl;

    if (pcmHeader.bitsPerSample < 16)
        cout << "Warning! 8 bits per sample will result in a low quality mp3!" << std::endl;

    pthread_mutex_unlock(&consoleMtx);

    lame_t pLameGF = lame_init();
    bool const isMono = pcmHeader.numChannels == 1;

    try {
        okOrThrow(::lame_set_mode         (pLameGF, isMono ? MONO : STEREO), __LINE__);
        okOrThrow(::lame_set_in_samplerate(pLameGF, pcmHeader.sampleRate),   __LINE__);
        okOrThrow(::lame_set_VBR          (pLameGF, vbr_default),            __LINE__);
        okOrThrow(::lame_set_quality      (pLameGF, 5),                      __LINE__);
        //okOrThrow(::lame_set_preset       (pLameGF, 128),                    __LINE__);
        okOrThrow(::lame_init_params      (pLameGF),                         __LINE__);
    }
    catch (std::runtime_error& e) {
        cout << e.what() << std::endl;
        ::lame_close(pLameGF);
        inPcm.close();
        return nullptr;
    }

    const constexpr size_t PCM_NUM_ELEMS = 8192; // elements
    const constexpr size_t MP3_BUF_SIZE  = 8192; // bytes

    int16_t pcmBuffer[2 * PCM_NUM_ELEMS] = {}; // 16 bit * 2 channel * PCM_NUM_ELEMS
    uint8_t mp3Buffer[MP3_BUF_SIZE];
    int32_t toWrite = 0;
    size_t const toRead = sizeof(pcmBuffer) / (isMono ? 2u : 1u); // read half of the buffer size in MONO mode
    std::ofstream outMp3(outFileName.c_str(), std::ios_base::binary | std::ofstream::out);

    do {
        inPcm.read(reinterpret_cast<char*>(&pcmBuffer), static_cast<std::streamsize>(toRead));

        auto const bytesRead = static_cast<int32_t>(inPcm.gcount());
        auto const samplesRead = bytesRead / (pcmHeader.bitsPerSample / 8) / pcmHeader.numChannels;

        if (pcmHeader.bitsPerSample == 8)
            transform8To16Bit(pcmBuffer, sizeof(pcmBuffer));

        if (isMono) {
            int16_t dummyPcmRightChannel[sizeof(pcmBuffer)] = {}; // right channel is ignored in MONO mode
            toWrite = ::lame_encode_buffer(pLameGF, pcmBuffer, dummyPcmRightChannel, samplesRead, mp3Buffer, MP3_BUF_SIZE);
        }
        else
            toWrite = ::lame_encode_buffer_interleaved(pLameGF, pcmBuffer, samplesRead, mp3Buffer, MP3_BUF_SIZE);

        if (toWrite == 0)
            std::cerr << "toWrite == 0 on " << inFileName;

        outMp3.write(reinterpret_cast<char*>(&mp3Buffer), toWrite);
    } while (!inPcm.eof());

    toWrite = ::lame_encode_flush(pLameGF, mp3Buffer, MP3_BUF_SIZE);
    outMp3.write(reinterpret_cast<char*>(&mp3Buffer), toWrite);

    ::lame_close(pLameGF);
    inPcm.close();
    outMp3.close();

    pthread_mutex_lock(&consoleMtx);
    cout << "Finished encoding file " << outFileName << std::endl;
    pthread_mutex_unlock(&consoleMtx);
    return nullptr;
}

// run worker for each file in a list
static void encodeAll2Mp3(PathNames const& files)
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

    cout << "Found " << files.size() << " files to encode" << std::endl;
    encodeAll2Mp3(files);
    return 0;
}
