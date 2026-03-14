#include "vpx_codec.hpp"

#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>

#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

namespace {

vpx_codec_iface_t* encoderInterface(VpxVariant variant)
{
  return variant == VpxVariant::VP8
    ? vpx_codec_vp8_cx()
    : vpx_codec_vp9_cx();
}

vpx_codec_iface_t* decoderInterface(VpxVariant variant)
{
  return variant == VpxVariant::VP8
    ? vpx_codec_vp8_dx()
    : vpx_codec_vp9_dx();
}

} // anonymous namespace

VpxCodec::VpxCodec(CodecInfo info, VpxMode mode, VpxVariant variant,
                   std::uint32_t width, std::uint32_t height,
                   std::uint32_t bitrate, float framerate,
                   std::uint32_t speed)
  : _info(std::move(info))
  , _mode(mode)
  , _variant(variant)
  , _width(width)
  , _height(height)
  , _bitrate(bitrate)
  , _framerate(framerate)
  , _speed(speed)
{
  if (_width == 0 || _height == 0)
  {
    throw std::runtime_error("VpxCodec: width and height must be non-zero");
  }

  if ((_width % 2) != 0 || (_height % 2) != 0)
  {
    throw std::runtime_error("VpxCodec: width and height must be even for I420");
  }

  std::memset(&_ctx, 0, sizeof(_ctx));
  std::memset(&_cfg, 0, sizeof(_cfg));

  if (_mode == VpxMode::Encoder)
  {
    auto* iface = encoderInterface(_variant);

    vpx_codec_err_t res = vpx_codec_enc_config_default(iface, &_cfg, 0);
    if (res != VPX_CODEC_OK)
    {
      throw std::runtime_error("vpx_codec_enc_config_default failed");
    }

    _cfg.g_w = _width;
    _cfg.g_h = _height;
    _cfg.rc_target_bitrate = _bitrate / 1000; // kbps
    _cfg.g_timebase.num = 1;
    _cfg.g_timebase.den = 90000; // 90kHz RTP clock
    _cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;
    _cfg.rc_end_usage = VPX_CBR;
    _cfg.g_threads = 1;
    _cfg.g_lag_in_frames = 0; // real-time, no look-ahead
    _cfg.kf_max_dist = static_cast<unsigned int>(_framerate); // ~1 keyframe/sec

    res = vpx_codec_enc_init(&_ctx, iface, &_cfg, 0);
    if (res != VPX_CODEC_OK)
    {
      throw std::runtime_error("vpx_codec_enc_init failed");
    }

    // Set speed/CPU usage
    vpx_codec_control(&_ctx, VP8E_SET_CPUUSED, static_cast<int>(_speed));

    // Allocate reusable I420 image wrapper
    _img = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420, _width, _height, 1);
    if (_img == nullptr)
    {
      vpx_codec_destroy(&_ctx);
      throw std::runtime_error("vpx_img_alloc failed");
    }

    _initialized = true;
  }
  else
  {
    auto* iface = decoderInterface(_variant);

    vpx_codec_err_t res = vpx_codec_dec_init(&_ctx, iface, nullptr, 0);
    if (res != VPX_CODEC_OK)
    {
      throw std::runtime_error("vpx_codec_dec_init failed");
    }

    _initialized = true;
  }
}

VpxCodec::~VpxCodec()
{
  if (_img != nullptr)
  {
    vpx_img_free(_img);
  }
  if (_initialized)
  {
    vpx_codec_destroy(&_ctx);
  }
}

const CodecInfo& VpxCodec::info() const
{
  return _info;
}

std::shared_ptr<MediaBuffer> VpxCodec::encode(const MediaBuffer& input)
{
  if (_mode != VpxMode::Encoder || !_initialized)
  {
    return nullptr;
  }

  // Validate minimum input size: packed I420 = width * height * 3/2
  std::size_t expectedBytes = static_cast<std::size_t>(_width) * _height * 3 / 2;
  if (input.size() < expectedBytes)
  {
    return nullptr;
  }

  // Copy input I420 data into vpx_image planes
  const std::uint8_t* src = input.data();
  std::size_t ySize = static_cast<std::size_t>(_width) * _height;
  std::size_t uvSize = static_cast<std::size_t>(_width / 2) * (_height / 2);

  // Y plane
  for (std::uint32_t row = 0; row < _height; ++row)
  {
    std::memcpy(_img->planes[VPX_PLANE_Y] + row * _img->stride[VPX_PLANE_Y],
                src + row * _width,
                _width);
  }
  // U plane
  for (std::uint32_t row = 0; row < _height / 2; ++row)
  {
    std::memcpy(_img->planes[VPX_PLANE_U] + row * _img->stride[VPX_PLANE_U],
                src + ySize + row * (_width / 2),
                _width / 2);
  }
  // V plane
  for (std::uint32_t row = 0; row < _height / 2; ++row)
  {
    std::memcpy(_img->planes[VPX_PLANE_V] + row * _img->stride[VPX_PLANE_V],
                src + ySize + uvSize + row * (_width / 2),
                _width / 2);
  }

  // Determine encode flags
  vpx_enc_frame_flags_t flags = 0;
  if (_forceKeyFrame)
  {
    flags |= VPX_EFLAG_FORCE_KF;
    _forceKeyFrame = false;
  }

  // pts in 90kHz timebase units
  vpx_codec_pts_t pts = static_cast<vpx_codec_pts_t>(input.timestamp());
  unsigned long duration = 1;

  vpx_codec_err_t res = vpx_codec_encode(&_ctx, _img, pts, duration, flags, VPX_DL_REALTIME);
  if (res != VPX_CODEC_OK)
  {
    return nullptr;
  }

  // Iterate output packets
  vpx_codec_iter_t iter = nullptr;
  const vpx_codec_cx_pkt_t* pkt = nullptr;

  while ((pkt = vpx_codec_get_cx_data(&_ctx, &iter)) != nullptr)
  {
    if (pkt->kind == VPX_CODEC_CX_FRAME_PKT)
    {
      auto output = MediaBuffer::create(pkt->data.frame.sz);
      output->copyMetadataFrom(input);
      std::memcpy(output->data(), pkt->data.frame.buf, pkt->data.frame.sz);
      output->setSize(pkt->data.frame.sz);
      return output;
    }
  }

  // No frame produced (rate control skip)
  return nullptr;
}

