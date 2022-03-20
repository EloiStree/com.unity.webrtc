#include "pch.h"

#include "GraphicsDevice/GraphicsUtility.h"
#include "ProfilerMarkerFactory.h"
#include "ScopedProfiler.h"
#include "UnityVideoEncoderFactory.h"

#if CUDA_PLATFORM
#include "Codec/NvCodec/NvCodec.h"
#include <cuda.h>
#endif

#if UNITY_OSX || UNITY_IOS
#import "sdk/objc/components/video_codec/RTCDefaultVideoEncoderFactory.h"
#import "sdk/objc/native/api/video_encoder_factory.h"
#elif UNITY_ANDROID
#include "Codec/AndroidCodec/android_codec_factory_helper.h"
#endif

namespace unity
{
namespace webrtc
{
    using namespace ::webrtc::H264;

    class UnityVideoEncoder : public VideoEncoder
    {
    public:
        UnityVideoEncoder(std::unique_ptr<VideoEncoder> encoder, ProfilerMarkerFactory* profiler)
            : encoder_(std::move(encoder))
            , profiler_(profiler)
            , marker_(nullptr)
            , profilerThread_(nullptr)
        {
            if (profiler)
                marker_ = profiler->CreateMarker(
                    "UnityVideoEncoder.Encode",
                    kUnityProfilerCategoryOther,
                    kUnityProfilerMarkerFlagDefault,
                    0);
        }
        ~UnityVideoEncoder() override { }

        void SetFecControllerOverride(FecControllerOverride* fec_controller_override) override
        {
            encoder_->SetFecControllerOverride(fec_controller_override);
        }
        int32_t InitEncode(const VideoCodec* codec_settings, int32_t number_of_cores, size_t max_payload_size) override
        {
            return encoder_->InitEncode(codec_settings, number_of_cores, max_payload_size);
        }
        int InitEncode(const VideoCodec* codec_settings, const VideoEncoder::Settings& settings) override
        {
            return encoder_->InitEncode(codec_settings, settings);
        }
        int32_t RegisterEncodeCompleteCallback(EncodedImageCallback* callback) override
        {
            return encoder_->RegisterEncodeCompleteCallback(callback);
        }
        int32_t Release() override { return encoder_->Release(); }
        int32_t Encode(const VideoFrame& frame, const std::vector<VideoFrameType>* frame_types) override
        {
            if (!profilerThread_)
                profilerThread_ = profiler_->CreateScopedProfilerThread("WebRTC", "VideoEncoder");

            int32_t result;
            {
                std::unique_ptr<const ScopedProfiler> profiler;
                if (profiler_)
                    profiler = profiler_->CreateScopedProfiler(*marker_);
                result = encoder_->Encode(frame, frame_types);
            }
            return result;
        }
        void SetRates(const RateControlParameters& parameters) override { encoder_->SetRates(parameters); }
        void OnPacketLossRateUpdate(float packet_loss_rate) override
        {
            encoder_->OnPacketLossRateUpdate(packet_loss_rate);
        }
        void OnRttUpdate(int64_t rtt_ms) override { encoder_->OnRttUpdate(rtt_ms); }
        void OnLossNotification(const LossNotification& loss_notification) override
        {
            encoder_->OnLossNotification(loss_notification);
        }
        EncoderInfo GetEncoderInfo() const override { return encoder_->GetEncoderInfo(); }

    private:
        std::unique_ptr<VideoEncoder> encoder_;
        ProfilerMarkerFactory* profiler_;
        const UnityProfilerMarkerDesc* marker_;
        std::unique_ptr<const ScopedProfilerThread> profilerThread_;
    };

    webrtc::VideoEncoderFactory* CreateNativeEncoderFactory(IGraphicsDevice* gfxDevice)
    {
#if UNITY_OSX || UNITY_IOS
        return webrtc::ObjCToNativeVideoEncoderFactory([[RTCDefaultVideoEncoderFactory alloc] init]).release();
#elif UNITY_ANDROID
        // todo(kazuki)::workaround
        // return CreateAndroidEncoderFactory().release();
        return nullptr;
#elif CUDA_PLATFORM
        CUcontext context = gfxDevice->GetCUcontext();
        NV_ENC_BUFFER_FORMAT format = gfxDevice->GetEncodeBufferFormat();
        return new NvEncoderFactory(context, format);
#endif
        return nullptr;
    }

    UnityVideoEncoderFactory::UnityVideoEncoderFactory(IGraphicsDevice* gfxDevice, ProfilerMarkerFactory* profiler)
        : profiler_(profiler)
        , internal_encoder_factory_(new webrtc::InternalEncoderFactory())
        , native_encoder_factory_(CreateNativeEncoderFactory(gfxDevice))
    {
    }

    UnityVideoEncoderFactory::~UnityVideoEncoderFactory() = default;

    std::vector<webrtc::SdpVideoFormat> UnityVideoEncoderFactory::GetSupportedFormats() const
    {
        std::vector<SdpVideoFormat> supported_codecs;

        for (const webrtc::SdpVideoFormat& format : internal_encoder_factory_->GetSupportedFormats())
            supported_codecs.push_back(format);
        if (native_encoder_factory_)
        {
            for (const webrtc::SdpVideoFormat& format : native_encoder_factory_->GetSupportedFormats())
                supported_codecs.push_back(format);
        }

        // Set video codec order: default video codec is VP8
        auto findIndex = [&](webrtc::SdpVideoFormat& format) -> long
        {
            const std::string sortOrder[4] = { "VP8", "VP9", "H264", "AV1X" };
            auto it = std::find(std::begin(sortOrder), std::end(sortOrder), format.name);
            if (it == std::end(sortOrder))
                return LONG_MAX;
            return std::distance(std::begin(sortOrder), it);
        };
        std::sort(
            supported_codecs.begin(),
            supported_codecs.end(),
            [&](webrtc::SdpVideoFormat& x, webrtc::SdpVideoFormat& y) -> int { return (findIndex(x) < findIndex(y)); });
        return supported_codecs;
    }

    webrtc::VideoEncoderFactory::CodecInfo
    UnityVideoEncoderFactory::QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const
    {
        if (native_encoder_factory_ && format.IsCodecInList(native_encoder_factory_->GetSupportedFormats()))
        {
            return native_encoder_factory_->QueryVideoEncoder(format);
        }
        RTC_DCHECK(format.IsCodecInList(internal_encoder_factory_->GetSupportedFormats()));
        return internal_encoder_factory_->QueryVideoEncoder(format);
    }

    std::unique_ptr<webrtc::VideoEncoder>
    UnityVideoEncoderFactory::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)
    {
        std::unique_ptr<webrtc::VideoEncoder> encoder;
        if (native_encoder_factory_ && format.IsCodecInList(native_encoder_factory_->GetSupportedFormats()))
        {
            encoder = native_encoder_factory_->CreateVideoEncoder(format);
        }
        else
        {
            encoder = internal_encoder_factory_->CreateVideoEncoder(format);
        }
        if (!profiler_)
            return encoder;

        // Use Unity Profiler for measuring encoding process.
        return std::make_unique<UnityVideoEncoder>(std::move(encoder), profiler_);
    }
}
}
