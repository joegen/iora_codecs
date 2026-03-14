// build_smoke_test.cpp — Verify linkage for all enabled codec submodules.
// Each codec is guarded by its IORA_CODECS_ENABLE_* preprocessor define,
// so this test compiles correctly for any combination of enabled/disabled codecs.

#include <cstdio>

// Iora framework (always available)
#include <iora/iora.hpp>

// Opus
#ifdef IORA_CODECS_ENABLE_OPUS
#include <opus/opus.h>
#endif

// G.722
#ifdef IORA_CODECS_ENABLE_G722
extern "C" {
#include <g722_encoder.h>
#include <g722_decoder.h>
}
#endif

// iLBC
#ifdef IORA_CODECS_ENABLE_ILBC
#include <ilbc.h>
#endif

// AMR
#ifdef IORA_CODECS_ENABLE_AMR
extern "C" {
#include <opencore-amrnb/interf_enc.h>
#include <opencore-amrnb/interf_dec.h>
#include <opencore-amrwb/dec_if.h>
#include <vo-amrwbenc/enc_if.h>
}
#endif

// H.264 (OpenH264)
#ifdef IORA_CODECS_ENABLE_H264
#include <wels/codec_api.h>
#endif

// VP8/VP9 (libvpx)
#ifdef IORA_CODECS_ENABLE_VPX
#include <vpx/vpx_encoder.h>
#include <vpx/vpx_decoder.h>
#endif

// AV1 (libaom + dav1d)
#ifdef IORA_CODECS_ENABLE_AV1
#include <aom/aom_encoder.h>
#include <aom/aom_decoder.h>
#include <dav1d/dav1d.h>
#endif

// speexdsp resampler (always built when any audio codec is enabled)
#include <speex/speex_resampler.h>

int main()
{
  int pass = 0;
  int total = 0;

  // Iora
  ++total;
  std::printf("[PASS] iora: header-only library included\n");
  ++pass;

  // speexdsp
  ++total;
  int err = 0;
  auto* resampler = speex_resampler_init(1, 48000, 8000, 3, &err);
  if (resampler && err == 0)
  {
    speex_resampler_destroy(resampler);
    std::printf("[PASS] speexdsp: resampler init/destroy OK\n");
    ++pass;
  }
  else
  {
    std::printf("[FAIL] speexdsp: resampler init failed (err=%d)\n", err);
  }

#ifdef IORA_CODECS_ENABLE_OPUS
  ++total;
  const char* ver = opus_get_version_string();
  if (ver)
  {
    std::printf("[PASS] opus: %s\n", ver);
    ++pass;
  }
  else
  {
    std::printf("[FAIL] opus: opus_get_version_string returned null\n");
  }
#endif

#ifdef IORA_CODECS_ENABLE_G722
  ++total;
  G722_ENC_CTX* g722_enc = g722_encoder_new(64000, 0);
  if (g722_enc)
  {
    g722_encoder_destroy(g722_enc);
    std::printf("[PASS] g722: encoder new/destroy OK\n");
    ++pass;
  }
  else
  {
    std::printf("[FAIL] g722: encoder new failed\n");
  }
#endif

#ifdef IORA_CODECS_ENABLE_ILBC
  ++total;
  IlbcEncoderInstance* ilbc_enc = nullptr;
  int16_t ilbc_rc = WebRtcIlbcfix_EncoderCreate(&ilbc_enc);
  if (ilbc_rc == 0 && ilbc_enc)
  {
    WebRtcIlbcfix_EncoderFree(ilbc_enc);
    std::printf("[PASS] ilbc: encoder create/free OK\n");
    ++pass;
  }
  else
  {
    std::printf("[FAIL] ilbc: encoder create failed\n");
  }
#endif

#ifdef IORA_CODECS_ENABLE_AMR
  ++total;
  void* amr_enc = Encoder_Interface_init(0);
  if (amr_enc)
  {
    Encoder_Interface_exit(amr_enc);
    std::printf("[PASS] opencore-amr: NB encoder init/exit OK\n");
    ++pass;
  }
  else
  {
    std::printf("[FAIL] opencore-amr: NB encoder init failed\n");
  }

  ++total;
  void* amrwb_enc = E_IF_init();
  if (amrwb_enc)
  {
    E_IF_exit(amrwb_enc);
    std::printf("[PASS] vo-amrwbenc: WB encoder init/exit OK\n");
    ++pass;
  }
  else
  {
    std::printf("[FAIL] vo-amrwbenc: WB encoder init failed\n");
  }
#endif

#ifdef IORA_CODECS_ENABLE_H264
  ++total;
  OpenH264Version h264_ver = WelsGetCodecVersion();
  std::printf("[PASS] openh264: version %u.%u.%u\n",
    h264_ver.uMajor, h264_ver.uMinor, h264_ver.uRevision);
  ++pass;
#endif

#ifdef IORA_CODECS_ENABLE_VPX
  ++total;
  const char* vpx_ver = vpx_codec_version_str();
  if (vpx_ver)
  {
    std::printf("[PASS] libvpx: %s\n", vpx_ver);
    ++pass;
  }
  else
  {
    std::printf("[FAIL] libvpx: version string null\n");
  }
#endif

#ifdef IORA_CODECS_ENABLE_AV1
  ++total;
  const char* aom_ver = aom_codec_version_str();
  if (aom_ver)
  {
    std::printf("[PASS] libaom: %s\n", aom_ver);
    ++pass;
  }
  else
  {
    std::printf("[FAIL] libaom: version string null\n");
  }

  ++total;
  {
    Dav1dSettings dav1d_settings;
    dav1d_default_settings(&dav1d_settings);
    std::printf("[PASS] dav1d: default settings initialized\n");
    ++pass;
  }
#endif

#ifdef IORA_CODECS_ENABLE_G711
  // G.711 is built-in (no external library) — verify the option is defined
  ++total;
  std::printf("[PASS] g711: built-in (no external dependency)\n");
  ++pass;
#endif

  std::printf("\n=== Smoke test: %d/%d passed ===\n", pass, total);
  return (pass == total) ? 0 : 1;
}
