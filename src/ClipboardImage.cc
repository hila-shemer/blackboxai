#include "ClipboardImage.hh"

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

namespace bbai {

  namespace {
    // One in-flight paste: owns its own ref to the bytes + the write fd + source.
    struct Writer {
      ClipboardImage::Blob png;
      size_t offset = 0;
      int fd = -1;
      wl_event_source *src = nullptr;
    };

    void writerFinish(Writer *w) {
      if (w->src) wl_event_source_remove(w->src);   // remove BEFORE close
      if (w->fd >= 0) close(w->fd);
      delete w;
    }

    int writerCb(int fd, uint32_t mask, void *data) {
      auto *w = static_cast<Writer *>(data);
      if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) { writerFinish(w); return 0; }
      const std::vector<uint8_t> &bytes = *w->png;
      while (w->offset < bytes.size()) {
        ssize_t n = write(fd, bytes.data() + w->offset, bytes.size() - w->offset);
        if (n > 0) { w->offset += static_cast<size_t>(n); continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;  // wait writable
        writerFinish(w);                              // real write error (EPIPE etc.)
        return 0;
      }
      writerFinish(w);                                // all bytes written
      return 0;
    }

    void ciSend(wlr_data_source *source, const char *mime, int32_t fd) {
      auto *ci = reinterpret_cast<ClipboardImage *>(source);
      if (!mime || std::strcmp(mime, "image/png") != 0 || !ci->png) { close(fd); return; }
      const int flags = fcntl(fd, F_GETFL, 0);
      fcntl(fd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);   // paste fd arrives BLOCKING
      auto *w = new Writer{ ci->png, 0, fd, nullptr };
      wl_event_loop *loop = wl_display_get_event_loop(ci->display);
      w->src = wl_event_loop_add_fd(loop, fd, WL_EVENT_WRITABLE, writerCb, w);
      if (!w->src) { close(fd); delete w; return; }   // add_fd OOM: don't leak fd+Writer
    }

    void ciDestroy(wlr_data_source *source) {
      // wlroots already freed base.mime_types + the strdup'd string before this.
      // We own only the C++ subtype; in-flight Writers keep their own blob ref.
      delete reinterpret_cast<ClipboardImage *>(source);
    }

    const wlr_data_source_impl kImpl = {
      .send = ciSend,
      .accept = nullptr,        // clipboard-only: accept/dnd_* are unused
      .destroy = ciDestroy,
      .dnd_drop = nullptr,
      .dnd_finish = nullptr,
      .dnd_action = nullptr,
    };
  } // namespace

  ClipboardImage *ClipboardImage::create(wl_display *display, Blob png) {
    auto *ci = new ClipboardImage();
    wlr_data_source_init(&ci->base, &kImpl);   // zero-fills base; set C++ members AFTER
    ci->display = display;
    ci->png = std::move(png);
    char **slot = static_cast<char **>(wl_array_add(&ci->base.mime_types, sizeof(char *)));
    if (slot) *slot = strdup("image/png");
    return ci;
  }

} // namespace bbai
