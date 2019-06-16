#ifndef ENCODE2MP3_H
#define ENCODE2MP3_H

#include <stdint.h>
#include <string>
#include <vector>
#include <pthread.h>

enum class PathType : uint8_t { File, Dir };

struct Worker
{
    int32_t   status;
    void*     pStatus = &status;
    pthread_t thread;
};

struct PathName
{
    PathType    type;
    std::string name;
};

//canonical format
#pragma pack(push, 1)
struct PcmHeader
{
    char     chunkID[4]; // all char[] here is text fields, w/o null termination
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

#endif // ENCODE2MP3_H
