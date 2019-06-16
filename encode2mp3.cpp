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
#include <assert.h>
#include <string.h>  // standard C
#include <errno.h>
#include <pthread.h> // POSIX

#include <lame/lame.h>

#include "encode2mp3.hpp"
#include "filesystem.hpp"

using std::vector;
using std::string;
using std::cout;
using std::cerr;
using std::endl;

static pthread_mutex_t consoleMtx;
static vector<string> extentions = { "wav", "wave", "pcm" }; // lower case


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
        && h.bitsPerSample == 16 // transforming 8 bit to 16 resulting in an awful quality mp3
        && h.numChannels    > 0
        && h.sampleRate     > 0
        && h.bitsPerSample  > 0
        && ::memcmp(h.subchunk2ID, "data", 4) == 0;
}

// test lame functions for success or throw with details
static void okOrThrow(int32_t status, int line)
{
    if (status != lame_errorcodes_t::LAME_OKAY)
        throw std::runtime_error("ERROR: lame failed with code " + std::to_string(status) + " at line " + std::to_string(line));
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
    if (!file) {
        cerr << "ERROR! File path is NULL!\n";
        return nullptr;
    }

    auto    inFileName       = static_cast<char const*>(file);
    auto    outFileName      = changeExtention(inFileName);
    auto    inPcm            = std::ifstream(inFileName, std::ifstream::in);
    auto    pcmHeader        = readPcmHeader(inPcm);
    int64_t samplesDeclared  = pcmHeader.subchunk2Size / pcmHeader.blockAlign;
    ::pthread_mutex_lock(&consoleMtx);

    if (!isValid(pcmHeader)) {
        if (pcmHeader.audioFormat != 1)
            cerr << "ERROR! Unsupported audio format: " << inFileName << endl; // cmd swallows "\n"s
        else if (pcmHeader.bitsPerSample != 16)
            cerr << "ERROR! Only 16 bit per sample is supported: " << inFileName << endl;
        else
            cerr << "ERROR! Broken header: " << inFileName << endl;

        ::pthread_mutex_unlock(&consoleMtx);
        inPcm.close();
        return nullptr;
    }

    cout << "Encoding file to " << outFileName << "\n";
    cout << "Number of samples: " << samplesDeclared << endl;

    assert(pcmHeader.bitsPerSample / 8 == 2); // 16 bit per sample
    ::pthread_mutex_unlock(&consoleMtx);

    lame_t pLameGF = lame_init();
    bool const isMono = pcmHeader.numChannels == 1;

    try {
        okOrThrow(::lame_set_mode         (pLameGF, isMono ? MONO : STEREO), __LINE__);
        okOrThrow(::lame_set_in_samplerate(pLameGF, pcmHeader.sampleRate),   __LINE__);
        okOrThrow(::lame_set_VBR          (pLameGF, vbr_off),                __LINE__); // keep it off, affects mp3 length somehow
        okOrThrow(::lame_set_quality      (pLameGF, 5),                      __LINE__);
        okOrThrow(::lame_init_params      (pLameGF),                         __LINE__);
    }
    catch (std::runtime_error const& e) {
        ::pthread_mutex_lock(&consoleMtx);
        cerr << e.what() << endl;
        ::pthread_mutex_unlock(&consoleMtx);
        ::lame_close(pLameGF);
        inPcm.close();
        return nullptr;
    }

    const constexpr size_t PCM_BUF_SIZE = 8192; // L+R channels of 16 bits each
    const constexpr size_t MP3_BUF_SIZE = 8192; // bytes

    auto         pcmBuffer        = vector<int16_t>(PCM_BUF_SIZE, 0);
    auto         mp3Buffer        = vector<uint8_t>(MP3_BUF_SIZE, 0);
    auto         outMp3           = std::ofstream(outFileName.c_str(), std::ios_base::binary | std::ofstream::out);
    int32_t      toWrite          = 0;
    int32_t      samplesReadTotal = 0;
    bool         isMoreSamples    = true;
    size_t const toRead           = PCM_BUF_SIZE / (isMono ? 2u : 1u); // read half of the buffer size in MONO mode

    do {
        inPcm.read(reinterpret_cast<char*>(pcmBuffer.data()), static_cast<std::streamsize>(toRead));

        auto const bytesRead = static_cast<int32_t>(inPcm.gcount());
        assert(bytesRead != 0);
        auto samplesRead = bytesRead / (pcmHeader.bitsPerSample / 8) / pcmHeader.numChannels;

        if (samplesReadTotal + samplesRead >= samplesDeclared) {
            samplesRead = static_cast<int32_t>(samplesDeclared - samplesReadTotal);
            isMoreSamples = false;
        }

        samplesReadTotal += samplesRead;

        if (isMono) {
            int16_t* emptyChannel = pcmBuffer.data() + pcmBuffer.size() / 2;
            assert(std::all_of(emptyChannel, emptyChannel + pcmBuffer.size() / 2, [](auto b){ return b == 0; })); // is right channel empty?
            toWrite = ::lame_encode_buffer(pLameGF, pcmBuffer.data(), emptyChannel, samplesRead, mp3Buffer.data(), MP3_BUF_SIZE);
        }
        else
            toWrite = ::lame_encode_buffer_interleaved(pLameGF, pcmBuffer.data(), samplesRead, mp3Buffer.data(), MP3_BUF_SIZE);

        assert(toWrite >= 0);
        outMp3.write(reinterpret_cast<char*>(mp3Buffer.data()), toWrite);
    } while (!inPcm.eof() && isMoreSamples);

    assert(samplesDeclared == samplesReadTotal);
    toWrite = ::lame_encode_flush(pLameGF, mp3Buffer.data(), MP3_BUF_SIZE);
    outMp3.write(reinterpret_cast<char*>(mp3Buffer.data()), toWrite);
    outMp3.flush();
    outMp3.close();
    inPcm.close();
    ::lame_close(pLameGF);

    ::pthread_mutex_lock(&consoleMtx);
    cout << "Finished encoding file " << outFileName << endl;
    ::pthread_mutex_unlock(&consoleMtx);
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

    for (auto& worker : workers)
        ::pthread_join(worker.thread, &worker.pStatus);
}


static void printExtentionsMsg()
{
    cout << "Supported file extentions: ";
    for (auto const& ext : extentions)
        cout << '.' << ext << " ";
    cout << "\n";
}


int main(int argNum, char** args)
{
    printExtentionsMsg();

    if (argNum != 2) {
        cerr << "Error: folder not specified!\n"
                "Usage: encode2mp3 folder_name\n";

        return -1;
    }
    
    if (!checkPath(args[1])) {
        cerr << "ERROR! UNIX console detected! Please, use '/' or '\\\\' path separators instead of '\\'\n";
        return -1;
    }

    auto const& files = filterFiles(getCanonicalDirContents(args[1]), extentions);

    if (files.empty()) {
        cerr << "An error happened or the directory doesn't exist or has no supported files!\n";
        return -1;
    }

    cout << "Found " << files.size() << " files to encode\n";
    encodeAll2Mp3(files);
    return 0;
}
