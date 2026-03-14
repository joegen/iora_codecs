#include "av1_codec.hpp"

#include <aom/aomcx.h>

#include <dav1d/dav1d.h>
#include <dav1d/data.h>
#include <dav1d/picture.h>

#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

namespace {

void noopFree(const uint8_t*, void*)
{
}

} // anonymous namespace

Av1Codec::Av1Codec(CodecInfo info, Av1Mode mode,
                   std::uint32_t width, std::uint32_t height,
                   std::uint32_t bitrate, float framerate,
                   std::uint32_t speed)
  : _info(std::move(info))
  , _mode(mode)
  , _width(width)
  , _height(height)
  , _bitrate(bitrate)
  , _framerate(framerate)
  , _speed(speed)
{
  if (_width == 0 || _height == 0)
  {
    throw std::runtime_error("Av1Codec: width and height must be non-zero");
  }

  if ((_width % 2) != 0 || (_height % 2) != 0)
  {
    throw std::runtime_error("Av1Codec: width and height must be even for I420");
  }

  std::memset(&_encCtx, 0, sizeof(_encCtx));
  std::memset(&_cfg, 0, sizeof(_cfg));

  if (_mode == Av1Mode::Encoder)
  {
    aom_codec_iface_t* iface = aom_codec_av1_cx();

    aom_codec_err_t res = aom_codec_enc_config_default(iface, &_cfg, AOM_USAGE_REALTIME);
    if (res != AOM_CODEC_OK)
    {
      throw std::runtime_error("aom_codec_enc_config_default failed");
    }

    _cfg.g_w = _width;
    _cfg.g_h = _height;
    _cfg.rc_target_bitrate = _bitrate / 1000; // kbps
    _cfg.g_timebase.num = 1;
    _cfg.g_timebase.den = 90000; // 90kHz RTP clock
    _cfg.g_error_resilient = AOM_ERROR_RESILIENT_DEFAULT;
    _cfg.rc_end_usage = AOM_CBR;
    _cfg.g_threads = 1;
    _cfg.g_lag_in_frames = 0; // real-time, no look-ahead
    _cfg.kf_max_dist = static_cast<unsigned int>(_framerate); // ~1 keyframe/sec

    res = aom_codec_enc_init(&_encCtx, iface, &_cfg, 0);
    if (res != AOM_CODEC_OK)
    {
      throw std::runtime_error("aom_codec_enc_init failed");
    }

    // Set speed/CPU usage
    aom_codec_control(&_encCtx, AOME_SET_CPUUSED, static_cast<int>(_speed));

    // Allocate reusable I420 image wrapper
    _img = aom_img_alloc(nullptr, AOM_IMG_FMT_I420, _width, _height, 1);
    if (_img == nullptr)
    {
      aom_codec_destroy(&_encCtx);
      throw std::runtime_error("aom_img_alloc failed");
    }

    _encoderInitialized = true;
  }
  else
  {
    Dav1dSettings settings;
    dav1d_default_settings(&settings);
    settings.n_threads = 1;
    settings.max_frame_delay = 1; // low-latency

    int ret = dav1d_open(&_dav1dCtx, &settings);
    if (ret < 0)
    {
      throw std::runtime_error("dav1d_open failed");
    }
  }
}

Av1Codec::~Av1Codec()
{
  if (_img != nullptr)
  {
    aom_img_free(_img);
  }
  if (_encoderInitialized)
  {
    aom_codec_destroy(&_encCtx);
  }
  if (_dav1dCtx != nullptr)
  {
    dav1d_close(&_dav1dCtx);
  }
}

const CodecInfo& Av1Codec::info() const
{
  return _info;
}

