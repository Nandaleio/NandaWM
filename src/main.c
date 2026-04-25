#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "nanda.h"

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);

	struct nanda_server server = {0};

	server.wl_display = wl_display_create();

	/* In wlroots 0.18, backend_autocreate takes an event loop, not the display */
	struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
	server.backend = wlr_backend_autocreate(loop, NULL);
	if (!server.backend) {
		wlr_log(WLR_ERROR, "Failed to create backend");
		return 1;
	}

	server.renderer = wlr_renderer_autocreate(server.backend);
	if (!server.renderer) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		return 1;
	}
	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (!server.allocator) {
		wlr_log(WLR_ERROR, "Failed to create allocator");
		return 1;
	}

	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	/* Output layout maps physical displays into a 2D coordinate space */
	server.output_layout = wlr_output_layout_create(server.wl_display);
	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	/* Scene graph handles compositing: render order, visibility, transforms */
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	/* Scene trees in z-order (back → front): BACKGROUND, BOTTOM, windows, TOP, OVERLAY */
	server.layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] =
		wlr_scene_tree_create(&server.scene->tree);
	server.layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] =
		wlr_scene_tree_create(&server.scene->tree);
	server.shell_tree = wlr_scene_tree_create(&server.scene->tree);
	server.layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP] =
		wlr_scene_tree_create(&server.scene->tree);
	server.layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] =
		wlr_scene_tree_create(&server.scene->tree);

	/* Layer shell: panels, docks, wallpapers, overlays (wlr-layer-shell-unstable-v1) */
	wl_list_init(&server.layer_surfaces);
	server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
	server.new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

	/* XDG shell: protocol used by most modern Wayland apps */
	wl_list_init(&server.views);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 6);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	/* Cursor: logical pointer that moves across outputs */
	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	wlr_xcursor_manager_load(server.cursor_mgr, 1.0f);

	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	/* Seat: owns keyboard, pointer, and touch for one user session */
	wl_list_init(&server.keyboards);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.request_cursor.notify = server_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
	server.request_set_selection.notify = server_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection,
		&server.request_set_selection);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	if (!getenv("DISPLAY"))
		setenv("DISPLAY", ":0", true);
	wlr_log(WLR_INFO, "NandaWM running on %s", socket);
	wlr_log(WLR_INFO, "Keybindings: Alt+Tab=cycle  Alt+F4=close  Super+F=fuzzel  Ctrl+Alt+Q=quit");

	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", "alacritty", NULL);
		_exit(1);
	}

	wl_display_run(server.wl_display);

	wl_display_destroy_clients(server.wl_display);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wlr_output_layout_destroy(server.output_layout);
	wl_display_destroy(server.wl_display);

	return 0;
}
