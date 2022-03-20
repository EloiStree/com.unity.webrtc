#pragma once

namespace unity
{
namespace webrtc
{
    using namespace ::webrtc;

    class IGraphicsDevice;
    class ProfilerMarkerFactory;
    class UnityVideoEncoderFactory : public VideoEncoderFactory
    {
    public:
        //VideoEncoderFactory
        // Returns a list of supported video formats in order of preference, to use
        // for signaling etc.
        virtual std::vector<SdpVideoFormat> GetSupportedFormats() const override;
        // Returns information about how this format will be encoded. The specified
        // format must be one of the supported formats by this factory.
        virtual CodecInfo QueryVideoEncoder(const SdpVideoFormat& format) const override;
        // Creates a VideoEncoder for the specified format.
        virtual std::unique_ptr<VideoEncoder> CreateVideoEncoder(const SdpVideoFormat& format) override;

        UnityVideoEncoderFactory(IGraphicsDevice* gfxDevice, ProfilerMarkerFactory* profiler);
        ~UnityVideoEncoderFactory();

    private:
        ProfilerMarkerFactory* profiler_;
        const std::unique_ptr<VideoEncoderFactory> internal_encoder_factory_;
        const std::unique_ptr<VideoEncoderFactory> native_encoder_factory_;
    };
}
}
