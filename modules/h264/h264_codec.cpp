#include "h264_codec.hpp"

#include <wels/codec_api.h>
#include <wels/codec_app_def.h>

#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

H264Codec::H264Codec(CodecInfo info, H264Mode mode,
                     std::uint32_t width, std::uint32_t height,
                     std::uint32_t bitrate, float framerate,
                     std::uint32_t profileIdc)
  : _info(std::move(info))
  , _mode(mode)
  , _width(width)
  , _height(height)
  , _bitrate(bitrate)
  , _framerate(framerate)
  , _profileIdc(profileIdc)
{
  if (_width == 0 || _height == 0)
  {
    throw std::runtime_error("H264Codec: width and height must be non-zero");
  }

  if ((_width % 2) != 0 || (_height % 2) != 0)
  {
    throw std::runtime_error("H264Codec: width and height must be even for I420");
  }

  if (_mode == H264Mode::Encoder)
  {
    ISVCEncoder* enc = nullptr;
    if (WelsCreateSVCEncoder(&enc) != 0 || enc == nullptr)
    {
      throw std::runtime_error("WelsCreateSVCEncoder failed");
    }

    SEncParamExt param;
    std::memset(&param, 0, sizeof(param));
    enc->GetDefaultParams(&param);

    param.iUsageType = CAMERA_VIDEO_REAL_TIME;
    param.iPicWidth = static_cast<int>(_width);
    param.iPicHeight = static_cast<int>(_height);
    param.iTargetBitrate = static_cast<int>(_bitrate);
    param.iRCMode = RC_BITRATE_MODE;
    param.fMaxFrameRate = _framerate;
    param.iTemporalLayerNum = 1;
    param.iSpatialLayerNum = 1;
    param.uiIntraPeriod = static_cast<unsigned int>(_framerate); // ~1 IDR/sec
    param.iMultipleThreadIdc = 1;
    param.bEnableFrameSkip = true;

    param.sSpatialLayers[0].iVideoWidth = static_cast<int>(_width);
    param.sSpatialLayers[0].iVideoHeight = static_cast<int>(_height);
    param.sSpatialLayers[0].fFrameRate = _framerate;
    param.sSpatialLayers[0].iSpatialBitrate = static_cast<int>(_bitrate);
    param.sSpatialLayers[0].iMaxSpatialBitrate = static_cast<int>(_bitrate);
    // Map profileIdc to OpenH264 enum
    EProfileIdc profile = PRO_BASELINE;
    if (_profileIdc == 77)
    {
      profile = PRO_MAIN;
    }
    else if (_profileIdc == 100)
    {
      profile = PRO_HIGH;
    }
    param.sSpatialLayers[0].uiProfileIdc = profile;

    int rv = enc->InitializeExt(&param);
    if (rv != 0)
    {
      WelsDestroySVCEncoder(enc);
      throw std::runtime_error("ISVCEncoder::InitializeExt failed");
    }

    int videoFormat = videoFormatI420;
    enc->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);

    // Suppress log output
    int logLevel = WELS_LOG_QUIET;
    enc->SetOption(ENCODER_OPTION_TRACE_LEVEL, &logLevel);

    _encoder = enc;
  }
  else
  {
    ISVCDecoder* dec = nullptr;
    if (WelsCreateDecoder(&dec) != 0 || dec == nullptr)
    {
      throw std::runtime_error("WelsCreateDecoder failed");
    }

    SDecodingParam param;
    std::memset(&param, 0, sizeof(param));
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    param.eEcActiveIdc = ERROR_CON_SLICE_COPY_CROSS_IDR;

    long rv = dec->Initialize(&param);
    if (rv != 0)
    {
      WelsDestroyDecoder(dec);
      throw std::runtime_error("ISVCDecoder::Initialize failed");
    }

    // Suppress log output
    int logLevel = WELS_LOG_QUIET;
    dec->SetOption(DECODER_OPTION_TRACE_LEVEL, &logLevel);

    _decoder = dec;
  }
}

H264Codec::~H264Codec()
{
  if (_encoder != nullptr)
  {
    auto* enc = _encoder;
    enc->Uninitialize();
    WelsDestroySVCEncoder(enc);
  }
  if (_decoder != nullptr)
  {
    auto* dec = _decoder;
    dec->Uninitialize();
    WelsDestroyDecoder(dec);
  }
}

