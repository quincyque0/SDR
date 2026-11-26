// SdrVisualizer.h  -  Real-time modulation visualizer + file player
// Dear ImGui (docking branch, >= 1.91.1) + ImPlot.  C++17.
//
// Threading model
//   DSP thread (Engine::Run)  : synthesises / reads IQ, applies channel, FFT,
//   builds Snapshot (~60 Hz) UI thread  (App::Render)  : try_lock + copy of the
//   latest Snapshot, draws, writes Params under a mutex. No allocations happen
//   in the render path (all buffers are preallocated).
//
// Integration (your main loop):
//   ImGui::CreateContext(); ImPlot::CreateContext();
//   io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
//   sdrviz::App app;  app.LoadFonts(io);  app.Init();  app.Start();
//   ... each frame, between ImGui::NewFrame() and ImGui::Render():
//   app.Render(); on shutdown: app.Stop();
#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace sdrviz {

using cf = std::complex<float>;
constexpr float kPi = 3.14159265358979f;
constexpr float k2Pi = 6.28318530717959f;

// ============================================================================
//  Shared data types
// ============================================================================
enum class Mod : int { BPSK = 0, QPSK, QAM16, FM, OFDM };
enum class FileFmt : int { CF32 = 0, CS16, CU8 };
enum class SourceMode : int { LiveSdr = 0, File, Sim };

constexpr int kBlock = 1024; // samples per DSP block
constexpr int kRing = 32768; // IQ ring (power of two)
constexpr int kRingMask = kRing - 1;
constexpr int kTimeN = 2048; // samples shown in time plot
constexpr int kMaxFft = 16384;
constexpr int kWfCols = 512, kWfRows = 96;
constexpr int kConstN = 1536; // constellation persistence
constexpr int kEyeTraces = 48;
constexpr int kMaxSps = 64;
constexpr int kMaxEyeLen = 2 * kMaxSps + 1;
constexpr int kSpan = 8; // pulse-shaping span in symbols
constexpr int kOfdmN = 64, kOfdmCp = 16;

struct Params { // UI -> DSP (copied under mutex)
  SourceMode sourceMode = SourceMode::File;
  char sdrUri[128] = "usb:1.11.5";
  float sdrRxFreqMhz = 750.0f;
  float sdrSampleRateMsps = 1.0f;
  float sdrGainDb = 30.0f;

  Mod mod = Mod::QPSK;
  bool useFile = true;
  FileFmt fileFmt = FileFmt::CS16;
  float sampleRateMsps = 1.0f;
  float centerFreqMhz = 750.0f;
  float carrierKHz = 0.0f;
  float symbolRateKsps = 100.0f;
  float alpha = 0.35f;
  float gainDb = -6.0f;
  float fmDevKHz = 25.0f;
  bool awgn = false;
  float ebn0Db = 25.0f;
  bool freqOff = false;
  float freqOffKHz = 0.0f;
  bool phaseNoise = false;
  float phaseNoiseDeg = 1.0f;
  bool afc = true;
  float triggerLevel = 0.0f;
  int fftSize = 4096;
  bool peakHold = true;
};

struct FileInfo {
  bool loaded = false;
  std::string name, error;
  FileFmt fmt = FileFmt::CS16;
  uint64_t bytes = 0, samples = 0;
};

struct Snapshot { // DSP -> UI
  uint64_t seq = 0;
  SourceMode sourceMode = SourceMode::File;
  bool sdrActive = false;
  std::string sdrStatus = "Disconnected";
  float fs = 1.0e6f, cfHz = 750.0e6f;
  int sps = 20, fftN = 4096, eyeLen = 41, eyeCount = 0, constCount = 0,
      idealCount = 0;
  Mod mod = Mod::QPSK;
  float snrDb = NAN, evmPct = 0, dspLoad = 0;
  bool evmValid = false, fileMode = false;
  double posSec = 0, totalSec = 0;
  float timeI[kTimeN]{}, timeQ[kTimeN]{};
  float constI[kConstN]{}, constQ[kConstN]{};
  float idealI[16]{}, idealQ[16]{};
  std::vector<float> psd, peak, wf, eye;
  Snapshot()
      : psd(kMaxFft, -120.f), peak(kMaxFft, -120.f),
        wf(kWfRows * kWfCols, -120.f), eye(kEyeTraces * kMaxEyeLen, 0.f) {}
};

// ============================================================================
//  Radix-2 FFT
// ============================================================================
class Fft {
public:
  void Init(int n) {
    n_ = n;
    tw_.resize(n / 2);
    rev_.resize(n);
    int bits = 0;
    while ((1 << bits) < n)
      ++bits;
    for (int k = 0; k < n / 2; ++k)
      tw_[k] = std::polar(1.0f, -k2Pi * (float)k / (float)n);
    for (int i = 0; i < n; ++i) {
      int r = 0;
      for (int b = 0; b < bits; ++b)
        if (i & (1 << b))
          r |= 1 << (bits - 1 - b);
      rev_[i] = r;
    }
  }
  void Forward(cf *d) const {
    for (int i = 0; i < n_; ++i) {
      int j = rev_[i];
      if (j > i)
        std::swap(d[i], d[j]);
    }
    for (int len = 2; len <= n_; len <<= 1) {
      int half = len >> 1, step = n_ / len;
      for (int i = 0; i < n_; i += len)
        for (int k = 0; k < half; ++k) {
          cf w = tw_[k * step], a = d[i + k], b = d[i + k + half];
          cf v(b.real() * w.real() - b.imag() * w.imag(),
               b.real() * w.imag() + b.imag() * w.real());
          d[i + k] = a + v;
          d[i + k + half] = a - v;
        }
    }
  }
  void Inverse(cf *d) const { // unscaled
    for (int i = 0; i < n_; ++i)
      d[i] = std::conj(d[i]);
    Forward(d);
    for (int i = 0; i < n_; ++i)
      d[i] = std::conj(d[i]);
  }

private:
  int n_ = 0;
  std::vector<cf> tw_;
  std::vector<int> rev_;
};

// ============================================================================
//  DSP engine (runs in its own thread)
// ============================================================================
class Engine {
public:
  // ---- control surface (thread-safe) ----
  std::atomic<bool> playing{true}, loop{true}, clearPeak{false};
  std::atomic<int> stepReq{0};
  std::atomic<bool> seekReq{false};
  std::atomic<double> seekSec{0.0};

  Engine() {
    bb_.assign(kRing, cf(0));
    rx_.assign(kRing, cf(0));
    fftBuf_.assign(kMaxFft, cf(0));
    psd_.assign(kMaxFft, -120.f);
    peak_.assign(kMaxFft, -120.f);
    wfRows_.assign(kWfRows * kWfCols, -120.f);
    fileBlk_.assign(kBlock, cf(0));
    raw_.assign(kBlock * 8, 0);
    taps_.assign(kSpan * kMaxSps, 0.f);
    fft64_.Init(kOfdmN);
    total_ = kRing; // so that (total_ - N) never underflows

    liveBlk_.assign(kBlock, cf(0));
    sdrRawBuf_.assign(kBlock * 2, 0);
  }
  ~Engine() {
    Stop();
    StopLiveSdr();
  }

  void Start() {
    if (running_.exchange(true))
      return;
    th_ = std::thread(&Engine::Run, this);
  }
  void Stop() {
    if (!running_.exchange(false))
      return;
    if (th_.joinable())
      th_.join();
  }

