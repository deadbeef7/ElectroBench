#pragma once

// Shared per-scene frame-time statistics and GPU timers.
//
// Average FPS alone hides the stutter old hardware is prone to, and it cannot
// tell a CPU-bound scene from a GPU-bound one. Every scene therefore records
// its frame times and, when the driver supports GL_TIME_ELAPSED queries, the
// time the GPU actually spent executing the frame. The benchmark reports the
// average, the 1% low (the FPS a slow frame delivers) and the p99/p99.9 frame
// times, and `--timings` prints a live per-second dump while the scene runs.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include <GL/glew.h>

extern bool gTimingsVerbose; // set by --timings in main.cxx

namespace framestats {

struct Stats {
  std::vector<float> frames; // seconds per frame

  Stats() { frames.reserve(8192); }

  void Reset() { frames.clear(); }

  void Add(double seconds) {
    if (seconds > 0.0 && seconds < 60.0) frames.push_back((float)seconds);
  }

  bool Empty() const { return frames.empty(); }

  double AverageFps() const {
    double sum = 0.0;
    for (float f : frames) sum += f;
    return sum > 0.0 ? (double)frames.size() / sum : 0.0;
  }

  // Frame time at a percentile of the sorted frame times, in milliseconds.
  double PercentileMs(double p) const {
    if (frames.empty()) return 0.0;
    std::vector<float> sorted = frames;
    std::sort(sorted.begin(), sorted.end());
    double idx = p * (double)(sorted.size() - 1);
    size_t i = (size_t)std::floor(idx);
    double a = sorted[i];
    double b = i + 1 < sorted.size() ? sorted[i + 1] : a;
    return (a + (b - a) * (idx - (double)i)) * 1000.0;
  }

  // The FPS implied by the slowest `fraction` of frames (the usual "1% low":
  // the mean frame time of the worst 1% of the run, inverted).
  double LowFps(double fraction = 0.01) const {
    if (frames.empty()) return 0.0;
    std::vector<float> sorted = frames;
    std::sort(sorted.begin(), sorted.end(), std::greater<float>());
    size_t n = (size_t)std::lround((double)sorted.size() * fraction);
    if (n < 1) n = 1;
    if (n > sorted.size()) n = sorted.size();
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) sum += sorted[i];
    return sum > 0.0 ? (double)n / sum : 0.0;
  }

  void Print(const char *scene) const {
    if (frames.empty()) return;
    std::printf("Frame timing (%s): %zu frames | average %.1f FPS (%.2f ms) | "
                "1%% low %.1f FPS | p99 %.2f ms | p99.9 %.2f ms\n",
                scene, frames.size(), AverageFps(),
                1000.0 / (AverageFps() > 0.0 ? AverageFps() : 1.0),
                LowFps(), PercentileMs(0.99), PercentileMs(0.999));
    std::fflush(stdout);
  }
};

// GL_TIME_ELAPSED timer with a small query ring, so reading a result never
// stalls the frame that is being submitted: results are collected a few frames
// later, once the driver reports them available.
class GpuTimer {
public:
  static const int kRing = 8;

  bool ready = false;
  bool active = false;
  GLuint queries[kRing] = {0};
  int head = 0;
  int pending = 0;
  double lastMs = 0.0;
  double totalMs = 0.0;
  long long samples = 0;

  void Init() {
    if (ready) return;
    if (!GLEW_VERSION_3_3) return;
    glGenQueries(kRing, queries);
    ready = true;
  }

  void Begin() {
    if (!ready || active) return;
    if (pending >= kRing) Collect();
    if (pending >= kRing) return;
    glBeginQuery(GL_TIME_ELAPSED, queries[(head + pending) % kRing]);
    active = true;
  }

  void End() {
    if (!ready || !active) return;
    glEndQuery(GL_TIME_ELAPSED);
    active = false;
    pending++;
  }

  void Collect() {
    while (pending > 0) {
      GLuint id = queries[head % kRing];
      GLint available = 0;
      glGetQueryObjectiv(id, GL_QUERY_RESULT_AVAILABLE, &available);
      if (!available) break;
      GLuint64 ns = 0;
      glGetQueryObjectui64v(id, GL_QUERY_RESULT, &ns);
      head = (head + 1) % kRing;
      pending--;
      double ms = (double)ns / 1.0e6;
      // Some software drivers hand back a clock reading instead of an elapsed
      // interval for the very first query of a context (tens of minutes). No
      // frame of this benchmark can take ten seconds, so drop such a sample
      // instead of letting it poison the average.
      if (ms < 0.0 || ms > 10000.0) continue;
      lastMs = ms;
      totalMs += lastMs;
      samples++;
    }
  }

  double LastMs() {
    Collect();
    return lastMs;
  }

  double AverageMs() {
    Collect();
    return samples > 0 ? totalMs / (double)samples : 0.0;
  }

  long long Samples() {
    Collect();
    return samples;
  }
};

} // namespace framestats
