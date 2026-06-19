// DemoRecorder: captures server frames to $BBAI_DEMO_DIR/<scenario>/NNN.png
// when that env var is set; a no-op otherwise so scenarios run as plain tests
// in CI without dumping any files.
#ifndef BLACKBOXAI_DEMO_RECORDER_HH
#define BLACKBOXAI_DEMO_RECORDER_HH

#include "HeadlessFixture.hh"
#include "Server.hh"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace bbai::test {

class DemoRecorder {
public:
  explicit DemoRecorder(std::string scenario) : scenario_(std::move(scenario)) {
    const char *d = std::getenv("BBAI_DEMO_DIR");
    if (d && *d) dir_ = std::string(d) + "/" + scenario_;
  }

  // Capture the current scene; hold it for `repeat` frames so a state lingers
  // in the output video. No-op when not recording.
  void shot(bbai::Server &server, int repeat = 1) {
    if (dir_.empty()) return;
    Frame f = captureFrame(server);
    for (int i = 0; i < repeat; ++i) {
      char name[32];
      std::snprintf(name, sizeof name, "/%04d.png", n_++);
      writeFramePng(f, dir_ + name);
    }
  }

  bool recording() const { return !dir_.empty(); }

private:
  std::string scenario_, dir_;
  int n_ = 0;
};

} // namespace bbai::test

#endif // BLACKBOXAI_DEMO_RECORDER_HH