  void SetParams(const Params &p) {
    std::lock_guard<std::mutex> l(pm_);
    params_ = p;
  }
  bool Fetch(Snapshot &out) { // non-blocking; true if new data
    std::unique_lock<std::mutex> l(sm_, std::try_to_lock);
    if (!l.owns_lock() || shared_.seq == out.seq)
      return false;
    out = shared_;
    return true;
  }
  void RequestLoad(const std::string &path, FileFmt fmt) {
    {
      std::lock_guard<std::mutex> l(lm_);
      pendPath_ = path;
      pendFmt_ = fmt;
    }
    loadReq_ = true;
  }
  void RequestSdrStart(const std::string &uri, double freqHz, double rateHz, double gainDb) {
    {
      std::lock_guard<std::mutex> l(sdrReqMutex_);
      sdrReqUri_ = uri;
      sdrReqFreq_ = freqHz;
      sdrReqRate_ = rateHz;
      sdrReqGain_ = gainDb;
    }
    sdrStartReq_ = true;
  }
  void RequestSdrStop() {
    sdrStopReq_ = true;
  }
  uint32_t InfoSeq() const { return infoSeq_.load(); }
  FileInfo GetInfo() {
    std::lock_guard<std::mutex> l(im_);
    return info_;
  }

private:
  // ---------------- thread ----------------
  void Run() {
    using clk = std::chrono::steady_clock;
    Params p;
    {
      std::lock_guard<std::mutex> l(pm_);
      p = params_;
    }
    Reconfigure(p, true);
    auto last = clk::now(), lastSnap = last;
    double due = 0, busyAcc = 0;
    while (running_) {
      auto t0 = clk::now();
      {
        std::lock_guard<std::mutex> l(pm_);
        p = params_;
      }
      if (loadReq_.exchange(false)) {
        std::string path;
        FileFmt f;
        {
          std::lock_guard<std::mutex> l(lm_);
          path = pendPath_;
          f = pendFmt_;
        }
        OpenFile(path, f);
      }
      if (sdrStartReq_.exchange(false)) {
        std::string u;
        double f, r, g;
        {
          std::lock_guard<std::mutex> l(sdrReqMutex_);
          u = sdrReqUri_;
          f = sdrReqFreq_;
          r = sdrReqRate_;
          g = sdrReqGain_;
        }
        StartLiveSdr(u, f, r, g);
      }
      if (sdrStopReq_.exchange(false)) {
        StopLiveSdr();
      }
      Reconfigure(p, false);
      const double fs = (p.sourceMode == SourceMode::LiveSdr)
                            ? (p.sdrSampleRateMsps * 1e6)
                            : (p.sampleRateMsps * 1e6);
      double dt = std::chrono::duration<double>(t0 - last).count();
      last = t0;
      if (seekReq_exchange() && file_.is_open())
        SeekFile(fs, seekSec.load());
      if (p.sourceMode == SourceMode::LiveSdr && sdrActive_) {
        ProduceBlock(p);
      } else {
        if (stepReq.exchange(0) > 0)
          ProduceBlock(p);
        if (playing.load()) {
          due += dt * fs;
          int nb = 0;
          while (due >= kBlock && nb < 64) {
            ProduceBlock(p);
            due -= kBlock;
            ++nb;
          }
          if (due > kBlock * 64)
            due = 0;
        } else
          due = 0;
      }
      auto t1 = clk::now();
      busyAcc += std::chrono::duration<double>(t1 - t0).count();
      if (t1 - lastSnap >= std::chrono::milliseconds(16)) {
        double wall = std::chrono::duration<double>(t1 - lastSnap).count();
        BuildSnapshot(p, (float)std::min(1.0, busyAcc / wall));
        busyAcc = 0;
        lastSnap = t1;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  }
  bool seekReq_exchange() { return seekReq.exchange(false); }

  // ---------------- configuration ----------------
  void Reconfigure(const Params &p, bool force) {
    const float fs = p.sampleRateMsps * 1e6f;
    int s = std::clamp(
        (int)std::lround(fs / std::max(1.f, p.symbolRateKsps * 1e3f)), 2,
        kMaxSps);
    float a = std::clamp(p.alpha, 0.05f, 1.0f);
    if (force || s != sps_ || a != alpha_) {
      sps_ = s;
      alpha_ = a;
      BuildTaps();
    }
    if (force || p.mod != mod_) {
      mod_ = p.mod;
      BuildConstellation();
    }
    if (force || p.fftSize != fftN_) {
      fftN_ = std::clamp(p.fftSize, 1024, kMaxFft);
      fft_.Init(fftN_);
      win_.resize(fftN_);
      sumW_ = 0;
      for (int i = 0; i < fftN_; ++i) {
        win_[i] = 0.5f - 0.5f * cosf(k2Pi * i / (fftN_ - 1));
        sumW_ += win_[i];
      }
      psdInit_ = false;
    }
  }
  void BuildTaps() { // raised-cosine (zero-ISI at symbol instants)
    const int L = kSpan * sps_;
    for (int i = 0; i < L; ++i) {
      float t = (float)(i - L / 2) / (float)sps_;
      float x = kPi * t, sinc = std::fabs(t) < 1e-6f ? 1.f : sinf(x) / x;
      float den = 1.f - 4.f * alpha_ * alpha_ * t * t;
      float rc =
          std::fabs(den) < 1e-5f ? kPi / 4.f : cosf(kPi * alpha_ * t) / den;
      taps_[i] = sinc * rc;
    }
    symPhase_ = 0;
    cnt_ = 0;
    nh_ = 0;
    nsum_ = cf(0);
    for (auto &n : nhist_)
      n = cf(0);
  }
  void BuildConstellation() {
    cons_.clear();
    switch (mod_) {
    case Mod::BPSK:
      bps_ = 1;
      cons_ = {{-1, 0}, {1, 0}};
      break;
    case Mod::QPSK:
    case Mod::OFDM: {
      bps_ = 2;
      float a = 0.70710678f;
      cons_ = {{a, a}, {-a, a}, {-a, -a}, {a, -a}};
    } break;
    case Mod::QAM16: {
      bps_ = 4;
      const float l[4] = {-3, -1, 1, 3};
      float s = 1.f / sqrtf(10.f);
      for (int i = 0; i < 4; ++i)
        for (int q = 0; q < 4; ++q)
          cons_.push_back({l[i] * s, l[q] * s});
    } break;
    case Mod::FM:
      bps_ = 1;
      break;
    }
    for (auto &c : symHist_)
      c = cf(0);
    constCount_ = 0;
    constHead_ = 0;
    evm2_ = 0.01f;
    haveEvm_ = false;
  }

  // ---------------- live SDR source ----------------
  bool StartLiveSdr(const std::string &uri, double freqHz, double rateHz, double gainDb) {
    StopLiveSdr();
    SoapySDRKwargs args{};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    if (!uri.empty() && uri != "auto") {
      SoapySDRKwargs_set(&args, "uri", uri.c_str());
    }
    SoapySDRKwargs_set(&args, "loopback", "0");
    sdr_ = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (!sdr_) {
      sdrStatus_ = "Error: Pluto SDR not found";
      sdrActive_ = false;
      return false;
    }

    if (SoapySDRDevice_setSampleRate(sdr_, SOAPY_SDR_RX, 0, rateHz) != 0 ||
        SoapySDRDevice_setFrequency(sdr_, SOAPY_SDR_RX, 0, freqHz, nullptr) != 0 ||
        SoapySDRDevice_setGain(sdr_, SOAPY_SDR_RX, 0, gainDb) != 0) {
      sdrStatus_ = "Error: RF parameters config failed";
      StopLiveSdr();
      return false;
    }

    size_t channels[] = {0};
    rxStream_ = SoapySDRDevice_setupStream(sdr_, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, 1, nullptr);
    if (!rxStream_) {
      sdrStatus_ = "Error: setupStream RX failed";
      StopLiveSdr();
      return false;
    }

    if (SoapySDRDevice_activateStream(sdr_, rxStream_, 0, 0, 0) != 0) {
      sdrStatus_ = "Error: activateStream RX failed";
      StopLiveSdr();
      return false;
    }

    sdrStatus_ = "Streaming Live RX";
    sdrActive_ = true;
    return true;
  }

  void StopLiveSdr() {
    if (sdr_ && rxStream_) {
      SoapySDRDevice_deactivateStream(sdr_, rxStream_, 0, 0);
      SoapySDRDevice_closeStream(sdr_, rxStream_);
      rxStream_ = nullptr;
    }
    if (sdr_) {
      SoapySDRDevice_unmake(sdr_);
      sdr_ = nullptr;
    }
    sdrActive_ = false;
    sdrStatus_ = "Disconnected";
  }

  void ReadLiveSdrBlock() {
    if (!sdr_ || !rxStream_) {
      std::fill(liveBlk_.begin(), liveBlk_.end(), cf(0, 0));
      return;
    }
    void *buffs[] = {sdrRawBuf_.data()};
    int flags = 0;
    long long timeNs = 0;
    int sr = SoapySDRDevice_readStream(sdr_, rxStream_, buffs, kBlock, &flags, &timeNs, 100000);
    if (sr > 0) {
      for (int i = 0; i < sr; ++i) {
        liveBlk_[i] = cf(sdrRawBuf_[2 * i] / 32768.0f, sdrRawBuf_[2 * i + 1] / 32768.0f);
      }
      for (int i = sr; i < kBlock; ++i) {
        liveBlk_[i] = cf(0, 0);
      }
    } else {
      std::fill(liveBlk_.begin(), liveBlk_.end(), cf(0, 0));
    }
  }

  // ---------------- file source ----------------
  void OpenFile(const std::string &path, FileFmt fmt) {
    if (file_.is_open())
      file_.close();
    file_.clear();
    file_.open(path, std::ios::binary | std::ios::ate);
    FileInfo fi;
    fi.fmt = fmt;
    if (!file_) {
      fi.error = "Cannot open file";
      Publish(fi);
      return;
    }
    fileBytes_ = (uint64_t)file_.tellg();
    fileBps_ = fmt == FileFmt::CF32 ? 8 : fmt == FileFmt::CS16 ? 4 : 2;
    fileTotal_ = fileBytes_ / fileBps_;
    filePos_ = 0;
    file_.seekg(0);
    size_t sl = path.find_last_of("/\\");
    fi.name = sl == std::string::npos ? path : path.substr(sl + 1);
    fi.bytes = fileBytes_;
    fi.samples = fileTotal_;
    fi.loaded = fileTotal_ > 0;
    if (!fi.loaded)
      fi.error = "File is empty";
    Publish(fi);
  }
  void Publish(const FileInfo &fi) {
    {
      std::lock_guard<std::mutex> l(im_);
      info_ = fi;
    }
    ++infoSeq_;
  }
  void SeekFile(double fs, double sec) {
    uint64_t pos = (uint64_t)std::clamp(
        sec * fs, 0.0, (double)(fileTotal_ ? fileTotal_ - 1 : 0));
    file_.clear();
    file_.seekg((std::streamoff)(pos * fileBps_));
    filePos_ = pos;
  }
  void ReadFileBlock(const Params &p) {
    int got = 0;
    const int bps = fileBps_;
    while (got < kBlock) {
      file_.read((char *)raw_.data(), (std::streamsize)(kBlock - got) * bps);
      int n = (int)(file_.gcount() / bps);
      const uint8_t *r = raw_.data();
      for (int i = 0; i < n; ++i) {
        cf v;
        if (p.fileFmt == FileFmt::CF32) {
          float f[2];
          std::memcpy(f, r + i * 8, 8);
          v = {f[0], f[1]};
        } else if (p.fileFmt == FileFmt::CS16) {
          int16_t s[2];
          std::memcpy(s, r + i * 4, 4);
          v = {s[0] / 32768.f, s[1] / 32768.f};
        } else
          v = {(r[i * 2] - 127.5f) / 127.5f, (r[i * 2 + 1] - 127.5f) / 127.5f};
        fileBlk_[got + i] = v;
      }
      got += n;
      filePos_ += n;
      if (got < kBlock) {
        file_.clear();
        if (loop.load() && fileTotal_ > 0) {
          file_.seekg(0);
          filePos_ = 0;
        } else {
          playing = false;
          break;
        }
      }
    }
    for (; got < kBlock; ++got)
      fileBlk_[got] = cf(0);
  }

  // ---------------- signal generation ----------------
  void BuildOfdmSymbol() {
    for (auto &x : ofdmX_)
      x = cf(0);
    for (int k = -26; k <= 26; ++k)
      if (k)
        ofdmX_[(k + kOfdmN) % kOfdmN] = cons_[(rng_() >> 8) % cons_.size()];
    std::array<cf, kOfdmN> t = ofdmX_;
    fft64_.Inverse(t.data());
    for (auto &v : t)
      v *= 0.125f; // 1/sqrt(64)
    for (int i = 0; i < kOfdmCp; ++i)
      ofdmTx_[i] = t[kOfdmN - kOfdmCp + i];
    for (int i = 0; i < kOfdmN; ++i)
      ofdmTx_[kOfdmCp + i] = t[i];
  }
  cf NextTx(const Params &p, float fs, bool &inst, cf &ideal, int &kind) {
    inst = false;
    kind = 0;
    ideal = cf(0);
    if (mod_ == Mod::FM) {
      float m = 0.7f * sinf(msg1_) + 0.3f * sinf(msg2_);
      msg1_ += k2Pi * 1000.f / fs;
      msg2_ += k2Pi * 2300.f / fs;
      if (msg1_ > k2Pi)
        msg1_ -= k2Pi;
      if (msg2_ > k2Pi)
        msg2_ -= k2Pi;
      fmPh_ += k2Pi * (p.fmDevKHz * 1e3f * m) / fs;
      while (fmPh_ > kPi)
        fmPh_ -= k2Pi;
      while (fmPh_ < -kPi)
        fmPh_ += k2Pi;
      if (++cnt_ >= sps_) {
        cnt_ = 0;
        inst = true;
      }
      return std::polar(1.0f, fmPh_);
    }
    if (mod_ == Mod::OFDM) {
      if (ofdmPos_ == 0)
        BuildOfdmSymbol();
      cf y = ofdmTx_[ofdmPos_];
      kind = 2;
      inst = (ofdmPos_ == kOfdmN + kOfdmCp - 1);
      ofdmPos_ = (ofdmPos_ + 1) % (kOfdmN + kOfdmCp);
      return y;
    }
    if (symPhase_ == 0) {
      std::copy_backward(symHist_.begin(), symHist_.end() - 1, symHist_.end());
      symHist_[0] = cons_[(rng_() >> 8) % cons_.size()];
    }
    cf y(0);
    for (int k = 0; k < kSpan; ++k)
      y += symHist_[k] * taps_[symPhase_ + k * sps_];
    inst = (symPhase_ == 0);
    ideal = symHist_[kSpan / 2];
    kind = 1;
    if (++symPhase_ >= sps_)
      symPhase_ = 0;
    return y;
  }
  void PushConst(cf t) {
    constI_[constHead_] = t.real();
    constQ_[constHead_] = t.imag();
    constHead_ = (constHead_ + 1) % kConstN;
    if (constCount_ < kConstN)
      ++constCount_;
  }
  void EvmAcc(cf t, cf ideal) {
    evm2_ += 0.01f * (std::norm(t - ideal) - evm2_);
    haveEvm_ = true;
  }

  void ProduceBlock(const Params &p) {
    const bool isLive = (p.sourceMode == SourceMode::LiveSdr) && sdrActive_;
    const bool fileMode = (p.sourceMode == SourceMode::File) && file_.is_open() && fileTotal_ > 0;
    const float fs = isLive ? (p.sdrSampleRateMsps * 1e6f) : (p.sampleRateMsps * 1e6f);
    if (isLive)
      ReadLiveSdrBlock();
    else if (fileMode)
      ReadFileBlock(p);
    const float amp = (mod_ == Mod::OFDM) ? 0.25f : 0.5f;
    const float G = powf(10.f, p.gainDb / 20.f) * amp;
    const float dFo = p.freqOff ? k2Pi * p.freqOffKHz * 1e3f / fs : 0.f;
    const cf foStep = std::polar(1.0f, dFo);
    const float pnStd =
        p.phaseNoise ? (p.phaseNoiseDeg * kPi / 180.f) / sqrtf((float)sps_)
                     : 0.f;
    const float esn0 =
        powf(10.f, (p.ebn0Db + 10.f * log10f((float)bps_)) / 10.f);
    normGain_ = (isLive || fileMode) ? std::max(1e-6f, sqrtf(symPw_)) : G;
    fileMode_ = (isLive || fileMode);

    for (int i = 0; i < kBlock; ++i) {
      const uint64_t idx = total_;
      cf y, rx, ideal;
      bool inst = false;
      int kind = 0;
      cf xn(0), rotFo(1), rotPn(1), nAvg(0);
      if (isLive) {
        y = liveBlk_[i];
        rx = y;
        inst = (idx % (uint64_t)sps_ == 0);
      } else if (fileMode) {
        y = fileBlk_[i];
        rx = y;
        inst = (idx % (uint64_t)sps_ == 0);
      } else {
        xn = NextTx(p, fs, inst, ideal, kind);
        cf xs = xn * G;
        Ps_ += 2e-4f * (std::norm(xs) - Ps_);
        foRot_ *= foStep;
        rotFo = foRot_;
        if (pnStd > 0.f) {
          pnPh_ += nd_(rng_) * pnStd;
          if (pnPh_ > kPi)
            pnPh_ -= k2Pi;
          if (pnPh_ < -kPi)
            pnPh_ += k2Pi;
          rotPn = std::polar(1.0f, pnPh_);
        }
        y = xs * rotFo * rotPn;
        cf n(0);
        if (p.awgn) {
          float var = (kind == 2 ? Ps_ / 0.8125f : Ps_ * (float)sps_) / esn0;
          float sg = sqrtf(std::max(var, 0.f) * 0.5f);
          n = cf(nd_(rng_) * sg, nd_(rng_) * sg);
        }
        y += n;
        nsum_ += n - nhist_[nh_];
        nhist_[nh_] = n;
        nh_ = (nh_ + 1) % sps_;
        nAvg = nsum_ / (float)sps_;
        rx = p.afc ? y * std::conj(rotFo) : y;
        if (p.afc)
          rotFo = cf(1); // for tap below: derotated
      }
      bb_[idx & kRingMask] = y;
      rx_[idx & kRingMask] = rx;
      total_++;
      if (!fileMode && !isLive)
        ++simCount_;
      if (inst) {
        lastSym_ = idx;
        if (fileMode || isLive) {
          symPw_ += 0.02f * (std::norm(y) - symPw_);
          cf t = y / std::max(1e-4f, sqrtf(symPw_));
          PushConst(t);
          if (!cons_.empty()) {
            cf best = cons_[0];
            float bd = 1e9f;
            for (auto &c : cons_) {
              float d = std::norm(t - c);
              if (d < bd) {
                bd = d;
                best = c;
              }
            }
            EvmAcc(t, best);
          }
        } else if (kind == 1) {
          cf t = xn * rotPn * (p.afc ? cf(1) : rotFo) + nAvg / G;
          PushConst(t);
          EvmAcc(t, ideal);
        } else if (kind == 0) {
          PushConst(rx / G);
        } else { // OFDM demod of the data part (CP removed)
          for (int n = 0; n < kOfdmN; ++n)
            ofdmRx_[n] = rx_[(idx - (kOfdmN - 1) + n) & kRingMask];
          fft64_.Forward(ofdmRx_.data());
          for (int k = -26; k <= 26; ++k)
            if (k) {
              int b = (k + kOfdmN) % kOfdmN;
              cf Y = ofdmRx_[b] * 0.125f / G;
              PushConst(Y);
              EvmAcc(Y, ofdmX_[b]);
            }
        }
      }
    }
    // keep the rotators numerically unit-length
    float m = std::abs(foRot_);
    if (m > 0.f)
      foRot_ /= m;
  }

  // ---------------- snapshot ----------------
  void BuildSnapshot(const Params &p, float load) {
    Snapshot &s = work_;
    const bool isLive = (p.sourceMode == SourceMode::LiveSdr) && sdrActive_;
    const float fs = isLive ? (p.sdrSampleRateMsps * 1e6f) : (p.sampleRateMsps * 1e6f);
    const uint64_t end = total_, m = kRingMask;
    s.sourceMode = p.sourceMode;
    s.sdrActive = sdrActive_;
    s.sdrStatus = sdrStatus_;
    s.fs = fs;
    s.cfHz = isLive ? (p.sdrRxFreqMhz * 1e6f) : (p.centerFreqMhz * 1e6f);
    s.sps = sps_;
    s.fftN = fftN_;
    s.mod = mod_;
    s.dspLoad = load;
    s.fileMode = fileMode_;

    // time domain with rising-edge trigger on I
    uint64_t start = end - kTimeN;
    for (uint64_t k = end - 8192; k < end - kTimeN - 1; ++k)
      if (bb_[k & m].real() < p.triggerLevel &&
          bb_[(k + 1) & m].real() >= p.triggerLevel) {
        start = k;
        break;
      }
    for (int i = 0; i < kTimeN; ++i) {
      cf v = bb_[(start + i) & m];
      s.timeI[i] = v.real();
      s.timeQ[i] = v.imag();
    }

    // spectrum (carrier offset applied as digital mixer)
    {
      const int N = fftN_;
      const float lim = 0.5f * fs / 1e3f;
      cf step = std::polar(1.0f, k2Pi * std::clamp(p.carrierKHz, -lim, lim) *
                                     1e3f / fs),
         rot(1);
      for (int i = 0; i < N; ++i) {
        fftBuf_[i] = bb_[(end - N + i) & m] * (win_[i]) * rot;
        rot *= step;
      }
      fft_.Forward(fftBuf_.data());
      bool clr = clearPeak.exchange(false) || !p.peakHold;
      for (int k = 0; k < N; ++k) {
        float db =
            20.f * log10f(std::abs(fftBuf_[(k + N / 2) % N]) / sumW_ + 1e-9f);
        psd_[k] = psdInit_ ? psd_[k] + 0.4f * (db - psd_[k]) : db;
        peak_[k] = clr ? psd_[k] : std::max(peak_[k] - 0.03f, db);
      }
      psdInit_ = true;
      std::copy(psd_.begin(), psd_.begin() + N, s.psd.begin());
      std::copy(peak_.begin(), peak_.begin() + N, s.peak.begin());
      if (end != lastWfTotal_) { // new waterfall row (max-pooled)
        lastWfTotal_ = end;
        float *row = &wfRows_[wfHead_ * kWfCols];
        for (int c = 0; c < kWfCols; ++c) {
          int lo = c * N / kWfCols,
              hi = std::max(lo + 1, (c + 1) * N / kWfCols);
          float mx = -200.f;
          for (int k = lo; k < hi; ++k)
            mx = std::max(mx, psd_[k]);
          row[c] = mx;
        }
        wfHead_ = (wfHead_ + 1) % kWfRows;
      }
      for (int r = 0; r < kWfRows; ++r)
        std::memcpy(
            &s.wf[r * kWfCols],
            &wfRows_[((wfHead_ - 1 - r + 2 * kWfRows) % kWfRows) * kWfCols],
            kWfCols * sizeof(float));
    }

    // constellation (oldest -> newest)
    s.constCount = constCount_;
    for (int i = 0; i < constCount_; ++i) {
      int src = (constHead_ + kConstN - constCount_ + i) % kConstN;
      s.constI[i] = constI_[src];
      s.constQ[i] = constQ_[src];
    }
    s.idealCount = (int)std::min<size_t>(cons_.size(), 16);
    for (int i = 0; i < s.idealCount; ++i) {
      s.idealI[i] = cons_[i].real();
      s.idealQ[i] = cons_[i].imag();
    }
    s.evmValid = haveEvm_ && mod_ != Mod::FM;
    s.evmPct = 100.f * sqrtf(std::max(evm2_, 0.f));
    if (s.evmValid)
      s.snrDb = -10.f * log10f(std::max(evm2_, 1e-9f));
    else if (p.awgn && !fileMode_)
      s.snrDb =
          10.f *
          log10f(powf(10.f, (p.ebn0Db + 10.f * log10f((float)bps_)) / 10.f) /
                 sps_);
    else
      s.snrDb = NAN;

    // eye: 2-symbol windows aligned to the last symbol instant
    const int L = 2 * sps_ + 1;
    s.eyeLen = L;
    s.eyeCount = kEyeTraces;
    const float inv = 1.f / std::max(1e-6f, normGain_);
    for (int j = 0; j < kEyeTraces; ++j) {
      uint64_t st = lastSym_ - (uint64_t)(j + 2) * sps_;
      for (int i = 0; i < L; ++i)
        s.eye[j * kMaxEyeLen + i] = rx_[(st + i) & m].real() * inv;
    }
    if (fileMode_) {
      s.posSec = filePos_ / (double)fs;
      s.totalSec = fileTotal_ / (double)fs;
    } else {
      s.posSec = simCount_ / (double)fs;
      s.totalSec = 0;
    }
    s.seq = ++seq_;
    {
      std::lock_guard<std::mutex> l(sm_);
      std::swap(shared_, s);
    }
  }

  // ---------------- state ----------------
  std::thread th_;
  std::atomic<bool> running_{false};
  std::mutex pm_, sm_, lm_, im_;
  Params params_;
  Snapshot shared_, work_;
  uint64_t seq_ = 0;
  std::atomic<bool> loadReq_{false};
  std::string pendPath_;
  FileFmt pendFmt_ = FileFmt::CF32;
  FileInfo info_;
  std::atomic<uint32_t> infoSeq_{0};
  // rings
  std::vector<cf> bb_, rx_, fftBuf_, fileBlk_, liveBlk_;
  std::vector<uint8_t> raw_;
  std::vector<int16_t> sdrRawBuf_;
  SoapySDRDevice *sdr_ = nullptr;
  SoapySDRStream *rxStream_ = nullptr;
  bool sdrActive_ = false;
  std::string sdrStatus_ = "Disconnected";
  std::atomic<bool> sdrStartReq_{false};
  std::atomic<bool> sdrStopReq_{false};
  std::string sdrReqUri_;
  double sdrReqFreq_{750e6}, sdrReqRate_{1e6}, sdrReqGain_{30.0};
  std::mutex sdrReqMutex_;
  uint64_t total_ = 0, simCount_ = 0, lastSym_ = kRing, lastWfTotal_ = 0;
  // config
  Mod mod_ = Mod::QPSK;
  int sps_ = 20, bps_ = 2, fftN_ = 0;
  float alpha_ = 0.35f;
  std::vector<float> taps_, win_;
  float sumW_ = 1;
  std::vector<cf> cons_;
  Fft fft_, fft64_;
  // tx state
  std::array<cf, kSpan> symHist_{};
  int symPhase_ = 0, cnt_ = 0;
  float msg1_ = 0, msg2_ = 0, fmPh_ = 0;
  std::array<cf, kOfdmN + kOfdmCp> ofdmTx_{};
  std::array<cf, kOfdmN> ofdmX_{}, ofdmRx_{};
  int ofdmPos_ = 0;
  // channel state
  std::mt19937 rng_{12345};
  std::normal_distribution<float> nd_{0.f, 1.f};
  cf foRot_{1.f, 0.f};
  float pnPh_ = 0, Ps_ = 0.05f;
  std::array<cf, kMaxSps> nhist_{};
  cf nsum_{0.f, 0.f};
  int nh_ = 0;
  // metrics / display state
  float evm2_ = 0.01f, symPw_ = 1.f, normGain_ = 0.5f;
  bool haveEvm_ = false, fileMode_ = false;
  float constI_[kConstN]{}, constQ_[kConstN]{};
  int constHead_ = 0, constCount_ = 0;
  std::vector<float> psd_, peak_, wfRows_;
  bool psdInit_ = false;
  int wfHead_ = 0;
  // file
  std::ifstream file_;
  uint64_t fileBytes_ = 0, fileTotal_ = 0, filePos_ = 0;
  int fileBps_ = 8;
};

// ============================================================================
//  UI helpers / theme
// ============================================================================
constexpr ImVec4 Hex(uint32_t rgb, float a = 1.f) {
  return ImVec4(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f,
                (rgb & 255) / 255.f, a);
}
namespace col {
constexpr ImVec4 bg = Hex(0x121316), card = Hex(0x1A1C22),
                 border = Hex(0x2A2D37), muted = Hex(0x8E929F),
                 text = Hex(0xF0F2F5), cyan = Hex(0x00E5FF),
                 amber = Hex(0xFFB300), coral = Hex(0xFF5252),
                 green = Hex(0x00E676);
}
inline ImFont *g_ui = nullptr;
inline ImFont *g_mono = nullptr;
inline void PushMono() {
  if (!g_mono)
    return;
#if IMGUI_VERSION_NUM >= 19200
  ImGui::PushFont(g_mono, 0.0f);
#else
  ImGui::PushFont(g_mono);
#endif
}
inline void PopMono() {
  if (g_mono)
    ImGui::PopFont();
}
inline ImVec4 A(ImVec4 c, float a) {
  c.w = a;
  return c;
}
inline ImVec2 Off(ImVec2 p, float dx, float dy) {
  return ImVec2(p.x + dx, p.y + dy);
}

inline void ApplyModernTheme() {
  ImGuiStyle &s = ImGui::GetStyle();
  s.WindowRounding = 6.f;
  s.ChildRounding = 6.f;
  s.FrameRounding = 4.f;
  s.GrabRounding = 4.f;
  s.PopupRounding = 6.f;
  s.TabRounding = 4.f;
  s.ScrollbarRounding = 6.f;
  s.WindowPadding = ImVec2(12, 12);
  s.ItemSpacing = ImVec2(8, 8);
  s.FramePadding = ImVec2(8, 5);
  s.WindowBorderSize = 1.f;
  s.ChildBorderSize = 1.f;
  s.PopupBorderSize = 1.f;
  s.FrameBorderSize = 0.f;
  s.ScrollbarSize = 12.f;
  s.GrabMinSize = 12.f;
  s.DockingSeparatorSize = 2.f;
  s.WindowMenuButtonPosition = ImGuiDir_None;
  ImVec4 *c = s.Colors;
  c[ImGuiCol_Text] = col::text;
  c[ImGuiCol_TextDisabled] = col::muted;
  c[ImGuiCol_WindowBg] = col::card;
  c[ImGuiCol_ChildBg] = Hex(0x16181D);
  c[ImGuiCol_PopupBg] = Hex(0x1F2229);
  c[ImGuiCol_Border] = col::border;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = col::bg;
  c[ImGuiCol_FrameBgHovered] = Hex(0x20232B);
  c[ImGuiCol_FrameBgActive] = Hex(0x262A33);
  c[ImGuiCol_TitleBg] = col::card;
  c[ImGuiCol_TitleBgActive] = col::card;
  c[ImGuiCol_TitleBgCollapsed] = col::card;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = col::border;
  c[ImGuiCol_ScrollbarGrabHovered] = Hex(0x3A3E4B);
  c[ImGuiCol_ScrollbarGrabActive] = Hex(0x4A4F5E);
  c[ImGuiCol_CheckMark] = col::cyan;
  c[ImGuiCol_SliderGrab] = col::cyan;
  c[ImGuiCol_SliderGrabActive] = Hex(0x7AF1FF);
  c[ImGuiCol_Button] = Hex(0x232630);
  c[ImGuiCol_ButtonHovered] = Hex(0x2C303B);
  c[ImGuiCol_ButtonActive] = Hex(0x353A47);
  c[ImGuiCol_Header] = Hex(0x20232B);
  c[ImGuiCol_HeaderHovered] = Hex(0x2A2E39);
  c[ImGuiCol_HeaderActive] = Hex(0x323744);
  c[ImGuiCol_Separator] = col::border;
  c[ImGuiCol_SeparatorHovered] = col::cyan;
  c[ImGuiCol_SeparatorActive] = col::cyan;
  c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ResizeGripHovered] = A(col::cyan, .4f);
  c[ImGuiCol_ResizeGripActive] = A(col::cyan, .7f);
  c[ImGuiCol_Tab] = col::card;
  c[ImGuiCol_TabHovered] = Hex(0x2A2E39);
  c[ImGuiCol_TabSelected] = Hex(0x232630);
  c[ImGuiCol_TabDimmed] = col::card;
  c[ImGuiCol_TabDimmedSelected] = Hex(0x20232B);
  c[ImGuiCol_DockingPreview] = A(col::cyan, .3f);
  c[ImGuiCol_DockingEmptyBg] = col::bg;
  c[ImGuiCol_TextSelectedBg] = A(col::cyan, .3f);

  ImPlotStyle &ps = ImPlot::GetStyle();
  ps.PlotBorderSize = 1.f;
  ps.LineWeight = 1.5f;
  ps.PlotPadding = ImVec2(8, 8);
  ps.LegendPadding = ImVec2(8, 6);
  ImVec4 *p = ps.Colors;
  p[ImPlotCol_FrameBg] = ImVec4(0, 0, 0, 0);
  p[ImPlotCol_PlotBg] = col::bg;
  p[ImPlotCol_PlotBorder] = col::border;
  p[ImPlotCol_LegendBg] = A(col::card, .9f);
  p[ImPlotCol_LegendBorder] = col::border;
  p[ImPlotCol_LegendText] = col::text;
  p[ImPlotCol_TitleText] = col::text;
  p[ImPlotCol_InlayText] = col::muted;
  p[ImPlotCol_AxisText] = col::muted;
  p[ImPlotCol_AxisGrid] = A(col::border, .7f);
  p[ImPlotCol_AxisTick] = A(col::border, .9f);
  p[ImPlotCol_AxisBg] = ImVec4(0, 0, 0, 0);
  p[ImPlotCol_AxisBgHovered] = A(col::cyan, .08f);
  p[ImPlotCol_AxisBgActive] = A(col::cyan, .15f);
  p[ImPlotCol_Selection] = col::amber;
  p[ImPlotCol_Crosshairs] = A(col::muted, .6f);
}

inline bool FileExists(const char *path) {
  std::ifstream f(path, std::ios::binary);
  return (bool)f;
}

inline void Label(const char *t) { ImGui::TextColored(col::muted, "%s", t); }
inline bool SliderRow(const char *label, float *v, float lo, float hi,
                      const char *fmt, ImGuiSliderFlags fl = 0) {
  Label(label);
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::PushID(label);
  PushMono();
  bool c = ImGui::SliderFloat("##s", v, lo, hi, fmt, fl);
  PopMono();
  ImGui::PopID();
  return c;
}
inline bool Segmented(const char *const *items, int n, int *cur) {
  bool changed = false;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 6));
  float w = (ImGui::GetContentRegionAvail().x - 4.f * (n - 1)) / n;
  for (int i = 0; i < n; ++i) {
    if (i)
      ImGui::SameLine();
    bool sel = (*cur == i);
    if (sel) {
      ImGui::PushStyleColor(ImGuiCol_Button, A(col::cyan, .16f));
      ImGui::PushStyleColor(ImGuiCol_Text, col::cyan);
      ImGui::PushStyleColor(ImGuiCol_Border, col::cyan);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    }
    ImGui::PushID(i);
    if (ImGui::Button(items[i], ImVec2(w, 0))) {
      *cur = i;
      changed = true;
    }
    ImGui::PopID();
    if (sel) {
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(3);
    }
  }
  ImGui::PopStyleVar(2);
  return changed;
}
inline void FmtTime(char *b, size_t n, double s) {
  int m = (int)(s / 60);
  std::snprintf(b, n, "%02d:%06.3f", m, s - m * 60.0);
}
inline void FmtBytes(char *b, size_t n, uint64_t v) {
  if (v > (1ull << 30))
    std::snprintf(b, n, "%.2f GB", v / 1073741824.0);
  else if (v > (1ull << 20))
    std::snprintf(b, n, "%.1f MB", v / 1048576.0);
  else
    std::snprintf(b, n, "%.1f kB", v / 1024.0);
}
inline ImFont *MonoFont() { return g_mono ? g_mono : ImGui::GetFont(); }
inline float BadgeWidth(const char *label, const char *value) {
  float fs = ImGui::GetFontSize();
  return 10.f + ImGui::CalcTextSize(label).x + 6.f +
         MonoFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, value).x + 10.f;
}
inline void Badge(const char *label, const char *value, ImVec4 valCol) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  float h = ImGui::GetFrameHeight(), w = BadgeWidth(label, value),
        fs = ImGui::GetFontSize(), ty = p.y + (h - fs) * .5f;
  dl->AddRectFilled(p, Off(p, w, h), ImGui::GetColorU32(col::bg), 5.f);
  dl->AddRect(p, Off(p, w, h), ImGui::GetColorU32(col::border), 5.f);
  dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + 10, ty),
              ImGui::GetColorU32(col::muted), label);
  dl->AddText(MonoFont(), fs,
              ImVec2(p.x + 10 + ImGui::CalcTextSize(label).x + 6, ty),
              ImGui::GetColorU32(valCol), value);
  ImGui::Dummy(ImVec2(w, h));
}

