// Injectable command runner so menu Exec items are testable without spawning a
// real process. Production PosixCommandRunner double-forks/execvps the argv (no
// /bin/sh — argv is built-in, parser-free) and sets WAYLAND_DISPLAY; tests
// install a FakeCommandRunner that just records the argv.
#ifndef BLACKBOXAI_COMMANDRUNNER_HH
#define BLACKBOXAI_COMMANDRUNNER_HH

#include <string>
#include <vector>

namespace bbai {

  class CommandRunner {
  public:
    virtual ~CommandRunner() = default;
    virtual void run(const std::vector<std::string> &argv) = 0;
  };

  class PosixCommandRunner : public CommandRunner {
  public:
    explicit PosixCommandRunner(std::string wayland_display = "")
      : wayland_display_(std::move(wayland_display)) {}
    void run(const std::vector<std::string> &argv) override;
  private:
    std::string wayland_display_;
  };

  class FakeCommandRunner : public CommandRunner {
  public:
    void run(const std::vector<std::string> &argv) override {
      last_ = argv;
      ++count_;
    }
    const std::vector<std::string> &lastCommand() const { return last_; }
    int runCount() const { return count_; }
  private:
    std::vector<std::string> last_;
    int count_ = 0;
  };

} // namespace bbai

#endif // BLACKBOXAI_COMMANDRUNNER_HH