std::shared_ptr<MediaBuffer> Av1Codec::encode(const MediaBuffer& input)
{
  if (_mode != Av1Mode::Encoder || !_encoderInitialized)
  {
    return nullptr;
  }

  // Validate minimum input size: packed I420 = width * height * 3/2
  std::size_t expectedBytes = static_cast<std::size_t>(_width) * _height * 3 / 2;
  if (input.size() < expectedBytes)
  {
    return nullptr;
  }

  // Copy input I420 data into aom_image planes
  const std::uint8_t* src = input.data();
  std::size_t ySize = static_cast<std::size_t>(_width) * _height;
  std::size_t uvSize = static_cast<std::size_t>(_width / 2) * (_height / 2);

  // Y plane
  for (std::uint32_t row = 0; row < _height; ++row)
  {
    std::memcpy(_img->planes[AOM_PLANE_Y] + row * _img->stride[AOM_PLANE_Y],
                src + row * _width,
                _width);
  }
  // U plane
  for (std::uint32_t row = 0; row < _height / 2; ++row)
  {
    std::memcpy(_img->planes[AOM_PLANE_U] + row * _img->stride[AOM_PLANE_U],
                src + ySize + row * (_width / 2),
                _width / 2);
  }
  // V plane
  for (std::uint32_t row = 0; row < _height / 2; ++row)
  {
    std::memcpy(_img->planes[AOM_PLANE_V] + row * _img->stride[AOM_PLANE_V],
                src + ySize + uvSize + row * (_width / 2),
                _width / 2);
  }

  // Determine encode flags
  aom_enc_frame_flags_t flags = 0;
  if (_forceKeyFrame)
  {
    flags |= AOM_EFLAG_FORCE_KF;
    _forceKeyFrame = false;
  }

  // pts in 90kHz timebase units
  aom_codec_pts_t pts = static_cast<aom_codec_pts_t>(input.timestamp());
  unsigned long duration = 1;

  aom_codec_err_t res = aom_codec_encode(&_encCtx, _img, pts, duration, flags);
  if (res != AOM_CODEC_OK)
  {
    return nullptr;
  }

  // Iterate output packets
  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t* pkt = nullptr;

  while ((pkt = aom_codec_get_cx_data(&_encCtx, &iter)) != nullptr)
  {
    if (pkt->kind == AOM_CODEC_CX_FRAME_PKT)
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

std::shared_ptr<MediaBuffer> Av1Codec::decode(const MediaBuffer& input)
{
  if (_mode != Av1Mode::Decoder || _dav1dCtx == nullptr)
  {
    return nullptr;
  }

  if (input.size() == 0)
  {
    return nullptr;
  }

  // Wrap input data for dav1d — no-op free since MediaBuffer owns the data
  Dav1dData data;
  std::memset(&data, 0, sizeof(data));
  int ret = dav1d_data_wrap(&data, input.data(), input.size(), noopFree, nullptr);
  if (ret < 0)
  {
    return nullptr;
  }

  // Feed data to decoder
  ret = dav1d_send_data(_dav1dCtx, &data);
  if (ret < 0 && ret != DAV1D_ERR(EAGAIN))
  {
    // Clean up remaining data on error
    if (data.sz > 0)
    {
      dav1d_data_unref(&data);
    }
    return nullptr;
  }

  if (ret == DAV1D_ERR(EAGAIN))
  {
    // Buffer full — try to drain a picture first, then retry
    Dav1dPicture drainPic;
    std::memset(&drainPic, 0, sizeof(drainPic));
    dav1d_get_picture(_dav1dCtx, &drainPic);
    dav1d_picture_unref(&drainPic);

    ret = dav1d_send_data(_dav1dCtx, &data);
    if (ret < 0)
    {
      if (data.sz > 0)
      {
        dav1d_data_unref(&data);
      }
      return nullptr;
    }
  }

  // Clean up if partially consumed
  if (data.sz > 0)
  {
    dav1d_data_unref(&data);
  }

  // Get decoded picture
  Dav1dPicture pic;
  std::memset(&pic, 0, sizeof(pic));
  ret = dav1d_get_picture(_dav1dCtx, &pic);
  if (ret < 0)
  {
    // DAV1D_ERR(EAGAIN) means no frame ready yet — not an error
    return nullptr;
  }

  // Verify I420 8-bit format
  if (pic.p.layout != DAV1D_PIXEL_LAYOUT_I420 || pic.p.bpc != 8)
  {
    dav1d_picture_unref(&pic);
    return nullptr;
  }

  int outWidth = pic.p.w;
  int outHeight = pic.p.h;

  if (outWidth <= 0 || outHeight <= 0)
  {
    dav1d_picture_unref(&pic);
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
  ptrdiff_t yStride = pic.stride[0];
  const auto* yPlane = static_cast<const std::uint8_t*>(pic.data[0]);
  if (yStride == static_cast<ptrdiff_t>(outWidth))
  {
    std::memcpy(dst, yPlane, static_cast<std::size_t>(outWidth) * outHeight);
  }
  else
  {
    for (int row = 0; row < outHeight; ++row)
    {
      std::memcpy(dst + row * outWidth,
                  yPlane + row * yStride,
                  static_cast<std::size_t>(outWidth));
    }
  }
  dst += static_cast<std::size_t>(outWidth) * outHeight;

  // Copy U plane
  ptrdiff_t uvStride = pic.stride[1];
  const auto* uPlane = static_cast<const std::uint8_t*>(pic.data[1]);
  if (uvStride == static_cast<ptrdiff_t>(halfW))
  {
    std::memcpy(dst, uPlane, static_cast<std::size_t>(halfW) * halfH);
  }
  else
  {
    for (int row = 0; row < halfH; ++row)
    {
      std::memcpy(dst + row * halfW,
                  uPlane + row * uvStride,
                  static_cast<std::size_t>(halfW));
    }
  }
  dst += static_cast<std::size_t>(halfW) * halfH;

  // Copy V plane
  const auto* vPlane = static_cast<const std::uint8_t*>(pic.data[2]);
  if (uvStride == static_cast<ptrdiff_t>(halfW))
  {
    std::memcpy(dst, vPlane, static_cast<std::size_t>(halfW) * halfH);
  }
  else
  {
    for (int row = 0; row < halfH; ++row)
    {
      std::memcpy(dst + row * halfW,
                  vPlane + row * uvStride,
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

  dav1d_picture_unref(&pic);

  return output;
}

std::shared_ptr<MediaBuffer> Av1Codec::plc(std::size_t /*frameSamples*/)
{
  return nullptr;
}

bool Av1Codec::setParameter(const std::string& key, std::uint32_t value)
{
  if (_mode != Av1Mode::Encoder || !_encoderInitialized)
  {
    return false;
  }

  if (key == "bitrate")
  {
    _cfg.rc_target_bitrate = value / 1000; // kbps
    aom_codec_err_t res = aom_codec_enc_config_set(&_encCtx, &_cfg);
    if (res == AOM_CODEC_OK)
    {
      _bitrate = value;
      return true;
    }
    return false;
  }

  if (key == "speed")
  {
    aom_codec_err_t res = aom_codec_control(&_encCtx, AOME_SET_CPUUSED,
                                              static_cast<int>(value));
    if (res == AOM_CODEC_OK)
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

std::uint32_t Av1Codec::getParameter(const std::string& key) const
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
