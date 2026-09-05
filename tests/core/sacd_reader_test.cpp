#include "lossless/sacd_reader.hpp"
#include <QTemporaryDir>
#include <QtTest>
#include <fstream>
#include <vector>

namespace {
void be32(std::vector<unsigned char>& b, std::size_t p, std::uint32_t v)
{ for (int i=3;i>=0;--i) { b[p+static_cast<std::size_t>(i)]=static_cast<unsigned char>(v); v>>=8; } }
void tag(std::vector<unsigned char>& b,std::size_t p,const char* s)
{ std::copy(s,s+8,b.begin()+static_cast<std::ptrdiff_t>(p)); }
std::vector<unsigned char> image(int channels=2)
{
    const auto sectorCount=static_cast<unsigned>((4704*channels+1999)/2000);
    std::vector<unsigned char> b((525+sectorCount)*2048,0);
    const auto m=510U*2048U,a=520U*2048U,t=521U*2048U;
    tag(b,m,"SACDMTOC");b[m+8]=1;b[m+9]=20;be32(b,m+64,520);
    tag(b,a,channels==2?"TWOCHTOC":"MULCHTOC");b[a+8]=1;b[a+9]=20;b[a+11]=2;
    b[a+20]=4;b[a+21]=2;b[a+32]=static_cast<unsigned char>(channels);b[a+69]=1;
    be32(b,a+72,525);be32(b,a+76,524+sectorCount);
    tag(b,t,"SACDTRL1");be32(b,t+8,525);be32(b,t+8+255*4,sectorCount);
    // One full 1/75s stereo DSD frame, split over five sectors.
    std::size_t remaining=static_cast<std::size_t>(4704*channels);
    for (std::size_t s=525;s<525+sectorCount;++s) {
        const bool first=s==525;const std::size_t p=s*2048;
        const std::size_t n=std::min(remaining,static_cast<std::size_t>(2000));
        b[p]=static_cast<unsigned char>(first?0x24:0x20);
        const auto info=static_cast<std::uint16_t>((first?0x8000:0)|0x1000|n);
        b[p+1]=static_cast<unsigned char>(info>>8);b[p+2]=static_cast<unsigned char>(info);
        std::fill_n(b.begin()+static_cast<std::ptrdiff_t>(p+(first?6:3)),n,0x69);
        remaining-=n;
    }
    return b;
}
void setFrameTimecode(std::vector<unsigned char>& b,std::size_t sector,unsigned timecode)
{
    const auto p=sector*2048U;
    b[p+3]=static_cast<unsigned char>(timecode/(60U*75U));
    b[p+4]=static_cast<unsigned char>((timecode/75U)%60U);
    b[p+5]=static_cast<unsigned char>(timecode%75U);
}
std::vector<unsigned char> twoDsdFrameImage(unsigned firstTimecode,unsigned secondTimecode)
{
    auto b=image();
    constexpr std::size_t frameSectors=5;
    b.resize((525U+frameSectors*2U)*2048U,0);
    std::copy_n(b.begin()+525U*2048U,frameSectors*2048U,b.begin()+(525U+frameSectors)*2048U);
    setFrameTimecode(b,525,firstTimecode);
    setFrameTimecode(b,525+frameSectors,secondTimecode);
    be32(b,520U*2048U+76,static_cast<std::uint32_t>(524U+frameSectors*2U));
    be32(b,521U*2048U+1028,static_cast<std::uint32_t>(frameSectors*2U));
    return b;
}
std::vector<unsigned char> dstImageWithMissingMiddleSector()
{
    auto b=image();
    b.resize(528U*2048U,0);
    b[520U*2048U+21]=0;
    be32(b,520U*2048U+76,527);
    be32(b,521U*2048U+1028,3);
    const auto writePacket=[&b](std::size_t sector,bool start,unsigned timecode,unsigned declaredPackets){
        const auto p=sector*2048U;
        b[p]=static_cast<unsigned char>(start?0x25:0x21);
        const auto info=static_cast<unsigned>((start?0x9000:0x1000)|32U);
        b[p+1]=static_cast<unsigned char>(info>>8);b[p+2]=static_cast<unsigned char>(info);
        const auto payload=start?7U:3U;
        if(start){
            b[p+3]=static_cast<unsigned char>(timecode/(60U*75U));
            b[p+4]=static_cast<unsigned char>((timecode/75U)%60U);
            b[p+5]=static_cast<unsigned char>(timecode%75U);
            b[p+6]=static_cast<unsigned char>(declaredPackets<<2);
        }
        std::fill_n(b.begin()+static_cast<std::ptrdiff_t>(p+payload),32,0x5a);
    };
    // Frame 0 declares three audio packets but only its first and last packet
    // remain before frame 1 starts: the middle SACD sector is missing.
    writePacket(525,true,0,3);
    writePacket(526,false,0,0);
    writePacket(527,true,1,1);
    return b;
}
std::string save(const QString& path,const std::vector<unsigned char>& bytes)
{
    std::ofstream f(std::filesystem::path(path.toStdWString()),std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    return path.toUtf8().toStdString();
}
}
class SacdReaderTest:public QObject {
 Q_OBJECT
private slots:
 void rejectsShortAndBadMagic() {
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    QVERIFY(!r.open(save(d.filePath("bad.iso"),std::vector<unsigned char>(32)),error));QVERIFY(!error.empty());
 }
 void readsTocAndDsdFrame() {
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    QVERIFY2(r.open(save(d.filePath(QString::fromUtf8("中文.iso")),image()),error),error.c_str());
    QCOMPARE(r.tracks().size(),std::size_t(1));QCOMPARE(r.tracks()[0].channels,2);
    QVERIFY(r.selectTrack(0,error));agplayer::lossless::SacdFrame f;bool eof=false;
    QVERIFY2(r.readFrame(f,eof,error),error.c_str());QVERIFY(!eof);QCOMPARE(f.bytes.size(),std::size_t(9408));
    QVERIFY(r.readFrame(f,eof,error));QVERIFY(eof);
 }
 void rejectsTrackOutsideImage() {
    auto b=image();be32(b,521U*2048U+8+255*4,99);
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    QVERIFY(!r.open(save(d.filePath("overflow.iso"),b),error));
 }
 void multichannelOnlyArea() {
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    QVERIFY2(r.open(save(d.filePath("six-channel.iso"),image(6)),error),error.c_str());
    QCOMPARE(r.tracks().size(),std::size_t(1));QCOMPARE(r.tracks()[0].channels,6);
    QVERIFY(r.selectTrack(0,error));agplayer::lossless::SacdFrame f;bool eof=false;
    QVERIFY2(r.readFrame(f,eof,error),error.c_str());QCOMPARE(f.bytes.size(),std::size_t(28224));
 }
 void stereoAndMultichannelAreasRemainIndependent() {
    auto bytes=image();const auto multi=image(6);bytes.resize(550*2048,0);
    be32(bytes,510U*2048U+72,531);
    std::copy_n(multi.begin()+520*2048,2048,bytes.begin()+531*2048);
    std::copy_n(multi.begin()+521*2048,2048,bytes.begin()+532*2048);
    std::copy(multi.begin()+525*2048,multi.end(),bytes.begin()+535*2048);
    be32(bytes,531U*2048U+72,535);be32(bytes,531U*2048U+76,549);
    be32(bytes,532U*2048U+8,535);
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    QVERIFY2(r.open(save(d.filePath("dual-area.iso"),bytes),error),error.c_str());
    QCOMPARE(r.tracks().size(),std::size_t(2));QCOMPARE(r.tracks()[1].areaIndex,1);
    agplayer::lossless::SacdFrame f;bool eof=false;
    QVERIFY(r.selectTrack(1,error));QVERIFY(r.readFrame(f,eof,error));QCOMPARE(f.bytes.size(),std::size_t(28224));
    QVERIFY(r.selectTrack(0,error));QVERIFY(r.readFrame(f,eof,error));QCOMPARE(f.bytes.size(),std::size_t(9408));
 }
 void rejectsPacketOverflow() {
    auto b=image();b[525U*2048U+1]=0x97;b[525U*2048U+2]=0xff;
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    QVERIFY(r.open(save(d.filePath("packet.iso"),b),error));QVERIFY(r.selectTrack(0,error));
    agplayer::lossless::SacdFrame f;bool eof=false;QVERIFY(!r.readFrame(f,eof,error));
 }
 void cancellation() {
    QTemporaryDir d;std::string error;std::atomic_bool cancelled{true};agplayer::lossless::SacdReader r;
    QVERIFY(!r.open(save(d.filePath("cancel.iso"),image()),error,&cancelled));
 }
 void rawSectorEnvelopeAndTruncatedFrame() {
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    auto b=image();std::vector<unsigned char> raw(530*2064,0);
    for(std::size_t s=0;s<530;++s)
        std::copy_n(b.begin()+static_cast<std::ptrdiff_t>(s*2048),2048,raw.begin()+static_cast<std::ptrdiff_t>(s*2064+12));
    QVERIFY2(r.open(save(d.filePath("raw.iso"),raw),error),error.c_str());
    QVERIFY(r.selectTrack(0,error));agplayer::lossless::SacdFrame f;bool eof=false;
    QVERIFY(r.readFrame(f,eof,error));QCOMPARE(f.bytes.size(),std::size_t(9408));
    // A valid directory containing too few bytes must fail, not produce silence.
    b[529U*2048U+2]-=1;
    QVERIFY(r.open(save(d.filePath("short-frame.iso"),b),error));QVERIFY(r.selectTrack(0,error));
    QVERIFY(!r.readFrame(f,eof,error));
 }
 void rejectsMissingFrameFromTimecodeGap() {
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    QVERIFY2(r.open(save(d.filePath("missing-frame.iso"),twoDsdFrameImage(0,2)),error),error.c_str());
    QVERIFY(r.selectTrack(0,error));agplayer::lossless::SacdFrame f;bool eof=false;
    QVERIFY(!r.readFrame(f,eof,error));
 }
 void rejectsNonConsecutiveFrameTimecode() {
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    QVERIFY2(r.open(save(d.filePath("repeated-timecode.iso"),twoDsdFrameImage(12,12)),error),error.c_str());
    QVERIFY(r.selectTrack(0,error));agplayer::lossless::SacdFrame f;bool eof=false;
    QVERIFY(!r.readFrame(f,eof,error));
 }
 void rejectsDstFrameWithMissingMiddleSector() {
    QTemporaryDir d;std::string error;agplayer::lossless::SacdReader r;
    QVERIFY2(r.open(save(d.filePath("missing-dst-sector.iso"),dstImageWithMissingMiddleSector()),error),error.c_str());
    QVERIFY(r.selectTrack(0,error));agplayer::lossless::SacdFrame f;bool eof=false;
    QVERIFY(!r.readFrame(f,eof,error));
 }
 void officialDstFramesRoundTrip() {
    const auto fixtureRoot=qEnvironmentVariable("AGPLAYER_SACD_QA_OUTPUT");
    QFile file(QDir(fixtureRoot).filePath("fate-dst-64fs44-2ch.dff"));
    if(fixtureRoot.isEmpty()||!file.open(QIODevice::ReadOnly)) QSKIP("Optional FFmpeg FATE DST fixture not supplied");
    const auto input=file.readAll();
    const auto length=[&input](qsizetype p){
        quint64 value=0;for(int n=0;n<8;++n)value=(value<<8)|static_cast<unsigned char>(input[p+n]);return value;
    };
    std::vector<QByteArray> frames;
    for(qsizetype p=16;p+12<=input.size();){
        const auto size=length(p+4);QVERIFY(size<=static_cast<quint64>(input.size()-p-12));
        if(input.mid(p,4)=="DST "){
            const auto end=p+12+static_cast<qsizetype>(size);
            for(qsizetype q=p+12;q+12<=end;){
                const auto bytes=length(q+4);QVERIFY(bytes<=static_cast<quint64>(end-q-12));
                if(input.mid(q,4)=="DSTF")frames.push_back(input.mid(q+12,static_cast<qsizetype>(bytes)));
                q+=12+static_cast<qsizetype>(bytes)+(bytes%2);
            }
        }
        p+=12+static_cast<qsizetype>(size)+(size%2);
    }
    QCOMPARE(frames.size(),std::size_t(10));
    auto b=image();b.resize(525*2048);b[520U*2048U+21]=0;
    std::size_t sector=525;
    for(std::size_t f=0;f<frames.size();++f){
        const auto& frame=frames[f];
        const auto declaredPackets=static_cast<unsigned>((frame.size()+1999)/2000);
        QVERIFY(declaredPackets>0&&declaredPackets<=31);
        for(qsizetype offset=0;offset<frame.size();){
            const bool first=offset==0;const auto n=std::min<qsizetype>(2000,frame.size()-offset);
            b.resize((sector+1)*2048,0);const auto p=sector++*2048;
            b[p]=static_cast<unsigned char>(first?0x25:0x21);
            const auto info=static_cast<unsigned>((first?0x9000:0x1000)|n);
            b[p+1]=static_cast<unsigned char>(info>>8);b[p+2]=static_cast<unsigned char>(info);
            if(first){b[p+5]=static_cast<unsigned char>(f);b[p+6]=static_cast<unsigned char>(declaredPackets<<2);}
            std::copy_n(frame.constData()+offset,n,b.begin()+static_cast<std::ptrdiff_t>(p+(first?7:3)));
            offset+=n;
        }
    }
    be32(b,520U*2048U+76,static_cast<std::uint32_t>(sector-1));
    be32(b,521U*2048U+1028,static_cast<std::uint32_t>(sector-525));
    const auto path=save(QDir(fixtureRoot).filePath("fate-dst-synthetic.iso"),b);
    agplayer::lossless::SacdReader reader;std::string error;
    QVERIFY2(reader.open(path,error),error.c_str());QVERIFY(reader.selectTrack(0,error));
    for(const auto& expected:frames){
        agplayer::lossless::SacdFrame actual;bool eof=false;
        QVERIFY2(reader.readFrame(actual,eof,error),error.c_str());QVERIFY(!eof);QVERIFY(actual.dstEncoded);
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(actual.bytes.data()),static_cast<qsizetype>(actual.bytes.size())),expected);
    }
    agplayer::lossless::SacdTrackStream stream;QVERIFY2(stream.open(path,0,error),error.c_str());
    std::vector<std::uint8_t> output(static_cast<std::size_t>(stream.seek(0,0x10000)));
    QCOMPARE(stream.read(output.data(),static_cast<int>(output.size())),static_cast<int>(output.size()));
    save(QDir(fixtureRoot).filePath("fate-dst-roundtrip.dff"),output);
 }
 void virtualDffReadSeekAndSize() {
    QTemporaryDir d;std::string error;agplayer::lossless::SacdTrackStream stream;
    QVERIFY2(stream.open(save(d.filePath("virtual.iso"),image()),0,error),error.c_str());
    const auto size=stream.seek(0,0x10000);QVERIFY(size>9408);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    QCOMPARE(stream.read(bytes.data(),static_cast<int>(bytes.size())),static_cast<int>(size));
    QCOMPARE(std::string(reinterpret_cast<const char*>(bytes.data()),4),std::string("FRM8"));
    QCOMPARE(stream.seek(0,SEEK_SET),std::int64_t(0));
    std::array<std::uint8_t,4> first{};QCOMPARE(stream.read(first.data(),4),4);
    QVERIFY(std::equal(first.begin(),first.end(),bytes.begin()));
    QVERIFY(stream.seek(size+1,SEEK_SET)<0);
    const auto fixtureRoot=qEnvironmentVariable("AGPLAYER_SACD_QA_OUTPUT");
    if(!fixtureRoot.isEmpty()){
        QDir().mkpath(fixtureRoot);
        save(QDir(fixtureRoot).filePath("generated-silence.dff"),bytes);
        save(QDir(fixtureRoot).filePath("generated-silence.iso"),image());
    }
 }
};
QTEST_GUILESS_MAIN(SacdReaderTest)
#include "sacd_reader_test.moc"
