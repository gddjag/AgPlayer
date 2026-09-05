#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace agplayer::lossless {
struct SacdTrack {
    int areaIndex=0;
    int trackNumber=0;
    int channels=0;
    bool dstEncoded=false;
    std::uint32_t firstSector=0;
    std::uint32_t sectorCount=0;
    std::int64_t durationMs=0;
};
struct SacdFrame { std::vector<std::uint8_t> bytes; bool dstEncoded=false; };

// Read-only sector parser. No optical-device access, authentication or decryption.
class SacdReader final {
public:
    bool open(const std::string& path,std::string& error,const std::atomic_bool* cancelled=nullptr);
    const std::vector<SacdTrack>& tracks() const noexcept { return tracks_; }
    bool selectTrack(std::size_t index,std::string& error);
    bool readFrame(SacdFrame& frame,bool& eof,std::string& error);
private:
    bool readSector(std::uint64_t sector,std::vector<std::uint8_t>& out,std::string& error);
    bool readArea(std::uint32_t sector,int area,std::string& error);
    bool isCancelled(std::string& error) const;
    std::ifstream file_;
    std::uint64_t fileSize_=0;
    std::uint32_t sectorSize_=2048,payloadOffset_=0;
    std::vector<SacdTrack> tracks_;
    const std::atomic_bool* cancelled_=nullptr;
    SacdTrack selected_;
    std::uint64_t nextSector_=0,endSector_=0;
    std::vector<SacdFrame> ready_;
    SacdFrame partial_;
    bool hasPartial_=false;
    bool hasPreviousTimecode_=false;
    std::uint32_t previousTimecode_=0;
    unsigned remainingDstPackets_=0;
};

// A bounded virtual DSDIFF file. Preflight counts chunks, then replays sectors;
// only one audio frame and one sector are resident. It never writes an audio file.
class SacdTrackStream final {
public:
    bool open(const std::string& path,std::size_t track,std::string& error,const std::atomic_bool* cancelled=nullptr);
    int read(std::uint8_t* destination,int capacity) noexcept;
    std::int64_t seek(std::int64_t offset,int whence) noexcept;
    static int readCallback(void* context,std::uint8_t* destination,int capacity) noexcept;
    static std::int64_t seekCallback(void* context,std::int64_t offset,int whence) noexcept;
    const std::string& error() const noexcept { return error_; }
    const SacdTrack& track() const { return reader_.tracks().at(track_); }
private:
    bool nextChunk();
    bool rewind();
    SacdReader reader_;
    std::size_t track_=0,chunkOffset_=0;
    std::vector<std::uint8_t> header_,chunk_;
    std::uint64_t size_=0,position_=0;
    std::string error_;
};
}
