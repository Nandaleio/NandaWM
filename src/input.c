#include <stdlib.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <xkbcommon/xkbcommon.h>
#include "nanda.h"

static void spawn(const char *cmd) {
	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
		_exit(1);
	}
}

/* ---- pointer helpers ---- */

static void process_cursor_motion(struct nanda_server *server, uint32_t time) {
	double sx, sy;
	struct wlr_surface *surface = NULL;

	struct wlr_scene_node *node = wlr_scene_node_at(
		&server->scene->tree.node,
		server->cursor->x, server->cursor->y, &sx, &sy);

	if (node && node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *buf = wlr_scene_buffer_from_node(node);
		struct wlr_scene_surface *scene_surf =
			wlr_scene_surface_try_from_buffer(buf);
		if (scene_surf) {
			surface = scene_surf->surface;
		}
	}

	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

/* ---- cursor events ---- */

void server_cursor_motion(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
		event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_button(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	wlr_seat_pointer_notify_button(server->seat,
		event->time_msec, event->button, event->state);
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(server->seat,
		event->time_msec, event->orientation, event->delta,
		event->delta_discrete, event->source, event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

/* ---- seat requests ---- */

void server_request_cursor(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused =
		server->seat->pointer_state.focused_client;
	if (focused == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

void server_request_set_selection(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* ---- keyboard ---- */

static void keyboard_modifiers(struct wl_listener *listener, void *data) {
	struct nanda_keyboard *kb = wl_container_of(listener, kb, modifiers);
	wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(kb->server->seat,
		&kb->wlr_keyboard->modifiers);
}

static void keyboard_key(struct wl_listener *listener, void *data) {
	struct nanda_keyboard *kb = wl_container_of(listener, kb, key);
	struct nanda_server *server = kb->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_keyboard *wlr_kb = kb->wlr_keyboard;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(wlr_kb->xkb_state, keycode, &syms);

	uint32_t mods = wlr_keyboard_get_modifiers(wlr_kb);
	bool handled = false;

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			xkb_keysym_t sym = syms[i];

			/* Mod+Tab: cycle through windows */
			if ((mods & WLR_MODIFIER_LOGO) && sym == XKB_KEY_Tab) {
				cycle_view(server);
				handled = true;

			/* Alt+F4: close the focused window */
			} else if ((mods & WLR_MODIFIER_ALT) && sym == XKB_KEY_F4) {
				if (server->focused_view) {
					wlr_xdg_toplevel_send_close(
						server->focused_view->xdg_toplevel);
				}
				handled = true;

			/* Ctrl+Alt+Q: quit the compositor */
			} else if ((mods & WLR_MODIFIER_ALT) &&
					(mods & WLR_MODIFIER_CTRL) &&
					sym == XKB_KEY_q) {
				wl_display_terminate(server->wl_display);
				handled = true;

			/* Mod+F: open fuzzel launcher */
			} else if ((mods & WLR_MODIFIER_LOGO) && sym == XKB_KEY_f) {
				spawn("fuzzel");
				handled = true;

			/* Mod+Enter: open fuzzel launcher */
			} else if ((mods & WLR_MODIFIER_LOGO) && sym == XKB_KEY_Return) {
				spawn("fuzzel");
				handled = true;
			}
		}
	}

	if (!handled) {
		wlr_seat_set_keyboard(server->seat, wlr_kb);
		wlr_seat_keyboard_notify_key(server->seat,
			event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_destroy(struct wl_listener *listener, void *data) {
	struct nanda_keyboard *kb = wl_container_of(listener, kb, destroy);
	wl_list_remove(&kb->modifiers.link);
	wl_list_remove(&kb->key.link);
	wl_list_remove(&kb->destroy.link);
	wl_list_remove(&kb->link);
	free(kb);
}

static void create_keyboard(struct nanda_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_kb = wlr_keyboard_from_input_device(device);
	struct nanda_keyboard *kb = calloc(1, sizeof(*kb));
	kb->server = server;
	kb->wlr_keyboard = wlr_kb;

	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_rule_names rules = {
		.model   = "pc105",
		.layout  = "fr",
		.variant = "latin9",
	};
	struct xkb_keymap *keymap =
		xkb_keymap_new_from_names(ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
	wlr_keyboard_set_keymap(wlr_kb, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);

	wlr_keyboard_set_repeat_info(wlr_kb, 25, 600);

	kb->modifiers.notify = keyboard_modifiers;
	wl_signal_add(&wlr_kb->events.modifiers, &kb->modifiers);
	kb->key.notify = keyboard_key;
	wl_signal_add(&wlr_kb->events.key, &kb->key);
	kb->destroy.notify = keyboard_destroy;
	wl_signal_add(&device->events.destroy, &kb->destroy);

	wlr_seat_set_keyboard(server->seat, wlr_kb);
	wl_list_insert(&server->keyboards, &kb->link);
}

static void create_pointer(struct nanda_server *server,
		struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

/* ---- new input device ---- */

void server_new_input(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		create_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		create_pointer(server, device);
		break;
	default:
		break;
	}

	/* Tell the seat what capabilities it has */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}
