#include "SniMockItem.hh"

#include <systemd/sd-bus.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace bbai::test {

  const unsigned char kSniMockIcon[16] = {
    0xff, 0xff, 0x00, 0x00,   // red
    0xff, 0x00, 0xff, 0x00,   // green
    0xff, 0x00, 0x00, 0xff,   // blue
    0x80, 0x10, 0x20, 0x30,   // semi-transparent
  };
  const unsigned char kSniMockIcon2[16] = {
    0xff, 0x11, 0x22, 0x33,
    0xff, 0x44, 0x55, 0x66,
    0xff, 0x77, 0x88, 0x99,
    0x40, 0xaa, 0xbb, 0xcc,
  };

  namespace {

    struct MockState {
      sd_bus *bus = nullptr;
      int report_fd = -1;
      const unsigned char *icon = kSniMockIcon;
      const char *status = "Active";
      bool with_menu = false;
    };

    // --- com.canonical.dbusmenu mock (with_menu) ---------------------------
    struct MenuNode {
      int id;
      const char *label = nullptr;      // nullptr -> omitted
      const char *type = nullptr;       // "separator" or nullptr
      int enabled = -1;                 // -1 omit
      int visible = -1;                 // -1 omit
      const char *toggle_type = nullptr;
      int toggle_state = -2;            // -2 omit
      bool submenu = false;
      std::vector<MenuNode> kids;
    };

    // Deep tree: File -> {New, Recent -> {doc1, doc2}}, sep, Notifications(chk),
    // Upgrade(disabled), Secret(hidden). Underscores are mnemonic markers.
    MenuNode menuTree() {
      MenuNode root{0}; root.submenu = true;
      MenuNode file{1, "_File"}; file.submenu = true;
      file.kids.push_back({11, "New"});
      MenuNode recent{12, "R_ecent"}; recent.submenu = true;
      recent.kids.push_back({121, "doc1"});
      recent.kids.push_back({122, "doc2"});
      file.kids.push_back(recent);
      root.kids.push_back(file);
      root.kids.push_back({2, nullptr, "separator"});
      MenuNode chk{3, "Notifications"}; chk.toggle_type = "checkmark"; chk.toggle_state = 1;
      root.kids.push_back(chk);
      MenuNode dis{4, "Upgrade"}; dis.enabled = 0;
      root.kids.push_back(dis);
      MenuNode sec{5, "Secret"}; sec.visible = 0;
      root.kids.push_back(sec);
      return root;
    }

    unsigned g_menu_revision = 1;

    int appendMenuNode(sd_bus_message *reply, const MenuNode &n) {
      int r = sd_bus_message_open_container(reply, 'r', "ia{sv}av");
      if (r < 0) return r;
      if ((r = sd_bus_message_append(reply, "i", n.id)) < 0) return r;
      if ((r = sd_bus_message_open_container(reply, 'a', "{sv}")) < 0) return r;
      auto kv_s = [&](const char *k, const char *v) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", k);
        sd_bus_message_append(reply, "v", "s", v);
        sd_bus_message_close_container(reply);
      };
      auto kv_b = [&](const char *k, int v) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", k);
        sd_bus_message_append(reply, "v", "b", v);
        sd_bus_message_close_container(reply);
      };
      auto kv_i = [&](const char *k, int v) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", k);
        sd_bus_message_append(reply, "v", "i", v);
        sd_bus_message_close_container(reply);
      };
      if (n.label) kv_s("label", n.label);
      if (n.type) kv_s("type", n.type);
      if (n.enabled >= 0) kv_b("enabled", n.enabled);
      if (n.visible >= 0) kv_b("visible", n.visible);
      if (n.toggle_type) kv_s("toggle-type", n.toggle_type);
      if (n.toggle_state != -2) kv_i("toggle-state", n.toggle_state);
      if (n.submenu) kv_s("children-display", "submenu");
      kv_s("x-mock-junk", "skip-me");     // unknown vendor key -> client skips it
      if ((r = sd_bus_message_close_container(reply)) < 0) return r;   // a{sv}
      if ((r = sd_bus_message_open_container(reply, 'a', "v")) < 0) return r;
      for (const MenuNode &k : n.kids) {
        if ((r = sd_bus_message_open_container(reply, 'v', "(ia{sv}av)")) < 0) return r;
        if ((r = appendMenuNode(reply, k)) < 0) return r;
        if ((r = sd_bus_message_close_container(reply)) < 0) return r;
      }
      if ((r = sd_bus_message_close_container(reply)) < 0) return r;   // av
      return sd_bus_message_close_container(reply);                    // r
    }

    int onGetLayout(sd_bus_message *m, void *userdata, sd_bus_error *) {
      auto *st = static_cast<MockState *>(userdata);
      dprintf(st->report_fd, "GetLayout\n");
      sd_bus_message *reply = nullptr;
      int r = sd_bus_message_new_method_return(m, &reply);
      if (r < 0) return r;
      sd_bus_message_append(reply, "u", g_menu_revision);
      MenuNode tree = menuTree();
      appendMenuNode(reply, tree);
      r = sd_bus_send(nullptr, reply, nullptr);
      sd_bus_message_unref(reply);
      return r;
    }
    int onAboutToShow(sd_bus_message *m, void *userdata, sd_bus_error *) {
      auto *st = static_cast<MockState *>(userdata);
      int id = 0; sd_bus_message_read(m, "i", &id);
      dprintf(st->report_fd, "AboutToShow %d\n", id);
      return sd_bus_reply_method_return(m, "b", 0);   // needUpdate=false
    }
    int onMenuEvent(sd_bus_message *m, void *userdata, sd_bus_error *) {
      auto *st = static_cast<MockState *>(userdata);
      int id = 0; const char *ev = nullptr;
      sd_bus_message_read(m, "is", &id, &ev);
      sd_bus_message_skip(m, "v");
      uint32_t ts = 0; sd_bus_message_read(m, "u", &ts);
      dprintf(st->report_fd, "Event %d %s\n", id, ev);
      return sd_bus_reply_method_return(m, "");
    }
    int getMenuVersion(sd_bus *, const char *, const char *, const char *,
                       sd_bus_message *reply, void *, sd_bus_error *) {
      return sd_bus_message_append(reply, "u", 3u);
    }

    const sd_bus_vtable kMenuVtable[] = {
      SD_BUS_VTABLE_START(0),
      SD_BUS_METHOD("GetLayout", "iias", "u(ia{sv}av)", onGetLayout, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("AboutToShow", "i", "b", onAboutToShow, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("Event", "isvu", "", onMenuEvent, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_PROPERTY("Version", "u", getMenuVersion, 0, 0),
      SD_BUS_SIGNAL("LayoutUpdated", "ui", 0),
      SD_BUS_SIGNAL("ItemsPropertiesUpdated", "a(ia{sv})a(ias)", 0),
      SD_BUS_VTABLE_END
    };

    int getIconPixmap(sd_bus *, const char *, const char *, const char *,
                      sd_bus_message *reply, void *userdata, sd_bus_error *) {
      auto *st = static_cast<MockState *>(userdata);
      int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
      if (r < 0) return r;
      r = sd_bus_message_open_container(reply, 'r', "iiay");
      if (r < 0) return r;
      r = sd_bus_message_append(reply, "ii", 2, 2);
      if (r < 0) return r;
      r = sd_bus_message_append_array(reply, 'y', st->icon, 16);
      if (r < 0) return r;
      r = sd_bus_message_close_container(reply);
      if (r < 0) return r;
      return sd_bus_message_close_container(reply);
    }

    int getString(sd_bus *, const char *, const char *, const char *property,
                  sd_bus_message *reply, void *userdata, sd_bus_error *) {
      auto *st = static_cast<MockState *>(userdata);
      const char *v = "";
      if (strcmp(property, "Status") == 0)             v = st->status;
      else if (strcmp(property, "Id") == 0)            v = "sni-mock";
      else if (strcmp(property, "Title") == 0)         v = "Mock Item";
      else if (strcmp(property, "IconName") == 0)      v = "mock-icon";
      else if (strcmp(property, "IconThemePath") == 0) v = "/tmp/mock-theme";
      return sd_bus_message_append(reply, "s", v);
    }

    int getMenu(sd_bus *, const char *, const char *, const char *,
                sd_bus_message *reply, void *, sd_bus_error *) {
      return sd_bus_message_append(reply, "o", "/MenuBar");
    }

    int getItemIsMenu(sd_bus *, const char *, const char *, const char *,
                      sd_bus_message *reply, void *, sd_bus_error *) {
      return sd_bus_message_append(reply, "b", 1);
    }

    int getToolTip(sd_bus *, const char *, const char *, const char *,
                   sd_bus_message *reply, void *, sd_bus_error *) {
      // (sa(iiay)ss): icon-name, icon-frames (empty), title, body.
      int r = sd_bus_message_open_container(reply, 'r', "sa(iiay)ss");
      if (r < 0) return r;
      r = sd_bus_message_append(reply, "s", "");
      if (r < 0) return r;
      r = sd_bus_message_open_container(reply, 'a', "(iiay)");
      if (r < 0) return r;
      r = sd_bus_message_close_container(reply);
      if (r < 0) return r;
      r = sd_bus_message_append(reply, "ss", "Mock Tooltip", "body");
      if (r < 0) return r;
      return sd_bus_message_close_container(reply);
    }

    // Activate / SecondaryActivate / ContextMenu all record "<member> x y".
    int onMethod(sd_bus_message *m, void *userdata, sd_bus_error *) {
      auto *st = static_cast<MockState *>(userdata);
      int x = 0, y = 0;
      sd_bus_message_read(m, "ii", &x, &y);
      dprintf(st->report_fd, "%s %d %d\n", sd_bus_message_get_member(m), x, y);
      return sd_bus_reply_method_return(m, "");
    }

    const sd_bus_vtable kItemVtable[] = {
      SD_BUS_VTABLE_START(0),
      SD_BUS_METHOD("Activate", "ii", "", onMethod, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("SecondaryActivate", "ii", "", onMethod, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("ContextMenu", "ii", "", onMethod, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_PROPERTY("IconPixmap", "a(iiay)", getIconPixmap, 0, 0),
      SD_BUS_PROPERTY("Id", "s", getString, 0, 0),
      SD_BUS_PROPERTY("Title", "s", getString, 0, 0),
      SD_BUS_PROPERTY("Status", "s", getString, 0, 0),
      SD_BUS_PROPERTY("IconName", "s", getString, 0, 0),
      SD_BUS_PROPERTY("IconThemePath", "s", getString, 0, 0),
      SD_BUS_PROPERTY("Menu", "o", getMenu, 0, 0),
      SD_BUS_PROPERTY("ItemIsMenu", "b", getItemIsMenu, 0, 0),
      SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)", getToolTip, 0, 0),
      SD_BUS_SIGNAL("NewIcon", "", 0),
      SD_BUS_SIGNAL("NewStatus", "s", 0),
      SD_BUS_SIGNAL("NewTitle", "", 0),
      SD_BUS_SIGNAL("NewToolTip", "", 0),
      SD_BUS_VTABLE_END
    };

    [[noreturn]] void childMain(int cmd_fd, int report_fd, bool by_name, bool with_menu) {
      MockState st;
      st.report_fd = report_fd;
      st.with_menu = with_menu;
      if (sd_bus_open_user(&st.bus) < 0) _exit(1);
      if (sd_bus_add_object_vtable(st.bus, nullptr, "/StatusNotifierItem",
                                   "org.kde.StatusNotifierItem", kItemVtable,
                                   &st) < 0)
        _exit(1);
      if (with_menu) {
        if (sd_bus_add_object_vtable(st.bus, nullptr, "/MenuBar",
                                     "com.canonical.dbusmenu", kMenuVtable, &st) < 0)
          _exit(1);
      }
      const char *reg_arg = "/StatusNotifierItem";
      if (by_name) {
        if (sd_bus_request_name(st.bus, "org.test.SniMock", 0) < 0) _exit(1);
        reg_arg = "org.test.SniMock";
      }
      // Register with the watcher, retrying until it is up. Blocking is safe
      // here - the watcher lives in the PARENT, a different process.
      bool registered = false;
      for (int i = 0; i < 500 && !registered; ++i) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        int r = sd_bus_call_method(st.bus, "org.kde.StatusNotifierWatcher",
                                   "/StatusNotifierWatcher",
                                   "org.kde.StatusNotifierWatcher",
                                   "RegisterStatusNotifierItem", &err, nullptr,
                                   "s", reg_arg);
        sd_bus_error_free(&err);
        if (r >= 0) registered = true;
        else usleep(10 * 1000);
      }
      if (!registered) _exit(1);
      dprintf(report_fd, "registered\n");

      // Serve properties/methods + obey commands until 'q' or the pipe dies.
      for (;;) {
        for (;;) {
          int r = sd_bus_process(st.bus, nullptr);
          if (r < 0) _exit(1);
          if (r == 0) break;
        }
        struct pollfd fds[2] = {
          { sd_bus_get_fd(st.bus), POLLIN, 0 },
          { cmd_fd, POLLIN, 0 },
        };
        short bus_ev = (short)sd_bus_get_events(st.bus);
        if (bus_ev > 0) fds[0].events = bus_ev;
        if (poll(fds, 2, 50) < 0 && errno != EINTR) _exit(1);
        if (fds[1].revents & (POLLIN | POLLHUP)) {
          char c = 0;
          ssize_t n = read(cmd_fd, &c, 1);
          if (n == 0) _exit(0);                        // parent gone
          if (n < 0) continue;
          if (c == 'q') { sd_bus_flush_close_unref(st.bus); _exit(0); }
          if (c == 'i') {
            st.icon = kSniMockIcon2;
            sd_bus_emit_signal(st.bus, "/StatusNotifierItem",
                               "org.kde.StatusNotifierItem", "NewIcon", "");
          }
          if (c == 's') {
            st.status = "NeedsAttention";
            sd_bus_emit_signal(st.bus, "/StatusNotifierItem",
                               "org.kde.StatusNotifierItem", "NewStatus", "s",
                               st.status);
          }
          if (c == 'L') {   // bump revision + emit LayoutUpdated
            g_menu_revision++;
            sd_bus_emit_signal(st.bus, "/MenuBar", "com.canonical.dbusmenu",
                               "LayoutUpdated", "ui", g_menu_revision, 0);
          }
          if (c == 'l') {   // emit LayoutUpdated WITHOUT bumping (revision unchanged)
            sd_bus_emit_signal(st.bus, "/MenuBar", "com.canonical.dbusmenu",
                               "LayoutUpdated", "ui", g_menu_revision, 0);
          }
        }
      }
    }

  } // namespace

  SniMockChild::SniMockChild(bool register_by_name, bool with_menu) {
    int cmd[2] = {-1, -1}, rep[2] = {-1, -1};
    if (pipe(cmd) < 0) return;
    if (pipe(rep) < 0) { close(cmd[0]); close(cmd[1]); return; }
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
      close(cmd[1]);
      close(rep[0]);
      childMain(cmd[0], rep[1], register_by_name, with_menu);   // never returns
    }
    close(cmd[0]);
    close(rep[1]);
    pid_ = pid;
    cmd_fd_ = cmd[1];
    report_fd_ = rep[0];
  }

  SniMockChild::~SniMockChild() {
    if (pid_ > 0) { kill(pid_, SIGKILL); waitpid(pid_, nullptr, 0); }
    if (cmd_fd_ >= 0) close(cmd_fd_);
    if (report_fd_ >= 0) close(report_fd_);
  }

  void SniMockChild::send(char c) {
    if (cmd_fd_ >= 0) (void)!write(cmd_fd_, &c, 1);
  }

  void SniMockChild::updateIcon() { send('i'); }
  void SniMockChild::updateStatus() { send('s'); }
  void SniMockChild::emitLayoutUpdated(bool bump_revision) { send(bump_revision ? 'L' : 'l'); }

  void SniMockChild::quit() {
    if (pid_ <= 0) return;
    send('q');
    waitpid(pid_, nullptr, 0);
    pid_ = -1;
  }

  void SniMockChild::killHard() {
    if (pid_ <= 0) return;
    kill(pid_, SIGKILL);
    waitpid(pid_, nullptr, 0);
    pid_ = -1;
  }

  std::string SniMockChild::waitReport(int timeout_ms, std::function<void()> pump) {
    for (int waited = 0; waited <= timeout_ms;) {
      std::string::size_type nl = buf_.find('\n');
      if (nl != std::string::npos) {
        std::string line = buf_.substr(0, nl);
        buf_.erase(0, nl + 1);
        return line;
      }
      if (pump) pump();
      struct pollfd p = { report_fd_, POLLIN, 0 };
      int r = poll(&p, 1, 50);
      waited += 50;
      if (r <= 0) continue;
      char tmp[256];
      ssize_t n = read(report_fd_, tmp, sizeof tmp);
      if (n <= 0) return "";
      buf_.append(tmp, (size_t)n);
    }
    return "";
  }

} // namespace bbai::test
