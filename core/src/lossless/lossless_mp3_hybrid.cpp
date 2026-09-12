#include "lossless_mp3_hybrid.hpp"
#include "lossless_mp3_hybrid_detail.hpp"
#include "lossless_mp3_hybrid_window.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
namespace agplayer::lossless {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t hop = 576, framesPerOffset = 8, blockCount = 4;
constexpr std::size_t history = 511, lastBandTime = hop*framesPerOffset+35*32;
constexpr std::size_t excerptSize = history+lastBandTime+1;
constexpr double floorCoefficient = 1e-10;
struct BankTables {
    std::array<double,512> window{};
    std::array<std::array<double,64>,32> modulation{};
    BankTables() {
        for(std::size_t i=0;i<257;++i){
            const double v=static_cast<double>(mp3_hybrid_detail::mpegEncodingHalfWindow[i])/65536.0;
            window[i]=v;
            if(i!=0)window[512-i]=(i%64!=0)?-v:v;
        }
        for(std::size_t b=0;b<32;++b)for(std::size_t n=0;n<64;++n)
            modulation[b][n]=std::cos(pi/64*(2*static_cast<double>(b)+1)*(static_cast<double>(n)-16));
    }
};
const BankTables& bankTables(){static const BankTables tables;return tables;}
}
namespace mp3_hybrid_detail {
void analysisSubbands(const double* latest, std::array<double,32>& output) noexcept {
    const auto& tables=bankTables();std::array<double,64> folded{};
    // The cosine index is modulo 64: introducing an extra sign every 64 taps
    // would contradict the encoding window's existing sign convention.
    for(std::size_t i=0;i<64;++i)for(std::size_t j=0;j<8;++j){
        const auto tap=i+j*64;
        folded[i]+=latest[-static_cast<std::ptrdiff_t>(tap)]*tables.window[tap];
    }
    for(std::size_t b=0;b<32;++b){
        double sum=0;for(std::size_t i=0;i<64;++i)sum+=folded[i]*tables.modulation[b][i];output[b]=sum;
    }
}
void aliasReduction(double* bands) noexcept {
    // Standard adjacent-band orthogonal rotation, independently expressed.
    static const auto cs=[](){std::array<double,8> a{};constexpr std::array<double,8> c{{-.6,-.535,-.33,-.185,-.095,-.041,-.0142,-.0037}};for(std::size_t i=0;i<8;++i)a[i]=1/std::sqrt(1+c[i]*c[i]);return a;}();
    constexpr std::array<double,8> ci{{-.6,-.535,-.33,-.185,-.095,-.041,-.0142,-.0037}};
    for(std::size_t b=1;b<32;++b)for(std::size_t k=0;k<8;++k){
        const auto up=b*18-1-k,down=b*18+k;
        const double u=bands[up],d=bands[down],ca=ci[k]*cs[k];
        bands[up]=d*ca+u*cs[k];bands[down]=d*cs[k]-u*ca;
    }
}
bool accumulateLogEnergy(const double* coefficients,std::size_t count,double& energy) noexcept {
    for(;count>=4;coefficients+=4,count-=4){
        const double v0=coefficients[0],v1=coefficients[1],v2=coefficients[2],v3=coefficients[3];
        if(!std::isfinite(v0) || !std::isfinite(v1) || !std::isfinite(v2) || !std::isfinite(v3))return false;
        const double a=std::max(floorCoefficient,std::abs(v0)),b=std::max(floorCoefficient,std::abs(v1)),
            c=std::max(floorCoefficient,std::abs(v2)),d=std::max(floorCoefficient,std::abs(v3));
        // Four individually floored magnitudes have product >= 1e-40: no
        // underflow. Extreme finite coefficients can overflow the product,
        // so retain the scalar formula as a fallback instead of rejecting them.
        const double product=((a*b)*c)*d;
        if(std::isfinite(product) && product>=1e-40)energy+=20*std::log10(product);
        else {energy+=20*std::log10(a);energy+=20*std::log10(b);energy+=20*std::log10(c);energy+=20*std::log10(d);}
    }
    for(std::size_t i=0;i<count;++i){
        const double value=coefficients[i];if(!std::isfinite(value))return false;
        energy+=20*std::log10(std::max(floorCoefficient,std::abs(value)));
    }
    return true;
}
LongMdct::LongMdct(){
    for(std::size_t n=0;n<36;++n){
        const double x=static_cast<double>(n);window_[n]=std::sin(pi/36*(x+.5));
        pre_[n]={std::cos(-pi*x/36),std::sin(-pi*x/36)};
    }
    for(std::size_t k=0;k<18;++k){const double a=-2*pi/36*(.5+9)*(static_cast<double>(k)+.5);post_[k]={std::cos(a),std::sin(a)};}
    if(av_tx_init(&context_,&transform_,AV_TX_DOUBLE_FFT,0,36,nullptr,AV_TX_UNALIGNED)<0){
        av_tx_uninit(&context_);throw std::runtime_error("MP3 hybrid FFT initialization failed");
    }
}
LongMdct::~LongMdct(){av_tx_uninit(&context_);}
void LongMdct::transform(const double* samples,double* coefficients){
    for(std::size_t n=0;n<36;++n){const double v=samples[n]*window_[n];input_[n]={v*pre_[n].re,v*pre_[n].im};}
    transform_(context_,output_.data(),input_.data(),sizeof(AVComplexDouble));
    for(std::size_t k=0;k<18;++k)coefficients[k]=output_[k].re*post_[k].re-output_[k].im*post_[k].im;
}
}
struct Mp3HybridProbe::Impl {
    struct Measurement {
        std::size_t count=0;
        double real=0,imaginary=0,peaks=0,z=0;
        std::array<std::size_t,blockCount> phases{};
        MdctFrameEvidence result() const noexcept {
            MdctFrameEvidence out{};out.window="mp3_hybrid_36";
            if(count==0)return out;
            const double magnitude=std::hypot(real,imaginary);
            out.activeBlocks=count;out.coherentPeakDb=magnitude/static_cast<double>(count);
            out.phaseConcentration=peaks>0?std::clamp(magnitude/peaks,0.0,1.0):0;
            out.meanPeakZ=z/static_cast<double>(count);
            for(std::size_t i=0;i<count;++i){
                std::size_t aligned=0;
                for(std::size_t j=0;j<count;++j){const auto d=phases[i]>phases[j]?phases[i]-phases[j]:phases[j]-phases[i];if(std::min(d,hop-d)<=2)++aligned;}
                out.alignedBlocks=std::max(out.alignedBlocks,aligned);
            }
            return out;
        }
    };
    const std::size_t channels;
    std::array<std::uint64_t,blockCount> starts{};
    std::size_t planned=0,current=0;
    std::uint64_t cursor=0;
    std::vector<double> captured;
    std::array<std::vector<double>,blockCount> excerpts;
    std::array<std::vector<double>,blockCount> alternateExcerpts;
    bool refined=false,alternateRefined=false;
    mp3_hybrid_detail::LongMdct transform;
    Measurement primary,alternate;
    Impl(int channelCount,std::uint64_t total):channels(static_cast<std::size_t>(channelCount)){
        planned=static_cast<std::size_t>(std::min<std::uint64_t>(blockCount,total/excerptSize));
        if(planned==blockCount && total>=30816){
            // Same bases as frozen Python v2 for 3-second fixtures; long files
            // disperse anchors across their full length. Store preceding history.
            const auto span=total-12096;
            for(std::size_t i=0;i<blockCount;++i)
                starts[i]=4096-history+(span/3)*i+(span%3)*i/3;
        }else for(std::size_t i=0;i<planned;++i)starts[i]=i*excerptSize;
        captured.reserve(excerptSize*channels);
    }
    void capture(){
        std::size_t selected=0;double maximum=0;
        std::array<double,2> stereoPower{};
        for(std::size_t ch=0;ch<channels;++ch){
            double power=0;
            for(std::size_t n=0;n<excerptSize;++n){
                const double v=captured[n*channels+ch];if(!std::isfinite(v))return;power+=v*v;
            }
            if(!std::isfinite(power))return;
            if(channels==2)stereoPower[ch]=power;
            if(power>maximum){maximum=power;selected=ch;}
        }
        if(maximum/static_cast<double>(excerptSize)<1e-12)return;
        auto& mono=excerpts[current];mono.resize(excerptSize);
        for(std::size_t i=0;i<excerptSize;++i)mono[i]=captured[i*channels+selected];
        if(channels==2 && stereoPower[1-selected]/static_cast<double>(excerptSize)>=1e-12){
            const auto otherChannel=1-selected;
            auto& other=alternateExcerpts[current];other.resize(excerptSize);
            for(std::size_t i=0;i<excerptSize;++i)other[i]=captured[i*channels+otherChannel];
        }
    }
    void measure(std::size_t block,const std::vector<double>& mono,Measurement& measured,const std::atomic_bool& cancel){
        if(mono.empty())return;
        std::vector<std::array<double,32>> subbands(lastBandTime+1);
        for(std::size_t t=0;t<=lastBandTime;++t){
            if((t%64)==0 && cancel.load(std::memory_order_relaxed))return;
            mp3_hybrid_detail::analysisSubbands(mono.data()+history+t,subbands[t]);
            for(const auto v:subbands[t])if(!std::isfinite(v))return;
        }
        std::array<double,hop+1> energies{};
        std::array<double,36> samples{};std::array<double,576> coefficients{};
        // Include offset 576 so every phase has a forward difference. Python
        // v2 stopped at 575 and consequently omitted the final boundary.
        for(std::size_t shift=0;shift<=hop;++shift){
            if(cancel.load(std::memory_order_relaxed))return;
            double energy=0;
            for(std::size_t f=0;f<framesPerOffset;++f){
                for(std::size_t b=0;b<32;++b){
                    for(std::size_t n=0;n<36;++n){
                        const double v=subbands[shift+f*hop+n*32][b];
                        samples[n]=((n&1U)!=0 && (b&1U)!=0)?-v:v;
                    }
                    transform.transform(samples.data(),coefficients.data()+b*18);
                }
                mp3_hybrid_detail::aliasReduction(coefficients.data());
                if(!mp3_hybrid_detail::accumulateLogEnergy(coefficients.data(),coefficients.size(),energy))return;
            }
            energies[shift]=energy/static_cast<double>(framesPerOffset*576);
        }
        if(cancel.load(std::memory_order_relaxed))return;
        double peak=0,sum=0,squares=0;std::size_t offset=0;
        for(std::size_t i=0;i<hop;++i){const double d=energies[i+1]-energies[i];sum+=d;squares+=d*d;if(d>peak){peak=d;offset=i;}}
        const double mean=sum/hop,deviation=std::sqrt(std::max(0.0,squares/hop-mean*mean));
        const auto phase=static_cast<std::size_t>((starts[block]+history+offset)%hop);
        const double angle=2*pi*static_cast<double>(phase)/hop;
        measured.real+=peak*std::cos(angle);measured.imaginary+=peak*std::sin(angle);
        measured.peaks+=peak;measured.z+=peak/std::max(deviation,1e-12);measured.phases[measured.count++]=phase;
    }
};
Mp3HybridProbe::Mp3HybridProbe(int rate,int channels,std::uint64_t total){
    if(rate>=32000 && rate<=48000 && channels>0 && channels<=64 && total>=excerptSize)
        impl_=std::make_unique<Impl>(channels,total);
}
Mp3HybridProbe::~Mp3HybridProbe()=default;
void Mp3HybridProbe::consume(const double* samples,std::size_t frames,const std::atomic_bool& cancel){
    if(!impl_ || impl_->refined || impl_->alternateRefined || !samples || cancel.load(std::memory_order_relaxed))return;
    auto& p=*impl_;
    if(frames>std::numeric_limits<std::uint64_t>::max()-p.cursor
       || frames>static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())/p.channels)
        throw std::overflow_error("MP3 hybrid input frame count overflow");
    const auto end=p.cursor+frames;
    while(p.current<p.planned && p.starts[p.current]<end){
        const auto begin=std::max(p.cursor,p.starts[p.current]);
        const auto stop=std::min(end,p.starts[p.current]+excerptSize);
        if(stop>begin)p.captured.insert(p.captured.end(),samples+static_cast<std::size_t>(begin-p.cursor)*p.channels,samples+static_cast<std::size_t>(stop-p.cursor)*p.channels);
        if(p.captured.size()!=excerptSize*p.channels)break;
        p.capture();p.captured.clear();++p.current;
        if(cancel.load(std::memory_order_relaxed))break;
    }
    p.cursor=end;
}
void Mp3HybridProbe::refine(const std::atomic_bool& cancel){
    if(!impl_ || impl_->refined || cancel.load(std::memory_order_relaxed))return;
    auto& p=*impl_;p.refined=true;
    for(std::size_t block=0;block<p.current;++block){
        if(cancel.load(std::memory_order_relaxed))break;
        p.measure(block,p.excerpts[block],p.primary,cancel);
    }
}
void Mp3HybridProbe::refineAlternate(const std::atomic_bool& cancel){
    if(!impl_ || impl_->channels!=2 || impl_->alternateRefined || cancel.load(std::memory_order_relaxed))return;
    auto& p=*impl_;p.alternateRefined=true;
    for(std::size_t block=0;block<p.current;++block){
        if(cancel.load(std::memory_order_relaxed))break;
        p.measure(block,p.alternateExcerpts[block],p.alternate,cancel);
    }
}
MdctFrameEvidence Mp3HybridProbe::result() const noexcept {
    return impl_?impl_->primary.result():Impl::Measurement{}.result();
}
MdctFrameEvidence Mp3HybridProbe::alternateResult() const noexcept {
    return impl_?impl_->alternate.result():Impl::Measurement{}.result();
}
}