std::shared_ptr<MediaBuffer> VpxCodec::decode(const MediaBuffer& input)
{
  if (_mode != VpxMode::Decoder || !_initialized)
  {
    return nullptr;
  }

  if (input.size() == 0)
  {
    return nullptr;
  }

  vpx_codec_err_t res = vpx_codec_decode(&_ctx, input.data(),
                                           static_cast<unsigned int>(input.size()),
                                           nullptr, 0);
  if (res != VPX_CODEC_OK)
  {
    return nullptr;
  }

  vpx_codec_iter_t iter = nullptr;
  vpx_image_t* img = vpx_codec_get_frame(&_ctx, &iter);
  if (img == nullptr)
  {
    return nullptr;
  }

  int outWidth = static_cast<int>(img->d_w);
  int outHeight = static_cast<int>(img->d_h);

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

  // Copy Y plane
  int yStride = img->stride[VPX_PLANE_Y];
  if (yStride == outWidth)
  {
    std::memcpy(dst, img->planes[VPX_PLANE_Y],
                static_cast<std::size_t>(outWidth) * outHeight);
  }
  else
  {
    for (int row = 0; row < outHeight; ++row)
    {
      std::memcpy(dst + row * outWidth,
                  img->planes[VPX_PLANE_Y] + row * yStride,
                  static_cast<std::size_t>(outWidth));
    }
  }
  dst += static_cast<std::size_t>(outWidth) * outHeight;

  // Copy U plane
  int uvStride = img->stride[VPX_PLANE_U];
  if (uvStride == halfW)
  {
    std::memcpy(dst, img->planes[VPX_PLANE_U],
                static_cast<std::size_t>(halfW) * halfH);
  }
  else
  {
    for (int row = 0; row < halfH; ++row)
    {
      std::memcpy(dst + row * halfW,
                  img->planes[VPX_PLANE_U] + row * uvStride,
                  static_cast<std::size_t>(halfW));
    }
  }
  dst += static_cast<std::size_t>(halfW) * halfH;

  // Copy V plane
  int vStride = img->stride[VPX_PLANE_V];
  if (vStride == halfW)
  {
    std::memcpy(dst, img->planes[VPX_PLANE_V],
                static_cast<std::size_t>(halfW) * halfH);
  }
  else
  {
    for (int row = 0; row < halfH; ++row)
    {
      std::memcpy(dst + row * halfW,
                  img->planes[VPX_PLANE_V] + row * vStride,
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

std::shared_ptr<MediaBuffer> VpxCodec::plc(std::size_t /*frameSamples*/)
{
  return nullptr;
}

bool VpxCodec::setParameter(const std::string& key, std::uint32_t value)
{
  if (_mode != VpxMode::Encoder || !_initialized)
  {
    return false;
  }

  if (key == "bitrate")
  {
    _cfg.rc_target_bitrate = value / 1000; // kbps
    vpx_codec_err_t res = vpx_codec_enc_config_set(&_ctx, &_cfg);
    if (res == VPX_CODEC_OK)
    {
      _bitrate = value;
      return true;
    }
    return false;
  }

  if (key == "speed")
  {
    vpx_codec_err_t res = vpx_codec_control_(&_ctx, VP8E_SET_CPUUSED,
                                               static_cast<int>(value));
    if (res == VPX_CODEC_OK)
    {
      _speed = value;
      return true;
    }
    return false;
  }

  if (key == "framerate")
  {
    _framerate = static_cast<float>(value);
    return true;
  }

  if (key == "requestKeyFrame")
  {
    _forceKeyFrame = true;
    return true;
  }

  return false;
}

std::uint32_t VpxCodec::getParameter(const std::string& key) const
{
  if (key == "bitrate")
  {
    return _bitrate;
  }
  if (key == "speed")
  {
    return _speed;
  }
  if (key == "framerate")
  {
    return static_cast<std::uint32_t>(_framerate);
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
