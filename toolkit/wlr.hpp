// The single C-interop boundary for BlackboxAI. No other file includes
// wlroots / wayland / xkb C headers directly — they all come through here,
// wrapped in extern "C", with WLR_USE_UNSTABLE forced on. This header also
// houses the wlroots-version shim as the ABI moves across minor releases.
#ifndef BLACKBOXAI_WLR_HPP
#define BLACKBOXAI_WLR_HPP

#ifndef WLR_USE_UNSTABLE
#define WLR_USE_UNSTABLE
#endif

extern "C" {

// wlroots 0.19's wlr_scene.h declares two functions using the C99 '[static 4]'
// array-parameter hint (wlr_scene_rect_create / wlr_scene_rect_set_color),
// which is a syntax error in C++. We include a build-time *sanitized* copy
// (the hint stripped to '[]') FIRST; because it is a verbatim copy it defines
// wlr_scene.h's include guard, so the unmodified system header is suppressed
// even when pulled in transitively below. See the wlr_scene_compat
// custom_target in the top-level meson.build. (No -fpermissive, no keyword
// macros — the fix is localized to this boundary shim.)
#include <wlr_scene_compat.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>
#include <wlr/interfaces/wlr_buffer.h>

// client-facing globals (M2+)
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>

// input (seat/cursor/keyboard via wlr_seat.h's transitive pulls) + server-side
// decorations (M3). wlr_seat.h transitively includes wlr_input_device.h,
// wlr_keyboard.h (which pulls <xkbcommon/xkbcommon.h>) and wlr_pointer.h.
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_server_decoration.h>

// Keysym constants (XKB_KEY_*) for the M4 keybinding table + menu navigation.
// wlr_keyboard.h already pulls <xkbcommon/xkbcommon.h> (the xkb_* functions);
// this adds the symbol names.
#include <xkbcommon/xkbcommon-keysyms.h>

} // extern "C"

#endif // BLACKBOXAI_WLR_HPP