const CodecInfo& H264Codec::info() const
{
  return _info;
}

std::shared_ptr<MediaBuffer> H264Codec::encode(const MediaBuffer& input)
{
  if (_encoder == nullptr || _mode != H264Mode::Encoder)
  {
    return nullptr;
  }

  // Validate minimum input size: packed I420 = width * height * 3/2
  std::size_t expectedBytes = static_cast<std::size_t>(_width) * _height * 3 / 2;
  if (input.size() < expectedBytes)
  {
    return nullptr;
  }

  auto* enc = _encoder;

  // Force IDR if requested
  if (_forceIdr)
  {
    enc->ForceIntraFrame(true);
    _forceIdr = false;
  }

  SSourcePicture srcPic;
  std::memset(&srcPic, 0, sizeof(srcPic));
  srcPic.iColorFormat = videoFormatI420;
  srcPic.iPicWidth = static_cast<int>(_width);
  srcPic.iPicHeight = static_cast<int>(_height);
  srcPic.iStride[0] = static_cast<int>(_width);
  srcPic.iStride[1] = static_cast<int>(_width / 2);
  srcPic.iStride[2] = static_cast<int>(_width / 2);

  // OpenH264 SSourcePicture::pData is unsigned char* (not const) but EncodeFrame
  // does not modify the source data. const_cast is safe here.
  auto* yPlane = const_cast<unsigned char*>(input.data());
  srcPic.pData[0] = yPlane;
  srcPic.pData[1] = yPlane + static_cast<std::size_t>(_width) * _height;
  srcPic.pData[2] = srcPic.pData[1] + static_cast<std::size_t>(_width / 2) * (_height / 2);

  SFrameBSInfo bsInfo;
  std::memset(&bsInfo, 0, sizeof(bsInfo));

  int rv = enc->EncodeFrame(&srcPic, &bsInfo);
  if (rv != 0)
  {
    return nullptr;
  }

  // Encoder may skip the frame for rate control
  if (bsInfo.eFrameType == videoFrameTypeSkip || bsInfo.eFrameType == videoFrameTypeInvalid)
  {
    return nullptr;
  }

  if (bsInfo.iFrameSizeInBytes <= 0)
  {
    return nullptr;
  }

  // NAL layers are contiguous starting from sLayerInfo[0].pBsBuf
  auto output = MediaBuffer::create(static_cast<std::size_t>(bsInfo.iFrameSizeInBytes));
  output->copyMetadataFrom(input);

  // Copy all NAL layers
  int offset = 0;
  for (int i = 0; i < bsInfo.iLayerNum; ++i)
  {
    SLayerBSInfo& layer = bsInfo.sLayerInfo[i];
    int layerSize = 0;
    for (int j = 0; j < layer.iNalCount; ++j)
    {
      layerSize += layer.pNalLengthInByte[j];
    }
    std::memcpy(output->data() + offset, layer.pBsBuf, static_cast<std::size_t>(layerSize));
    offset += layerSize;
  }

  output->setSize(static_cast<std::size_t>(offset));
  return output;
}