// ============================================================================
//  Application (UI thread)
// ============================================================================
class App {
public:
  static constexpr float kBarH = 48.f;

  void LoadFonts(ImGuiIO &io, const char *ui = "fonts/Inter-Regular.ttf",
                 const char *mono = "fonts/JetBrainsMono-Regular.ttf",
                 float px = 15.f) {
    g_ui = FileExists(ui) ? io.Fonts->AddFontFromFileTTF(ui, px)
                          : io.Fonts->AddFontDefault();
    g_mono = FileExists(mono) ? io.Fonts->AddFontFromFileTTF(mono, px - 1.f)
                              : nullptr;
  }
  void Init() {
    ApplyModernTheme();
    engine_.SetParams(ui_);
  }
  void OpenFile(const std::string &path, FileFmt fmt = FileFmt::CS16) {
    ui_.sourceMode = SourceMode::File;
    ui_.useFile = true;
    ui_.fileFmt = fmt;
    std::strncpy(path_, path.c_str(), sizeof(path_) - 1);
    engine_.RequestLoad(path, fmt);
    engine_.SetParams(ui_);
  }
  void StartLiveSdr() {
    ui_.sourceMode = SourceMode::LiveSdr;
    engine_.RequestSdrStart(ui_.sdrUri, ui_.sdrRxFreqMhz * 1e6, ui_.sdrSampleRateMsps * 1e6, ui_.sdrGainDb);
    engine_.SetParams(ui_);
  }
  void StopLiveSdr() {
    engine_.RequestSdrStop();
  }
  void Start() { engine_.Start(); }
  void Stop() { engine_.Stop(); }

