#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/codec/codec_info.hpp"

using namespace iora::codecs;
using namespace std::chrono_literals;

TEST_CASE("CodecType: string conversion", "[codec][info]")
{
  CHECK(std::string(codecTypeToString(CodecType::Audio)) == "Audio");
  CHECK(std::string(codecTypeToString(CodecType::Video)) == "Video");
}

TEST_CASE("CodecFeatures: bitwise operations", "[codec][info]")
{
  auto features = CodecFeatures::Fec | CodecFeatures::Dtx | CodecFeatures::Vbr;

  CHECK(hasFeature(features, CodecFeatures::Fec));
  CHECK(hasFeature(features, CodecFeatures::Dtx));
  CHECK(hasFeature(features, CodecFeatures::Vbr));
  CHECK_FALSE(hasFeature(features, CodecFeatures::Plc));
  CHECK_FALSE(hasFeature(features, CodecFeatures::Cbr));
  CHECK_FALSE(hasFeature(features, CodecFeatures::Svc));
  CHECK_FALSE(hasFeature(features, CodecFeatures::Vad));
}

TEST_CASE("CodecFeatures: None has no flags", "[codec][info]")
{
  CHECK_FALSE(hasFeature(CodecFeatures::None, CodecFeatures::Fec));
  CHECK_FALSE(hasFeature(CodecFeatures::None, CodecFeatures::Dtx));
}

TEST_CASE("CodecFeatures: compound assignment", "[codec][info]")
{
  auto features = CodecFeatures::None;
  features |= CodecFeatures::Plc;
  features |= CodecFeatures::Vad;
  CHECK(hasFeature(features, CodecFeatures::Plc));
  CHECK(hasFeature(features, CodecFeatures::Vad));
  CHECK_FALSE(hasFeature(features, CodecFeatures::Fec));
}

TEST_CASE("CodecInfo: Opus descriptor", "[codec][info]")
{
  CodecInfo opus;
  opus.name = "opus";
  opus.type = CodecType::Audio;
  opus.mediaSubtype = "opus";
  opus.clockRate = 48000;
  opus.channels = 2;
  opus.defaultPayloadType = 111;
  opus.defaultBitrate = 64000;
  opus.frameSize = 20000us;
  opus.features = CodecFeatures::Fec | CodecFeatures::Dtx
                | CodecFeatures::Vbr | CodecFeatures::Plc;

  CHECK(opus.name == "opus");
  CHECK(opus.type == CodecType::Audio);
  CHECK(opus.clockRate == 48000);
  CHECK(opus.channels == 2);
  CHECK(opus.defaultPayloadType == 111);
  CHECK(hasFeature(opus.features, CodecFeatures::Fec));
  CHECK(hasFeature(opus.features, CodecFeatures::Plc));
}

TEST_CASE("CodecInfo: PCMU descriptor", "[codec][info]")
{
  CodecInfo pcmu;
  pcmu.name = "PCMU";
  pcmu.type = CodecType::Audio;
  pcmu.mediaSubtype = "PCMU";
  pcmu.clockRate = 8000;
  pcmu.channels = 1;
  pcmu.defaultPayloadType = 0;
  pcmu.defaultBitrate = 64000;
  pcmu.frameSize = 20000us;
  pcmu.features = CodecFeatures::Cbr;

  CHECK(pcmu.defaultPayloadType == 0);
  CHECK(pcmu.clockRate == 8000);
  CHECK(hasFeature(pcmu.features, CodecFeatures::Cbr));
}

TEST_CASE("CodecInfo: H264 descriptor", "[codec][info]")
{
  CodecInfo h264;
  h264.name = "H264";
  h264.type = CodecType::Video;
  h264.mediaSubtype = "H264";
  h264.clockRate = 90000;
  h264.channels = 0;
  h264.defaultPayloadType = 96;
  h264.features = CodecFeatures::Svc | CodecFeatures::Vbr;

  CHECK(h264.type == CodecType::Video);
  CHECK(h264.clockRate == 90000);
  CHECK(hasFeature(h264.features, CodecFeatures::Svc));
}

TEST_CASE("CodecInfo: operator==", "[codec][info]")
{
  CodecInfo a;
  a.name = "opus";
  a.clockRate = 48000;
  a.channels = 2;

  CodecInfo b;
  b.name = "opus";
  b.clockRate = 48000;
  b.channels = 2;
  b.defaultBitrate = 128000; // Different bitrate, still equal

  CHECK(a == b);
  CHECK_FALSE(a != b);
}

TEST_CASE("CodecInfo: operator== mismatch", "[codec][info]")
{
  CodecInfo opus;
  opus.name = "opus";
  opus.clockRate = 48000;
  opus.channels = 2;

  CodecInfo pcmu;
  pcmu.name = "PCMU";
  pcmu.clockRate = 8000;
  pcmu.channels = 1;

  CHECK_FALSE(opus == pcmu);
  CHECK(opus != pcmu);
}

TEST_CASE("CodecInfo: matches", "[codec][info]")
{
  CodecInfo a;
  a.name = "opus";
  a.clockRate = 48000;
  a.channels = 2;

  CodecInfo b;
  b.name = "opus";
  b.clockRate = 48000;
  b.channels = 2;
  b.features = CodecFeatures::Fec; // Different features, still matches

  CHECK(a.matches(b));
}