std::shared_ptr<MediaBuffer> H264Codec::decode(const MediaBuffer& input)
{
  if (_decoder == nullptr || _mode != H264Mode::Decoder)
  {
    return nullptr;
  }

  if (input.size() == 0)
  {
    return nullptr;
  }

  auto* dec = _decoder;

  unsigned char* ppDst[3] = {nullptr, nullptr, nullptr};
  SBufferInfo sDstBufInfo;
  std::memset(&sDstBufInfo, 0, sizeof(sDstBufInfo));

  DECODING_STATE rv = dec->DecodeFrameNoDelay(
    input.data(),
    static_cast<int>(input.size()),
    ppDst,
    &sDstBufInfo);

  // DECODING_STATE is a bitfield — composite values like dsDataErrorConcealed | dsRefLost
  // are valid and produce output. Reject only logic-level errors (0x1000+).
  if (rv & (dsInvalidArgument | dsInitialOptExpected | dsOutOfMemory | dsDstBufNeedExpan))
  {
    return nullptr;
  }

  if (sDstBufInfo.iBufferStatus != 1)
  {
    return nullptr;
  }

  if (ppDst[0] == nullptr || ppDst[1] == nullptr || ppDst[2] == nullptr)
  {
    return nullptr;
  }

  int outWidth = sDstBufInfo.UsrData.sSystemBuffer.iWidth;
  int outHeight = sDstBufInfo.UsrData.sSystemBuffer.iHeight;
  int yStride = sDstBufInfo.UsrData.sSystemBuffer.iStride[0];
  int uvStride = sDstBufInfo.UsrData.sSystemBuffer.iStride[1];

  if (outWidth <= 0 || outHeight <= 0)
  {
    return nullptr;
  }

  // Pack into contiguous I420: stride = width (no padding)
  std::size_t packedSize = static_cast<std::size_t>(outWidth) * outHeight * 3 / 2;
  auto output = MediaBuffer::create(packedSize);
  output->copyMetadataFrom(input);

  std::uint8_t* dst = output->data();
  int halfW = outWidth / 2;
  int halfH = outHeight / 2;

  // Copy Y plane (row by row if stride differs from width)
  if (yStride == outWidth)
  {
    std::memcpy(dst, ppDst[0], static_cast<std::size_t>(outWidth) * outHeight);
  }
  else
  {
    for (int row = 0; row < outHeight; ++row)
    {
      std::memcpy(dst + row * outWidth, ppDst[0] + row * yStride,
                  static_cast<std::size_t>(outWidth));
    }
  }
  dst += static_cast<std::size_t>(outWidth) * outHeight;

  // Copy U plane
  if (uvStride == halfW)
  {
    std::memcpy(dst, ppDst[1], static_cast<std::size_t>(halfW) * halfH);
  }
  else
  {
    for (int row = 0; row < halfH; ++row)
    {
      std::memcpy(dst + row * halfW, ppDst[1] + row * uvStride,
                  static_cast<std::size_t>(halfW));
    }
  }
  dst += static_cast<std::size_t>(halfW) * halfH;

  // Copy V plane
  if (uvStride == halfW)
  {
    std::memcpy(dst, ppDst[2], static_cast<std::size_t>(halfW) * halfH);
  }
  else
  {
    for (int row = 0; row < halfH; ++row)
    {
      std::memcpy(dst + row * halfW, ppDst[2] + row * uvStride,
                  static_cast<std::size_t>(halfW));
    }
  }

  output->setSize(packedSize);

  // Set video metadata
  output->setWidth(static_cast<std::uint32_t>(outWidth));
  output->setHeight(static_cast<std::uint32_t>(outHeight));
  output->setStride(0, static_cast<std::uint32_t>(outWidth));
  output->setStride(1, static_cast<std::uint32_t>(halfW));
  output->setStride(2, static_cast<std::uint32_t>(halfW));
  output->setPixelFormat(PixelFormat::I420);

  return output;
}

std::shared_ptr<MediaBuffer> H264Codec::plc(std::size_t /*frameSamples*/)
{
  return nullptr;
}

bool H264Codec::setParameter(const std::string& key, std::uint32_t value)
{
  if (_mode != H264Mode::Encoder || _encoder == nullptr)
  {
    return false;
  }

  auto* enc = _encoder;

  if (key == "bitrate")
  {
    SBitrateInfo bitrateInfo;
    std::memset(&bitrateInfo, 0, sizeof(bitrateInfo));
    bitrateInfo.iLayer = SPATIAL_LAYER_0;
    bitrateInfo.iBitrate = static_cast<int>(value);
    int rv = enc->SetOption(ENCODER_OPTION_BITRATE, &bitrateInfo);
    if (rv == 0)
    {
      _bitrate = value;
      return true;
    }
    return false;
  }

  if (key == "framerate")
  {
    float fps = static_cast<float>(value);
    int rv = enc->SetOption(ENCODER_OPTION_FRAME_RATE, &fps);
    if (rv == 0)
    {
      _framerate = fps;
      return true;
    }
    return false;
  }

  if (key == "requestKeyFrame")
  {
    _forceIdr = true;
    return true;
  }

  return false;
}

std::uint32_t H264Codec::getParameter(const std::string& key) const
{
  if (key == "bitrate")
  {
    return _bitrate;
  }
  if (key == "framerate")
  {
    return static_cast<std::uint32_t>(_framerate);
  }
  if (key == "profile")
  {
    return _profileIdc;
  }
  if (key == "width")
  {
    return _width;
  }
  if (key == "height")
  {
    return _height;
  }
  return 0;
}

} // namespace codecs
} // namespace iora