  void Render() {
    if (!frozen_)
      engine_.Fetch(snap_);
    if (engine_.InfoSeq() != infoSeq_) {
      info_ = engine_.GetInfo();
      infoSeq_ = engine_.InfoSeq();
    }
    if (snap_.fs != lastFs_ || snap_.cfHz != lastCf_) {
      bandDirty_ = true;
      lastFs_ = snap_.fs;
      lastCf_ = snap_.cfHz;
    }
    RenderTopBar();
    BeginDockHost();
    RenderControls();
    RenderPlots();
    RenderRightPanel();
    engine_.SetParams(ui_);
  }

private:
  // --------------------------------------------------------------- top bar
  void RenderTopBar() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, kBarH));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(12, (kBarH - ImGui::GetFrameHeight()) * .5f - 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::card);
    ImGui::Begin("##TopBar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    const Snapshot &s = snap_;
    ImGui::AlignTextToFramePadding();

    // source status
    if (s.sourceMode == SourceMode::LiveSdr) {
      if (s.sdrActive) {
        ImGui::TextColored(col::green, "LIVE RX");
        ImGui::SameLine();
        ImGui::TextColored(col::text, "(Pluto SDR)");
      } else {
        ImGui::TextColored(col::coral, "%s", s.sdrStatus.c_str());
      }
    } else if (s.sourceMode == SourceMode::File) {
      if (info_.loaded) {
        static const char *fm[] = {"CF32", "Int16", "CU8"};
        char sz[32];
        FmtBytes(sz, sizeof sz, info_.bytes);
        ImGui::TextUnformatted(info_.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(col::muted, "%s | %s", fm[(int)info_.fmt], sz);
      } else if (!info_.error.empty()) {
        ImGui::TextColored(col::coral, "%s", info_.error.c_str());
      } else {
        ImGui::TextColored(col::muted, "No file loaded");
      }
    } else {
      ImGui::TextColored(col::cyan, "SIMULATION");
      ImGui::SameLine();
      static const char *mNames[] = {"BPSK", "QPSK", "16-QAM", "FM", "OFDM"};
      ImGui::TextColored(col::muted, "%s", mNames[(int)s.mod]);
    }
    ImGui::SameLine(0, 18);

    // transport
    bool play = engine_.playing.load();
    ImGui::PushStyleColor(ImGuiCol_Button, play ? col::green : Hex(0x232630));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          play ? Hex(0x33EE8F) : Hex(0x2C303B));
    ImGui::PushStyleColor(ImGuiCol_Text, play ? Hex(0x07260F) : col::text);
    if (ImGui::Button(play ? "Pause" : "Play", ImVec2(68, 0)))
      engine_.playing = !play;
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::BeginDisabled(play);
    if (ImGui::Button("Step"))
      engine_.stepReq++;
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool lp = engine_.loop.load();
    if (lp) {
      ImGui::PushStyleColor(ImGuiCol_Button, A(col::amber, .2f));
      ImGui::PushStyleColor(ImGuiCol_Text, col::amber);
    }
    if (ImGui::Button("Loop"))
      engine_.loop = !lp;
    if (lp)
      ImGui::PopStyleColor(2);
    ImGui::SameLine();

    // KPI strings
    char sr[32], cfq[32], snr[32], fps[48];
    std::snprintf(sr, sizeof sr, "%.3f MSPS", s.fs / 1e6f);
    std::snprintf(cfq, sizeof cfq, "%.3f MHz", s.cfHz / 1e6f);
    if (std::isnan(s.snrDb))
      std::snprintf(snr, sizeof snr, "--");
    else
      std::snprintf(snr, sizeof snr, "%.1f dB", s.snrDb);
    std::snprintf(fps, sizeof fps, "%3.0f / %2.0f%%", ImGui::GetIO().Framerate,
                  s.dspLoad * 100.f);
    const char *kl[] = {"SR", "CF", "SNR", "FPS/DSP"};
    const char *kv[] = {sr, cfq, snr, fps};
    float kpiW = 0;
    for (int i = 0; i < 4; ++i)
      kpiW += BadgeWidth(kl[i], kv[i]) + 8.f;
    char tp[24], tt[24], tm[64];
    FmtTime(tp, sizeof tp, s.posSec);
    if (s.fileMode)
      FmtTime(tt, sizeof tt, s.totalSec);
    else
      std::snprintf(tt, sizeof tt, "  LIVE SIM");
    std::snprintf(tm, sizeof tm, "%s / %s", tp, tt);
    PushMono();
    float timeW = ImGui::CalcTextSize(tm).x;
    PopMono();

    float sw = std::max(80.f, ImGui::GetWindowWidth() -
                                  ImGui::GetStyle().WindowPadding.x -
                                  ImGui::GetCursorPosX() - timeW - kpiW - 28.f);
    ImGui::BeginDisabled(!s.fileMode);
    float cur = (float)s.posSec, tot = (float)std::max(s.totalSec, 1e-6);
    ImGui::SetNextItemWidth(sw);
    if (ImGui::SliderFloat("##scrub", &cur, 0.f, tot, "")) {
      engine_.seekSec = cur;
      engine_.seekReq = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    PushMono();
    ImGui::TextUnformatted(tm);
    PopMono();
    ImGui::SameLine(0, 16);
    for (int i = 0; i < 4; ++i) {
      ImVec4 c = col::text;
      if (i == 2 && !std::isnan(s.snrDb))
        c = s.snrDb > 15.f  ? col::green
            : s.snrDb > 8.f ? col::amber
                            : col::coral;
      if (i == 3)
        c = s.dspLoad > .85f ? col::coral : col::cyan;
      Badge(kl[i], kv[i], c);
      ImGui::SameLine(0, 8);
    }
    if (frozen_) {
      ImGui::SameLine(0, 4);
      ImGui::TextColored(col::coral, "PAUSED");
    }
    ImGui::End();
  }

  // --------------------------------------------------------------- dock host
  void BeginDockHost() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImVec2 pos(vp->WorkPos.x, vp->WorkPos.y + kBarH),
        size(vp->WorkSize.x, vp->WorkSize.y - kBarH);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::bg);
    ImGui::Begin("##DockHost", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImGuiID id = ImGui::GetID("MainDock");
    if (!ImGui::DockBuilderGetNode(id)) {
      ImGui::DockBuilderRemoveNode(id);
      ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(id, size);
      ImGuiID rest, center,
          left = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 280.f / size.x,
                                             nullptr, &rest);
      ImGuiID right = ImGui::DockBuilderSplitNode(
          rest, ImGuiDir_Right, 320.f / (size.x - 280.f), nullptr, &center);
      ImGui::DockBuilderDockWindow("DSP Controls", left);
      ImGui::DockBuilderDockWindow("Signal Viewport", center);
      ImGui::DockBuilderDockWindow("Constellation / Eye", right);
      ImGui::DockBuilderFinish(id);
    }
    ImGui::DockSpace(id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
  }

  // --------------------------------------------------------------- left
  // sidebar
  void RenderControls() {
    ImGui::Begin("DSP Controls");
    Params &p = ui_;
    const float fsK = snap_.fs / 1e3f;

    // ---------------- Acquisition Source Selector ----------------
    if (ImGui::CollapsingHeader("Signal Source Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
      static const char *srcModes[] = {"Live SDR (RX)", "File Player", "Simulation"};
      int curMode = (int)p.sourceMode;
      if (Segmented(srcModes, 3, &curMode)) {
        p.sourceMode = (SourceMode)curMode;
      }
      ImGui::Spacing();

      if (p.sourceMode == SourceMode::LiveSdr) {
        Label("Pluto URI");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##sdr_uri", p.sdrUri, sizeof p.sdrUri);
        if (ImGui::Button("usb:1.11.5", ImVec2(80, 0))) {
          std::strncpy(p.sdrUri, "usb:1.11.5", sizeof(p.sdrUri) - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("usb:1.13.5", ImVec2(80, 0))) {
          std::strncpy(p.sdrUri, "usb:1.13.5", sizeof(p.sdrUri) - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("192.168.2.1", ImVec2(90, 0))) {
          std::strncpy(p.sdrUri, "ip:192.168.2.1", sizeof(p.sdrUri) - 1);
        }

        Label("RX Center Frequency");
        ImGui::SetNextItemWidth(-FLT_MIN);
        PushMono();
        ImGui::DragFloat("##sdr_cf", &p.sdrRxFreqMhz, 0.1f, 70.0f, 6000.0f, "%.3f MHz");
        PopMono();

        Label("RX Sample Rate");
        ImGui::SetNextItemWidth(-FLT_MIN);
        PushMono();
        ImGui::DragFloat("##sdr_sr", &p.sdrSampleRateMsps, 0.05f, 0.521f, 20.0f, "%.3f MSPS");
        PopMono();

        SliderRow("RX Gain", &p.sdrGainDb, 0.0f, 73.0f, "%.1f dB");

        ImGui::Spacing();
        if (!snap_.sdrActive) {
          ImGui::PushStyleColor(ImGuiCol_Button, Hex(0x1B5E20));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hex(0x2E7D32));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, Hex(0x388E3C));
          if (ImGui::Button("START LIVE RX STREAM", ImVec2(-FLT_MIN, 32))) {
            StartLiveSdr();
          }
          ImGui::PopStyleColor(3);
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button, Hex(0xB71C1C));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hex(0xC62828));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, Hex(0xD32F2F));
          if (ImGui::Button("STOP LIVE RX STREAM", ImVec2(-FLT_MIN, 32))) {
            StopLiveSdr();
          }
          ImGui::PopStyleColor(3);
        }
        ImGui::TextColored(snap_.sdrActive ? col::green : col::muted,
                           "Status: %s", snap_.sdrStatus.c_str());
      } else if (p.sourceMode == SourceMode::File) {
        Label("File Path");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##path", path_, sizeof path_);
        if (ImGui::Button("Rxdata.pcm", ImVec2(90, 0))) {
          std::strncpy(path_, "Rxdata.pcm", sizeof(path_) - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Txdata.pcm", ImVec2(90, 0))) {
          std::strncpy(path_, "Txdata.pcm", sizeof(path_) - 1);
        }

        static const char *ff[] = {"CF32 (complex float)",
                                   "Int16 / CS16 (interleaved IQ)", "CU8 (RTL-SDR)"};
        int f = (int)p.fileFmt;
        Label("Format");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##fmt", &f, ff, 3))
          p.fileFmt = (FileFmt)f;

        if (ImGui::Button("Load File", ImVec2(-FLT_MIN, 28))) {
          engine_.RequestLoad(path_, p.fileFmt);
        }

        Label("Sample Rate");
        ImGui::SetNextItemWidth(-FLT_MIN);
        PushMono();
        ImGui::DragFloat("##sr", &p.sampleRateMsps, 0.01f, 0.1f, 20.f, "%.3f MSPS");
        PopMono();

        Label("Center Frequency");
        ImGui::SetNextItemWidth(-FLT_MIN);
        PushMono();
        ImGui::DragFloat("##cf", &p.centerFreqMhz, 0.01f, 0.1f, 6000.f, "%.3f MHz");
        PopMono();
      } else {
        ImGui::TextColored(col::cyan, "Generating synthetic modulated signals.");
      }
    }

    static const char *mods[] = {"BPSK", "QPSK", "16-QAM", "FM", "OFDM"};
    if (p.sourceMode == SourceMode::Sim) {
      if (ImGui::CollapsingHeader("Modulation Scheme",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        int m = (int)p.mod;
        if (Segmented(mods, 5, &m))
          p.mod = (Mod)m;
      }
      if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        SliderRow("Carrier frequency offset", &p.carrierKHz, -0.5f * fsK,
                  0.5f * fsK, "%+.1f kHz");
        SliderRow("Symbol rate", &p.symbolRateKsps, 10.f, 1000.f, "%.1f ksym/s",
                  ImGuiSliderFlags_Logarithmic);
        ImGui::BeginDisabled(p.mod == Mod::FM || p.mod == Mod::OFDM);
        SliderRow("Roll-off factor (alpha)", &p.alpha, 0.05f, 1.f, "%.2f");
        ImGui::EndDisabled();
        if (p.mod == Mod::FM)
          SliderRow("FM deviation", &p.fmDevKHz, 1.f, 200.f, "%.1f kHz");
        SliderRow("Gain", &p.gainDb, -40.f, 6.f, "%+.1f dB");
      }
      if (ImGui::CollapsingHeader("Channel Impairments",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("AWGN noise", &p.awgn);
        ImGui::BeginDisabled(!p.awgn);
        SliderRow("Eb/N0", &p.ebn0Db, 0.f, 40.f, "%.1f dB");
        ImGui::EndDisabled();
        ImGui::Checkbox("Frequency offset", &p.freqOff);
        ImGui::BeginDisabled(!p.freqOff);
        SliderRow("Offset", &p.freqOffKHz, -20.f, 20.f, "%+.2f kHz");
        ImGui::EndDisabled();
        ImGui::Checkbox("Phase noise", &p.phaseNoise);
        ImGui::BeginDisabled(!p.phaseNoise);
        SliderRow("Jitter (rms / symbol)", &p.phaseNoiseDeg, 0.f, 8.f,
                  "%.2f deg");
        ImGui::EndDisabled();
        ImGui::Checkbox("Receiver AFC (derotate offset)", &p.afc);
      }
    }

    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
      static const char *fftL[] = {"1024", "2048", "4096", "8192", "16384"};
      static const int fftV[] = {1024, 2048, 4096, 8192, 16384};
      int fi = 0;
      for (int i = 0; i < 5; ++i)
        if (fftV[i] == p.fftSize)
          fi = i;
      Label("FFT size");
      ImGui::SetNextItemWidth(-FLT_MIN);
      PushMono();
      if (ImGui::Combo("##fft", &fi, fftL, 5))
        p.fftSize = fftV[fi];
      PopMono();
      ImGui::Checkbox("Peak hold", &p.peakHold);
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear"))
        engine_.clearPeak = true;
      ImGui::Checkbox("Auto-fit time plot", &autoFit_);
      SliderRow("Trigger level", &p.triggerLevel, -1.f, 1.f, "%+.3f");
    }
    ImGui::End();
  }

  // --------------------------------------------------------------- center
  // plots
  void HandleFreezeClick() {
    if (ImPlot::IsPlotHovered() && ImGui::IsMouseReleased(0) &&
        ImLengthSqr(ImGui::GetMouseDragDelta(0)) == 0.f)
      frozen_ = !frozen_;
  }
  void FrozenOverlay() {
    if (!frozen_)
      return;
    ImPlot::GetPlotDrawList()->AddText(Off(ImPlot::GetPlotPos(), 8, 6),
                                       ImGui::GetColorU32(col::coral),
                                       "PAUSED - click plot to resume");
  }

  void RenderPlots() {
    ImGui::Begin("Signal Viewport");
    const Snapshot &s = snap_;
    const float half =
        (ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y) *
        0.5f;
    const double fsMHz = s.fs / 1e6, x0 = (s.cfHz - s.fs / 2) / 1e6,
                 x1 = (s.cfHz + s.fs / 2) / 1e6;
    const ImPlotFlags pf = ImPlotFlags_Crosshairs | ImPlotFlags_NoMouseText;

    // ---- time domain ----
    PushMono();
    if (ImPlot::BeginPlot("Time Domain  |  I / Q", ImVec2(-1, half), pf)) {
      ImPlot::SetupAxes("t (\xC2\xB5s)", "Amplitude", ImPlotAxisFlags_AutoFit,
                        autoFit_ ? ImPlotAxisFlags_AutoFit : 0);
      ImPlot::SetupAxisLimits(ImAxis_Y1, -1.2, 1.2, ImPlotCond_Once);
      ImPlot::SetupLegend(ImPlotLocation_NorthEast);
      const double dt = 1e6 / s.fs;
      ImPlot::SetNextLineStyle(col::cyan, 1.5f);
      ImPlot::PlotLine("I", s.timeI, kTimeN, dt, 0.0);
      ImPlot::SetNextLineStyle(col::amber, 1.5f);
      ImPlot::PlotLine("Q", s.timeQ, kTimeN, dt, 0.0);
      double trig = ui_.triggerLevel;
      if (ImPlot::DragLineY(0, &trig, A(col::amber, .7f), 1.f))
        ui_.triggerLevel = (float)std::clamp(trig, -1.0, 1.0);
      if (ImPlot::IsPlotHovered()) {
        ImPlotPoint m = ImPlot::GetPlotMousePos();
        int i = std::clamp((int)(m.x / dt + .5), 0, kTimeN - 1);
        ImGui::BeginTooltip();
        ImGui::Text("t   = %9.3f us", i * dt);
        ImGui::TextColored(col::cyan, "I   = %+9.5f", s.timeI[i]);
        ImGui::TextColored(col::amber, "Q   = %+9.5f", s.timeQ[i]);
        ImGui::Text("|x| = %9.5f", std::hypot(s.timeI[i], s.timeQ[i]));
        ImGui::Text("ph  = %+8.2f deg",
                    std::atan2(s.timeQ[i], s.timeI[i]) * 57.29578f);
        ImGui::EndTooltip();
      }
      HandleFreezeClick();
      FrozenOverlay();
      ImPlot::EndPlot();
    }

    // ---- PSD + waterfall (subplots keep the frequency axes aligned and
    // linked) ----
    static float rows[2] = {0.6f, 0.4f};
    const ImPlotCond xc = bandDirty_ ? ImPlotCond_Always : ImPlotCond_Once;
    if (ImPlot::BeginSubplots(
            "##spec", 2, 1, ImVec2(-1, half),
            ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoTitle, rows)) {
      if (ImPlot::BeginPlot("Power Spectral Density", ImVec2(0, 0), pf)) {
        ImPlot::SetupAxes(nullptr, "dBFS", ImPlotAxisFlags_NoTickLabels, 0);
        ImPlot::SetupAxisLimits(ImAxis_X1, x0, x1, xc);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -120, 0, ImPlotCond_Once);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        const int N = s.fftN;
        const double df = fsMHz / N, xs = x0;
        ImPlot::SetNextFillStyle(col::cyan, .12f);
        ImPlot::PlotShaded("##fill", s.psd.data(), N, -200.0, df, xs);
        ImPlot::SetNextLineStyle(col::cyan, 1.4f);
        ImPlot::PlotLine("PSD", s.psd.data(), N, df, xs);
        if (ui_.peakHold) {
          ImPlot::SetNextLineStyle(A(col::amber, .85f), 1.f);
          ImPlot::PlotLine("Peak hold", s.peak.data(), N, df, xs);
        }
        if (ImPlot::IsPlotHovered()) {
          ImPlotPoint m = ImPlot::GetPlotMousePos();
          int b = std::clamp((int)((m.x - xs) / df + .5), 0, N - 1);
          ImGui::BeginTooltip();
          ImGui::Text("f    = %12.6f MHz", xs + b * df);
          ImGui::Text("df   = %+10.2f kHz", (xs + b * df - s.cfHz / 1e6) * 1e3);
          ImGui::TextColored(col::cyan, "PSD  = %+8.2f dBFS", s.psd[b]);
          ImGui::TextColored(col::amber, "Peak = %+8.2f dBFS", s.peak[b]);
          ImGui::EndTooltip();
        }
        HandleFreezeClick();
        ImPlot::EndPlot();
      }
      if (ImPlot::BeginPlot("Waterfall", ImVec2(0, 0),
                            pf | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes("Frequency (MHz)", nullptr, 0,
                          ImPlotAxisFlags_NoTickLabels |
                              ImPlotAxisFlags_NoGridLines |
                              ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_X1, x0, x1, xc);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, kWfRows, ImPlotCond_Always);
        ImPlot::PushColormap(ImPlotColormap_Viridis);
        ImPlot::PlotHeatmap("##wf", s.wf.data(), kWfRows, kWfCols, -120.0,
                            -20.0, nullptr, ImPlotPoint(x0, 0),
                            ImPlotPoint(x1, kWfRows));
        ImPlot::PopColormap();
        if (ImPlot::IsPlotHovered()) {
          ImPlotPoint m = ImPlot::GetPlotMousePos();
          int c = (int)((m.x - x0) / (x1 - x0) * kWfCols),
              r = (int)(kWfRows - m.y);
          if (c >= 0 && c < kWfCols && r >= 0 && r < kWfRows) {
            ImGui::BeginTooltip();
            ImGui::Text("f   = %12.6f MHz", m.x);
            ImGui::Text("age = %3d frames", r);
            ImGui::Text("lvl = %+8.2f dBFS", s.wf[r * kWfCols + c]);
            ImGui::EndTooltip();
          }
        }
        ImPlot::EndPlot();
      }
      ImPlot::EndSubplots();
    }
    bandDirty_ = false;
    PopMono();
    ImGui::End();
  }

  // --------------------------------------------------------------- right
  // sidebar
  void RenderRightPanel() {
    ImGui::Begin("Constellation / Eye");
    const Snapshot &s = snap_;
    const ImGuiStyle &st = ImGui::GetStyle();
    ImVec2 av = ImGui::GetContentRegionAvail();
    float inner = av.x - 2.f * st.WindowPadding.x - 2.f;
    float c1h = inner + 2.f * st.WindowPadding.y + 84.f;
    const ImPlotFlags pf = ImPlotFlags_Crosshairs | ImPlotFlags_NoMouseText |
                           ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
                           ImPlotFlags_NoTitle;

    ImGui::BeginChild("##constCard", ImVec2(-1, c1h),
                      ImGuiChildFlags_Borders |
                          ImGuiChildFlags_AlwaysUseWindowPadding);
    Label("IQ CONSTELLATION");
    PushMono();
    if (ImPlot::BeginPlot("##const", ImVec2(inner, inner),
                          pf | ImPlotFlags_Equal)) {
      const ImPlotAxisFlags af = ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus;
      ImPlot::SetupAxes("I", "Q", af, af);
      ImPlot::SetupAxisLimits(ImAxis_X1, -1.5, 1.5, ImPlotCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, -1.5, 1.5, ImPlotCond_Always);
      const int n = s.constCount, nb = 4, per = std::max(1, n / nb);
      const float al[4] = {0.07f, 0.16f, 0.35f, 0.85f};
      static const char *ids[4] = {"##b0", "##b1", "##b2", "##b3"};
      for (int b = 0; b < nb; ++b) {
        int o = b * per, cnt = (b == nb - 1) ? n - o : per;
        if (cnt <= 0)
          continue;
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.2f,
                                   A(col::cyan, al[b]), -1.f,
                                   A(col::cyan, al[b]));
        ImPlot::PlotScatter(ids[b], s.constI + o, s.constQ + o, cnt);
      }
      if (s.idealCount) {
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 8.f, ImVec4(0, 0, 0, 0),
                                   1.5f, A(col::amber, .9f));
        ImPlot::PlotScatter("##ideal", s.idealI, s.idealQ, s.idealCount);
      }
      if (ImPlot::IsPlotHovered()) {
        ImPlotPoint m = ImPlot::GetPlotMousePos();
        ImGui::BeginTooltip();
        ImGui::Text("I = %+7.4f", m.x);
        ImGui::Text("Q = %+7.4f", m.y);
        ImGui::Text("|s| = %6.4f  ph = %+7.2f", std::hypot(m.x, m.y),
                    std::atan2(m.y, m.x) * 57.29578);
        ImGui::EndTooltip();
      }
      ImPlot::EndPlot();
    }
    if (s.evmValid) {
      ImVec4 c = s.evmPct < 8.f    ? col::green
                 : s.evmPct < 20.f ? col::amber
                                   : col::coral;
      ImGui::TextColored(c, "EVM %6.2f %%", s.evmPct);
      ImGui::SameLine();
      ImGui::TextColored(col::muted, " MER %5.1f dB",
                         -20.f * log10f(std::max(s.evmPct / 100.f, 1e-6f)));
    } else
      ImGui::TextColored(col::muted, "EVM    --");
    PopMono();
    ImGui::EndChild();

    float c2h = std::max(140.f, ImGui::GetContentRegionAvail().y);
    ImGui::BeginChild("##eyeCard", ImVec2(-1, c2h),
                      ImGuiChildFlags_Borders |
                          ImGuiChildFlags_AlwaysUseWindowPadding);
    Label("EYE DIAGRAM (I)");
    PushMono();
    if (ImPlot::BeginPlot("##eye", ImVec2(-1, -1), pf)) {
      ImPlot::SetupAxes("t (symbols)", nullptr, ImPlotAxisFlags_Lock,
                        ImPlotAxisFlags_NoMenus);
      ImPlot::SetupAxisLimits(ImAxis_X1, -1, 1, ImPlotCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, -1.6, 1.6, ImPlotCond_Once);
      float xs[kMaxEyeLen];
      const int L = std::min(s.eyeLen, kMaxEyeLen);
      for (int i = 0; i < L; ++i)
        xs[i] = -1.f + 2.f * i / (float)(L - 1);
      const double zero = 0.0;
      ImPlot::SetNextLineStyle(A(col::muted, .5f), 1.f);
      ImPlot::PlotInfLines("##t0", &zero, 1);
      for (int j = 0; j < s.eyeCount; ++j) {
        ImPlot::SetNextLineStyle(A(col::cyan, .22f), 1.f);
        ImPlot::PlotLine("##eye", xs, &s.eye[j * kMaxEyeLen], L);
      }
      if (ImPlot::IsPlotHovered()) {
        ImPlotPoint m = ImPlot::GetPlotMousePos();
        ImGui::BeginTooltip();
        ImGui::Text("t = %+6.3f sym", m.x);
        ImGui::Text("A = %+6.3f", m.y);
        ImGui::EndTooltip();
      }
      ImPlot::EndPlot();
    }
    PopMono();
    ImGui::EndChild();
    ImGui::End();
  }

  // --------------------------------------------------------------- state
  Engine engine_;
  Params ui_;
  Snapshot snap_;
  FileInfo info_;
  uint32_t infoSeq_ = 0;
  bool frozen_ = false, autoFit_ = false, bandDirty_ = true;
  float lastFs_ = 0, lastCf_ = 0;
  char path_[512] = "capture.cf32";
};

} // namespace sdrviz