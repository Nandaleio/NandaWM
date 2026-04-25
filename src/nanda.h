#ifndef NANDA_H
#define NANDA_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

struct nanda_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	/* Scene trees in render order: BACKGROUND, BOTTOM, shell windows, TOP, OVERLAY */
	struct wlr_scene_tree *layers[4];
	struct wlr_scene_tree *shell_tree;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;          /* nanda_output::link */
	struct wl_listener new_output;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct wl_list layer_surfaces;   /* nanda_layer_surface::link */

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;

	struct wl_list views;            /* nanda_view::link */
	struct nanda_view *focused_view;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_list keyboards;        /* nanda_keyboard::link */
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
};

struct nanda_output {
	struct wl_list link;
	struct nanda_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct nanda_view {
	struct wl_list link;
	struct nanda_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	bool mapped;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
};

struct nanda_layer_surface {
	struct wl_list link;
	struct nanda_server *server;
	struct nanda_output *output;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene_layer_surface;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener surface_commit;
	struct wl_listener new_popup;
};

struct nanda_keyboard {
	struct wl_list link;
	struct nanda_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

/* layer.c */
void server_new_layer_surface(struct wl_listener *listener, void *data);
void arrange_layers(struct nanda_server *server);

/* view.c */
void focus_view(struct nanda_view *view);
void cycle_view(struct nanda_server *server);
void server_new_xdg_toplevel(struct wl_listener *listener, void *data);
void server_new_xdg_popup(struct wl_listener *listener, void *data);

/* output.c */
void server_new_output(struct wl_listener *listener, void *data);

/* input.c */
void server_new_input(struct wl_listener *listener, void *data);
void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);
void server_request_cursor(struct wl_listener *listener, void *data);
void server_request_set_selection(struct wl_listener *listener, void *data);

#endif /* NANDA_H */
