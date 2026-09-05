#include "sacd_reader.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

extern "C" {
#include <libavutil/error.h>
#include <libavformat/avio.h>
}

namespace agplayer::lossless {
namespace {
constexpr std::size_t kPayload=2048,kMaximumFrameBytes=65536,kAreaMaximumSectors=96;
constexpr std::uint64_t kMasterSector=510;
constexpr std::uint32_t kDsdRate=2822400,kDsdBytesPerFramePerChannel=kDsdRate/8/75;
std::uint16_t u16(const std::vector<std::uint8_t>& b,std::size_t p) { return static_cast<std::uint16_t>((b.at(p)<<8)|b.at(p+1)); }
std::uint32_t u32(const std::vector<std::uint8_t>& b,std::size_t p) { return (static_cast<std::uint32_t>(u16(b,p))<<16)|u16(b,p+2); }
bool tagged(const std::vector<std::uint8_t>& b,const char* s) { return b.size()>=8 && std::memcmp(b.data(),s,8)==0; }
void append(std::vector<std::uint8_t>& b,const char* s,std::size_t n=4) { b.insert(b.end(),s,s+n); }
void number(std::vector<std::uint8_t>& b,std::uint64_t n,int bytes) { for(int i=bytes-1;i>=0;--i)b.push_back(static_cast<std::uint8_t>(n>>(i*8))); }
void chunk(std::vector<std::uint8_t>& b,const char* id,const std::vector<std::uint8_t>& payload) {
    append(b,id);number(b,payload.size(),8);b.insert(b.end(),payload.begin(),payload.end());if(payload.size()%2)b.push_back(0);
}
bool frameLengthValid(const SacdFrame& f,int channels) {
    return !f.bytes.empty() && (f.dstEncoded || f.bytes.size()==static_cast<std::size_t>(channels)*kDsdBytesPerFramePerChannel);
}
}
bool SacdReader::isCancelled(std::string& error) const {
    if(cancelled_ && cancelled_->load(std::memory_order_relaxed)){error="SACD analysis cancelled";return true;}return false;
}
bool SacdReader::readSector(std::uint64_t sector,std::vector<std::uint8_t>& out,std::string& error) {
    if(isCancelled(error))return false;
    if(sector>=fileSize_/sectorSize_){error="SACD sector extends beyond the image";return false;}
    file_.clear();file_.seekg(static_cast<std::streamoff>(sector*sectorSize_+payloadOffset_));
    out.resize(kPayload);file_.read(reinterpret_cast<char*>(out.data()),kPayload);
    if(file_.gcount()!=kPayload){error="Truncated SACD sector";return false;}return true;
}
bool SacdReader::open(const std::string& path,std::string& error,const std::atomic_bool* cancelled) {
    error.clear();file_.close();file_.clear();tracks_.clear();ready_.clear();partial_={};hasPartial_=false;
    nextSector_=endSector_=0;hasPreviousTimecode_=false;previousTimecode_=0;remainingDstPackets_=0;cancelled_=cancelled;
    if(isCancelled(error))return false;
    file_.open(std::filesystem::u8path(path),std::ios::binary);
    if(!file_){error="Cannot open SACD image read-only";return false;}
    file_.seekg(0,std::ios::end);const auto length=file_.tellg();
    if(length<0){error="Cannot measure SACD image";return false;}fileSize_=static_cast<std::uint64_t>(length);
    std::vector<std::uint8_t> master;bool found=false;
    for(const auto width: {2048U,2064U}){
        sectorSize_=width;payloadOffset_=width==2064?12U:0U;
        if(readSector(kMasterSector,master,error)&&tagged(master,"SACDMTOC")){found=true;break;}
    }
    if(!found){if(!isCancelled(error))error="Not a supported SACD image: master TOC not found";return false;}
    if(master[8]!=1 || master[9]>20){error="Unsupported SACD TOC version";return false;}
    if(fileSize_%sectorSize_!=0){error="Truncated SACD image sector";return false;}
    // TOC address fields are big-endian sector numbers, including backup copies.
    for(int area=0;area<2;++area){
        const auto address=u32(master,64+static_cast<std::size_t>(area)*8);
        const auto backup=u32(master,68+static_cast<std::size_t>(area)*8);
        if(address==0&&backup==0)continue;
        const auto count=tracks_.size();
        if(address && readArea(address,area,error))continue;
        tracks_.resize(count);
        if(backup && backup!=address && readArea(backup,area,error))continue;
        tracks_.clear();return false;
    }
    if(tracks_.empty()){error="SACD contains no supported audio area";return false;}
    error.clear();return true;
}
bool SacdReader::readArea(std::uint32_t sector,int area,std::string& error) {
    std::vector<std::uint8_t> toc;if(!readSector(sector,toc,error))return false;
    const bool stereo=tagged(toc,"TWOCHTOC");
    if(!stereo&&!tagged(toc,"MULCHTOC")){error="Invalid SACD audio area TOC";return false;}
    const auto sectors=u16(toc,10);const int channels=toc[32];const int count=toc[69];const int format=toc[21]&15;
    if(toc[8]!=1||toc[9]>20||toc[20]!=4||sectors<2||sectors>kAreaMaximumSectors||channels<1||channels>6||count<1
       ||(stereo&&channels!=2)||(format!=0&&format!=2&&format!=3)){
        error="Unsupported SACD area version, channel layout or frame format";return false;
    }
    const auto first=u32(toc,72),last=u32(toc,76);
    if(first>last||last>=fileSize_/sectorSize_||static_cast<std::uint64_t>(sector)+sectors>fileSize_/sectorSize_){error="SACD area boundary outside image";return false;}
    std::vector<std::uint8_t> table,times,block;
    for(std::uint32_t n=1;n<sectors;++n){
        if(!readSector(static_cast<std::uint64_t>(sector)+n,block,error))return false;
        if(tagged(block,"SACDTRL1"))table=block;
        else if(tagged(block,"SACDTRL2"))times=block;
    }
    if(table.empty()){error="SACD track offset table is missing";return false;}
    std::uint64_t previousEnd=first;
    for(int n=0;n<count;++n){
        const auto start=u32(table,8+static_cast<std::size_t>(n)*4),length=u32(table,1028+static_cast<std::size_t>(n)*4);
        const auto end=static_cast<std::uint64_t>(start)+length;
        if(length==0||start<first||start<previousEnd||end>static_cast<std::uint64_t>(last)+1){error="Invalid or overlapping SACD track extent";return false;}
        std::int64_t duration=0;
        if(!times.empty()){
            const auto p=1028+static_cast<std::size_t>(n)*4;
            if(times[p+1]>=60||times[p+2]>=75){error="Invalid SACD track timecode";return false;}
            duration=(static_cast<std::int64_t>(times[p])*60+times[p+1])*1000+times[p+2]*1000/75;
        }
        tracks_.push_back({area,n+1,channels,format==0,start,length,duration});previousEnd=end;
    }
    return true;
}
bool SacdReader::selectTrack(std::size_t index,std::string& error) {
    error.clear();if(index>=tracks_.size()){error="Invalid SACD track index";return false;}
    if(isCancelled(error))return false;selected_=tracks_[index];nextSector_=selected_.firstSector;
    endSector_=nextSector_+selected_.sectorCount;ready_.clear();partial_={};hasPartial_=false;
    hasPreviousTimecode_=false;previousTimecode_=0;remainingDstPackets_=0;return true;
}
bool SacdReader::readFrame(SacdFrame& output,bool& eof,std::string& error) {
    output={};eof=false;error.clear();if(isCancelled(error))return false;
    while(ready_.empty()){
        if(nextSector_>=endSector_){
            if(hasPartial_){
                if(!frameLengthValid(partial_,selected_.channels)||(partial_.dstEncoded&&remainingDstPackets_!=0)){
                    error="Incomplete final SACD audio frame";return false;
                }
                ready_.push_back(std::move(partial_));partial_={};hasPartial_=false;break;
            }
            eof=true;return true;
        }
        std::vector<std::uint8_t> b;if(!readSector(nextSector_++,b,error))return false;
        const std::size_t packets=b[0]>>5,frames=(b[0]>>2)&7;const bool dst=(b[0]&1)!=0;
        if(dst!=selected_.dstEncoded||packets==0||(b[0]&2)!=0){error="Unsupported or damaged SACD audio sector (possibly protected)";return false;}
        std::size_t offset=1+packets*2+frames*(dst?4U:3U);std::size_t starts=0;
        if(offset>kPayload){error="SACD packet directory overflow";return false;}
        std::array<unsigned,7> dstPacketCounts{};
        for(std::size_t n=0;n<frames;++n){
            const auto at=1+packets*2+n*(dst?4U:3U);
            if(b[at+1]>=60||b[at+2]>=75){error="Invalid SACD frame timecode";return false;}
            const auto timecode=(static_cast<std::uint32_t>(b[at])*60U+b[at+1])*75U+b[at+2];
            if(hasPreviousTimecode_&&timecode!=previousTimecode_+1U){error="Non-consecutive SACD frame timecode";return false;}
            hasPreviousTimecode_=true;previousTimecode_=timecode;
            if(dst){
                dstPacketCounts[n]=(b[at+3]>>2)&31U;
                if(dstPacketCounts[n]==0){error="Invalid DST frame sector count";return false;}
            }
        }
        std::size_t frameInfoIndex=0;
        for(std::size_t p=0;p<packets;++p){
            const auto info=u16(b,1+p*2);const std::size_t length=info&0x7ff;
            const unsigned type=(info>>11)&7;const bool start=(info&0x8000)!=0;
            if((info&0x4000)!=0||length>kPayload-offset){error="SACD packet exceeds sector boundary";return false;}
            if(type==2){
                if(start){
                    ++starts;
                    if(frameInfoIndex>=frames){error="SACD frame-start directory mismatch";return false;}
                    if(hasPartial_){
                        if(!frameLengthValid(partial_,selected_.channels)||(partial_.dstEncoded&&remainingDstPackets_!=0)){
                            error="Invalid or truncated SACD audio frame";return false;
                        }
                        ready_.push_back(std::move(partial_));partial_={};
                    }
                    hasPartial_=true;partial_.dstEncoded=dst;
                    if(dst)remainingDstPackets_=dstPacketCounts[frameInfoIndex];
                    ++frameInfoIndex;
                }
                if(!hasPartial_){error="SACD track starts inside an audio frame";return false;}
                if(dst){
                    if(remainingDstPackets_==0){error="DST frame exceeds its declared sector count";return false;}
                    --remainingDstPackets_;
                }
                if(partial_.bytes.size()+length>kMaximumFrameBytes){error="SACD audio frame exceeds safety limit";return false;}
                partial_.bytes.insert(partial_.bytes.end(),b.begin()+static_cast<std::ptrdiff_t>(offset),b.begin()+static_cast<std::ptrdiff_t>(offset+length));
            }else if(type!=3&&type!=7){error="Unsupported SACD packet type";return false;}
            offset+=length;
        }
        if(starts!=frames){error="SACD frame-start directory mismatch";return false;}
    }
    output=std::move(ready_.front());ready_.erase(ready_.begin());return true;
}

bool SacdTrackStream::open(const std::string& path,std::size_t track,std::string& error,const std::atomic_bool* cancelled){
    error_.clear();header_.clear();chunk_.clear();size_=position_=0;track_=track;
    if(!reader_.open(path,error,cancelled)||!reader_.selectTrack(track,error))return false;
    const auto metadata=reader_.tracks()[track];std::uint64_t payloadBytes=0,frameCount=0;SacdFrame frame;bool eof=false;
    while(true){
        if(!reader_.readFrame(frame,eof,error))return false;if(eof)break;
        const auto frameBytes=frame.bytes.size()+(metadata.dstEncoded?12+frame.bytes.size()%2:0);
        // AVIO addresses are signed 64-bit. Reserve space for the small DFF
        // metadata header, so every later length conversion remains exact.
        constexpr auto maxPayload=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())-4096;
        if(frameBytes>maxPayload-payloadBytes){error="SACD virtual stream is too large";return false;}
        payloadBytes+=frameBytes;++frameCount;
        if(frameCount>std::numeric_limits<std::uint32_t>::max()){error="SACD track contains too many frames";return false;}
    }
    if(frameCount==0){error="SACD track has no audio frames";return false;}
    std::vector<std::uint8_t> body,version,properties,rate,channels,compression;
    number(version,0x01050000,4);chunk(body,"FVER",version);
    append(properties,"SND ");number(rate,kDsdRate,4);chunk(properties,"FS  ",rate);
    number(channels,static_cast<std::uint64_t>(metadata.channels),2);
    const std::array<const char*,6> names{"MLFT","MRGT","C   ","LFE ","LS  ","RS  "};
    for(int c=0;c<metadata.channels;++c)append(channels,metadata.channels==2?(c==0?"SLFT":"SRGT"):names[static_cast<std::size_t>(c)]);
    chunk(properties,"CHNL",channels);append(compression,metadata.dstEncoded?"DST ":"DSD ");compression.push_back(0);chunk(properties,"CMPR",compression);chunk(body,"PROP",properties);
    const auto audioBytes=payloadBytes+(metadata.dstEncoded?18U:0U);
    append(header_,"FRM8");number(header_,4+body.size()+12+audioBytes+(audioBytes%2),8);append(header_,"DSD ");header_.insert(header_.end(),body.begin(),body.end());
    append(header_,metadata.dstEncoded?"DST ":"DSD ");number(header_,audioBytes,8);
    if(metadata.dstEncoded){std::vector<std::uint8_t> frameInfo;number(frameInfo,frameCount,4);number(frameInfo,75,2);chunk(header_,"FRTE",frameInfo);}
    size_=header_.size()+payloadBytes+(audioBytes%2);
    if(!rewind()){error=error_;return false;}error.clear();return true;
}
bool SacdTrackStream::rewind(){
    if(!reader_.selectTrack(track_,error_))return false;position_=0;chunk_=header_;chunkOffset_=0;return true;
}
bool SacdTrackStream::nextChunk(){
    SacdFrame frame;bool eof=false;if(!reader_.readFrame(frame,eof,error_))return false;
    if(eof)return false;chunk_.clear();chunkOffset_=0;
    if(frame.dstEncoded)chunk(chunk_,"DSTF",frame.bytes);else chunk_=std::move(frame.bytes);return true;
}
int SacdTrackStream::read(std::uint8_t* destination,int capacity) noexcept {
    if(!destination||capacity<=0)return AVERROR(EINVAL);
    try {
        int written=0;
        while(written<capacity&&position_<size_){
            if(chunkOffset_==chunk_.size()&&!nextChunk()){
                if(!error_.empty())return AVERROR_INVALIDDATA;break;
            }
            const auto count=std::min(chunk_.size()-chunkOffset_,static_cast<std::size_t>(capacity-written));
            std::memcpy(destination+written,chunk_.data()+chunkOffset_,count);written+=static_cast<int>(count);chunkOffset_+=count;position_+=count;
        }
        return written>0?written:AVERROR_EOF;
    }catch(...){error_="SACD stream read failed";return AVERROR(EIO);}
}
std::int64_t SacdTrackStream::seek(std::int64_t offset,int whence) noexcept {
    try {
        if(whence&AVSEEK_SIZE)return static_cast<std::int64_t>(size_);
        whence&=~AVSEEK_FORCE;
        const auto base=whence==SEEK_SET?0LL:whence==SEEK_CUR?static_cast<std::int64_t>(position_):whence==SEEK_END?static_cast<std::int64_t>(size_):-1LL;
        if(base<0||offset < -base||offset>static_cast<std::int64_t>(size_)-base)return AVERROR(EINVAL);
        const auto target=static_cast<std::uint64_t>(base+offset);
        if(target<position_&&!rewind())return AVERROR(EIO);
        std::array<std::uint8_t,32768> discard{};
        while(position_<target){const auto n=read(discard.data(),static_cast<int>(std::min<std::uint64_t>(target-position_,discard.size())));if(n<0)return n;}
        return static_cast<std::int64_t>(position_);
    }catch(...){return AVERROR(EIO);}
}
int SacdTrackStream::readCallback(void* context,std::uint8_t* destination,int capacity) noexcept { return static_cast<SacdTrackStream*>(context)->read(destination,capacity); }
std::int64_t SacdTrackStream::seekCallback(void* context,std::int64_t offset,int whence) noexcept {return static_cast<SacdTrackStream*>(context)->seek(offset,whence);}
}
