#include <config.h>
#include "QuiverUtils.h"
#include "ShortcutManager.h"

extern "C" {
#include <libswscale/swscale.h>
}
#include <vector>
#include <cstdint>

extern GtkApplication *g_pApp;

#define N_LOOPS 10

namespace QuiverUtils
{
	GdkTexture * TextureExifReorientate(GdkTexture * texture, int orientation)
	{
		if (!texture || orientation <= 1)
			return texture ? (GdkTexture*)g_object_ref(texture) : NULL;

		int src_w = gdk_texture_get_width(texture);
		int src_h = gdk_texture_get_height(texture);
		if (src_w <= 0 || src_h <= 0)
			return NULL;

		gsize src_stride = (gsize)src_w * 4;
		std::vector<guint32> src_buf((size_t)src_w * src_h);
		GdkTextureDownloader *dl = gdk_texture_downloader_new(texture);
		gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
		gdk_texture_downloader_download_into(dl, (guchar*)src_buf.data(), src_stride);
		gdk_texture_downloader_free(dl);

		int dst_w = (orientation >= 5) ? src_h : src_w;
		int dst_h = (orientation >= 5) ? src_w : src_h;
		gsize dst_stride = (gsize)dst_w * 4;
		guint32 *dst_raw = (guint32*)g_malloc0((size_t)dst_w * dst_h * 4);

		for (int y = 0; y < dst_h; ++y)
		{
			for (int x = 0; x < dst_w; ++x)
			{
				int sx = 0, sy = 0;
				switch (orientation)
				{
					case 2: // Flip H
						sx = src_w - 1 - x;
						sy = y;
						break;
					case 3: // Rotate 180
						sx = src_w - 1 - x;
						sy = src_h - 1 - y;
						break;
					case 4: // Flip V
						sx = x;
						sy = src_h - 1 - y;
						break;
					case 5: // Transpose (Flip V + Rot 90)
						sx = y;
						sy = x;
						break;
					case 6: // Rotate 90 CW
						sx = y;
						sy = src_h - 1 - x;
						break;
					case 7: // Transverse (Flip V + Rot 270)
						sx = src_w - 1 - y;
						sy = src_h - 1 - x;
						break;
					case 8: // Rotate 270 CW (90 CCW)
						sx = src_w - 1 - y;
						sy = x;
						break;
					default:
						sx = x;
						sy = y;
						break;
				}
				if (sx >= 0 && sx < src_w && sy >= 0 && sy < src_h)
				{
					dst_raw[y * dst_w + x] = src_buf[sy * src_w + sx];
				}
			}
		}

		GBytes *bytes = g_bytes_new_take(dst_raw, (gsize)dst_w * dst_h * 4);
		GdkTexture *result = gdk_memory_texture_new(dst_w, dst_h, GDK_MEMORY_R8G8B8A8, bytes, dst_stride);
		g_bytes_unref(bytes);
		return result;
	}

	GdkTexture * ScaleTexture(GdkTexture * texture, int dest_w, int dest_h)
	{
		if (!texture || dest_w <= 0 || dest_h <= 0)
			return NULL;

		int src_w = gdk_texture_get_width(texture);
		int src_h = gdk_texture_get_height(texture);
		if (src_w == dest_w && src_h == dest_h)
			return (GdkTexture*)g_object_ref(texture);

		gsize src_stride = (gsize)src_w * 4;
		std::vector<uint8_t> src_buf((size_t)src_w * src_h * 4);
		GdkTextureDownloader *dl = gdk_texture_downloader_new(texture);
		gdk_texture_downloader_set_format(dl, GDK_MEMORY_R8G8B8A8);
		gdk_texture_downloader_download_into(dl, src_buf.data(), src_stride);
		gdk_texture_downloader_free(dl);

		struct SwsContext *sws = sws_getContext(
			src_w, src_h, AV_PIX_FMT_RGBA,
			dest_w, dest_h, AV_PIX_FMT_RGBA,
			SWS_BILINEAR, NULL, NULL, NULL);
		if (!sws)
			return NULL;

		gsize dst_stride = (gsize)dest_w * 4;
		uint8_t *dst_data = (uint8_t*)g_malloc((size_t)dest_w * dest_h * 4);

		const uint8_t *src_slice[1] = { src_buf.data() };
		int src_stride_arr[1] = { (int)src_stride };
		uint8_t *dst_slice[1] = { dst_data };
		int dst_stride_arr[1] = { (int)dst_stride };

		sws_scale(sws, src_slice, src_stride_arr, 0, src_h, dst_slice, dst_stride_arr);
		sws_freeContext(sws);

		GBytes *bytes = g_bytes_new_take(dst_data, (gsize)dest_w * dest_h * 4);
		GdkTexture *result = gdk_memory_texture_new(dest_w, dest_h, GDK_MEMORY_R8G8B8A8, bytes, dst_stride);
		g_bytes_unref(bytes);
		return result;
	}

#if HAVE_GDK_PIXBUF
	GdkTexture * PixbufToTexture(GdkPixbuf * pixbuf)
	{
		if (!pixbuf)
			return NULL;

		GBytes *bytes = gdk_pixbuf_read_pixel_bytes(pixbuf);
		gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
		GdkMemoryFormat fmt = has_alpha ? GDK_MEMORY_R8G8B8A8 : GDK_MEMORY_R8G8B8;
		GdkTexture *tex = gdk_memory_texture_new(
			gdk_pixbuf_get_width(pixbuf),
			gdk_pixbuf_get_height(pixbuf),
			fmt,
			bytes,
			gdk_pixbuf_get_rowstride(pixbuf)
		);
		g_bytes_unref(bytes);
		return tex;
	}
	
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation)
	{
		GdkPixbuf * modified = NULL;

		switch (orientation)
		{
			case 1:
				break;
			case 2:
				modified = gdk_pixbuf_flip(pixbuf,TRUE);
				break;
			case 3:
				modified = gdk_pixbuf_rotate_simple(pixbuf,(GdkPixbufRotation)180);
				break;
			case 4:
				modified = gdk_pixbuf_flip(pixbuf,FALSE);
				break;
			case 5:
				{
					GdkPixbuf *tmp = gdk_pixbuf_flip(pixbuf,FALSE);
					modified = gdk_pixbuf_rotate_simple(tmp,GDK_PIXBUF_ROTATE_CLOCKWISE);
					g_object_unref(tmp);
				}
				break;
			case 6:
				modified = gdk_pixbuf_rotate_simple(pixbuf,GDK_PIXBUF_ROTATE_CLOCKWISE);
				break;
			case 7:
				{
					GdkPixbuf *tmp = gdk_pixbuf_flip(pixbuf,FALSE);
					modified = gdk_pixbuf_rotate_simple(tmp,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
					g_object_unref(tmp);
				}
				break;
			case 8:
				modified = gdk_pixbuf_rotate_simple(pixbuf,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
			default:
				break;
		}
		return modified;
	}
#endif
}

#include "QuiverUtils.h"

extern GtkApplication *g_pApp;
#include <gtk/gtk.h>
#include <gio/gio.h>

namespace QuiverUtils
{
	struct AccelEntry {
		char *action_name;
		guint keyval;
		GdkModifierType mods;
		gboolean connected;
		gboolean suppressed;
	};

	struct RadioMember {
		char *name;
		gint value;
	};

	struct RadioGroup {
		GPtrArray *members;
		gint current;
	};

	struct ToggleCallbackData {
		QuiverActionCallback cb;
		gpointer user_data;
	};

	struct RadioCallbackData {
		QuiverActionCallback cb;
		gpointer user_data;
		RadioGroup *group;
		RadioMember *member;
	};

	static GSimpleActionGroup *g_pActionGroup = NULL;

	static GPtrArray *g_accelEntries = NULL;
	static GPtrArray *g_radioGroups = NULL;

	static void free_accel_entry(gpointer data) {
		AccelEntry *entry = (AccelEntry*)data;
		g_free(entry->action_name);
		g_free(entry);
	}

	static void free_radio_group(gpointer data) {
		RadioGroup *group = (RadioGroup*)data;
		for (guint i = 0; i < group->members->len; i++) {
			RadioMember *member = (RadioMember*)g_ptr_array_index(group->members, i);
			g_free(member->name);
			g_free(member);
		}
		g_ptr_array_free(group->members, TRUE);
		g_free(group);
	}


static void register_accelerator(const char *action_name, const gchar *accel) {
		guint keyval;
		GdkModifierType mods;
		gtk_accelerator_parse(accel, &keyval, &mods);
		if (keyval == 0) return;

		if (ShortcutManager::GetInstance().GetAction(action_name) != nullptr) {
			ShortcutManager::GetInstance().ApplyShortcutsForAction(action_name);
			return;
		}

		if (g_pApp) {
			gchar *detailed_name = g_strdup_printf("quiver.%s", action_name);
			const gchar *accels[] = {accel, NULL};
			gtk_application_set_accels_for_action(g_pApp, detailed_name, accels);
			g_free(detailed_name);
		}

		AccelEntry *entry = g_new0(AccelEntry, 1);
		entry->action_name = g_strdup(action_name);
		entry->keyval = keyval;
		entry->mods = mods;
		entry->connected = FALSE;
		entry->suppressed = FALSE;

		g_ptr_array_add(g_accelEntries, entry);
	}

	static void radio_activate_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
		RadioCallbackData *data = (RadioCallbackData*)user_data;
		RadioGroup *group = data->group;
		group->current = data->member->value;
		for (guint i = 0; i < group->members->len; i++) {
			RadioMember *member = (RadioMember*)g_ptr_array_index(group->members, i);
			GAction *member_action = QuiverUtils::GetAction(member->name);
			if (NULL != member_action) {
				gboolean active = (member == data->member);
				g_simple_action_set_state(G_SIMPLE_ACTION(member_action), g_variant_new_boolean(active));
			}
		}
		if (NULL != data->cb) {
			data->cb(action, parameter, data->user_data);
		}
	}

static void toggle_activate_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
		ToggleCallbackData *data = (ToggleCallbackData*)user_data;
		
		GVariant *state = g_action_get_state(G_ACTION(action));
		gboolean active = state ? g_variant_get_boolean(state) : FALSE;
		if (state) g_variant_unref(state);
		
		gboolean new_state = !active;
		if (parameter != NULL && g_variant_is_of_type(parameter, G_VARIANT_TYPE_BOOLEAN)) {
		    new_state = g_variant_get_boolean(parameter);
		}
		
		GVariant *new_state_var = g_variant_new_boolean(new_state);
		g_simple_action_set_state(action, new_state_var);

		if (NULL != data->cb) {
			data->cb(action, new_state_var, data->user_data);
		}
	}

	void InitActions() {
		if (NULL == g_pActionGroup) {
			g_pActionGroup = g_simple_action_group_new();
			g_object_ref_sink(g_pActionGroup);
			g_accelEntries = g_ptr_array_new_with_free_func(free_accel_entry);
			g_radioGroups = g_ptr_array_new_with_free_func(free_radio_group);
		}
	}

	GSimpleActionGroup* GetActionGroup() {
		InitActions();
		return g_pActionGroup;
	}

	void AddAction(GAction *action) {
		InitActions();
		g_action_map_add_action(G_ACTION_MAP(g_pActionGroup), action);
	}

	void RemoveAction(const char *action_name) {
		if (NULL == g_pActionGroup) return;
		if (NULL != g_accelEntries) {
			for (guint i = 0; i < g_accelEntries->len; i++) {
				AccelEntry *entry = (AccelEntry*)g_ptr_array_index(g_accelEntries, i);
				if (0 == strcmp(entry->action_name, action_name)) {
					g_ptr_array_remove_index(g_accelEntries, i);
					break;
				}
			}
		}
		g_action_map_remove_action(G_ACTION_MAP(g_pActionGroup), action_name);
	}

	GAction* GetAction(const char *action_name) {
		if (NULL == g_pActionGroup) return NULL;
		return g_action_map_lookup_action(G_ACTION_MAP(g_pActionGroup), action_name);
	}

	void SetActionsSensitive(const gchar **actions, gint n_actions, gboolean bSensitive) {
		for (gint i = 0; i < n_actions; i++) {
			GAction *action = GetAction(actions[i]);
			if (NULL != action && G_IS_SIMPLE_ACTION(action)) {
				g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bSensitive);
			}
		}
	}

	GSimpleAction* AddSimpleAction(const char *name, const gchar *accel, QuiverActionCallback cb, gpointer user_data) {
		InitActions();
		GSimpleAction *action = g_simple_action_new(name, NULL);
		if (NULL != cb) {
			g_signal_connect(action, "activate", G_CALLBACK(cb), user_data);
		}
		AddAction(G_ACTION(action));
		if (NULL != accel && 0 != accel[0]) {
			register_accelerator(name, accel);
		}
		return action;
	}

	GSimpleAction* AddToggleAction(const char *name, const gchar *accel, gboolean active, QuiverActionCallback cb, gpointer user_data) {
		InitActions();
		GSimpleAction *action = g_simple_action_new_stateful(name, NULL, g_variant_new_boolean(active));
		if (NULL != cb) {
			ToggleCallbackData *data = g_new0(ToggleCallbackData, 1);
			data->cb = cb;
			data->user_data = user_data;
			g_signal_connect(action, "activate", G_CALLBACK(toggle_activate_cb), data);
		}
		AddAction(G_ACTION(action));
		if (NULL != accel && 0 != accel[0]) {
			register_accelerator(name, accel);
		}
		return action;
	}

	void AddRadioActions(const char *const names[], const gint values[], gint n, gint current_value, QuiverActionCallback cb, gpointer user_data) {
		InitActions();
		RadioGroup *group = g_new0(RadioGroup, 1);
		group->members = g_ptr_array_new();
		group->current = current_value;

		for (gint i = 0; i < n; i++) {
			RadioMember *member = g_new0(RadioMember, 1);
			member->name = g_strdup(names[i]);
			member->value = values[i];
			g_ptr_array_add(group->members, member);

			gboolean active = (values[i] == current_value);
			GSimpleAction *action = g_simple_action_new_stateful(names[i], NULL, g_variant_new_boolean(active));

			RadioCallbackData *data = g_new0(RadioCallbackData, 1);
			data->cb = cb;
			data->user_data = user_data;
			data->group = group;
			data->member = member;

			g_signal_connect(action, "activate", G_CALLBACK(radio_activate_cb), data);
			AddAction(G_ACTION(action));
		}
		g_ptr_array_add(g_radioGroups, group);
	}

	gboolean ToggleActionGetActive(const char *action_name) {
		GAction *action = GetAction(action_name);
		if (NULL == action) return FALSE;
		GVariant *state = g_action_get_state(action);
		if (NULL == state) return FALSE;
		gboolean active = g_variant_get_boolean(state);
		g_variant_unref(state);
		return active;
	}

	void ToggleActionSetActive(const char *action_name, gboolean active) {
		GAction *action = GetAction(action_name);
		if (NULL == action) return;
		GVariant *state = g_action_get_state(action);
		gboolean current = (NULL != state) ? g_variant_get_boolean(state) : FALSE;
		if (NULL != state) g_variant_unref(state);
		if (current == active) return;
	// mirrors the old gtk_toggle_action_set_active(): activate also runs
	// the action callback that actually shows/hides the associated widgets
	g_action_activate(action, NULL);
	}

	void ToggleActionSetState(const char *action_name, gboolean active) {
		// set the toggle state without running the activate callback
		GAction *action = GetAction(action_name);
		if (NULL == action) return;
		g_action_change_state(action, g_variant_new_boolean(active));
	}

	gint GetRadioActionCurrent(const char *action_name) {
		if (NULL == g_radioGroups) return 0;
		for (guint i = 0; i < g_radioGroups->len; i++) {
			RadioGroup *group = (RadioGroup*)g_ptr_array_index(g_radioGroups, i);
			for (guint j = 0; j < group->members->len; j++) {
				RadioMember *member = (RadioMember*)g_ptr_array_index(group->members, j);
				if (0 == strcmp(member->name, action_name)) return group->current;
			}
		}
		return 0;
	}

	void SetRadioActionCurrent(const char *action_name, gint value) {
		if (NULL == g_radioGroups) return;
		for (guint i = 0; i < g_radioGroups->len; i++) {
			RadioGroup *group = (RadioGroup*)g_ptr_array_index(g_radioGroups, i);
			for (guint j = 0; j < group->members->len; j++) {
				RadioMember *member = (RadioMember*)g_ptr_array_index(group->members, j);
				if (0 == strcmp(member->name, action_name)) {
					group->current = value;
					for (guint k = 0; k < group->members->len; k++) {
						RadioMember *other = (RadioMember*)g_ptr_array_index(group->members, k);
						GAction *other_action = GetAction(other->name);
						if (NULL != other_action) {
							g_simple_action_set_state(G_SIMPLE_ACTION(other_action), g_variant_new_boolean(other->value == value));
						}
					}
					return;
				}
			}
		}
	}

void AddAccelGroup(GtkWindow* /*window*/) {
		// Handled by GtkApplication
	}

void DisconnectUnmodifiedAccelerators() {
		ShortcutManager::GetInstance().SuppressUnmodifiedAccelerators(true);
	}

void ConnectUnmodifiedAccelerators() {
		ShortcutManager::GetInstance().SuppressUnmodifiedAccelerators(false);
	}

	void BindBuilderAccelerators(GtkBuilder *builder) {
		GSList *objects = gtk_builder_get_objects(builder);
		for (GSList *l = objects; l != NULL; l = l->next) {
			if (GTK_IS_ACTIONABLE(l->data)) {
				const gchar *action_name = gtk_actionable_get_action_name(GTK_ACTIONABLE(l->data));
				if (action_name && g_str_has_prefix(action_name, "quiver.")) {
					const gchar *name = action_name + 7;
					for (guint i = 0; i < g_accelEntries->len; i++) {
						AccelEntry *entry = (AccelEntry*)g_ptr_array_index(g_accelEntries, i);
						if (g_strcmp0(entry->action_name, name) == 0) {
							break;
						}
					}
				}
			}
		}
		g_slist_free(objects);
	}

	void BindWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name) {
		gchar *full_name = g_strdup_printf("quiver.%s", action_name);
		gtk_actionable_set_action_name(GTK_ACTIONABLE(widget), full_name);
		g_free(full_name);
		gtk_widget_insert_action_group(ancestor, "quiver", G_ACTION_GROUP(g_pActionGroup));
	}

	static void toggle_action_state_changed_cb(GObject *object, GParamSpec *pspec, gpointer user_data) { (void)pspec; 
		GtkWidget *widget = GTK_WIDGET(user_data);
		GAction *action = G_ACTION(object);
		GVariant *state = g_action_get_state(action);
		gboolean active = (NULL != state) ? g_variant_get_boolean(state) : FALSE;
		if (NULL != state) g_variant_unref(state);
		g_object_set(widget, "active", active, NULL);
	}

	static void toggle_widget_toggled_cb(GtkWidget *widget, gpointer user_data) {
		const gchar *action_name = (const gchar*)user_data;
		GAction *action = QuiverUtils::GetAction(action_name);
		if (NULL == action) return;
		gboolean active = FALSE;
		g_object_get(widget, "active", &active, NULL);
		GVariant *state = g_action_get_state(action);
		gboolean current = (NULL != state) ? g_variant_get_boolean(state) : FALSE;
		if (NULL != state) g_variant_unref(state);
		if (active == current) return;
		g_action_activate(action, NULL);
	}

	void BindToggleWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name) {
		GAction *action = QuiverUtils::GetAction(action_name);
		if (NULL == action) return;
		gtk_widget_insert_action_group(ancestor, "quiver", G_ACTION_GROUP(g_pActionGroup));
		gchar *name = g_strdup(action_name);
		g_object_set_data_full(G_OBJECT(widget), "quiver-action-name", name, g_free);
		g_signal_connect(widget, "toggled", G_CALLBACK(toggle_widget_toggled_cb), name);
		g_signal_connect(action, "notify::state", G_CALLBACK(toggle_action_state_changed_cb), widget);
		toggle_action_state_changed_cb(G_OBJECT(action), NULL, widget);
	}

	static void radio_action_state_changed_cb(GObject *object, GParamSpec *pspec, gpointer user_data) { (void)pspec; 
		GtkWidget *widget = GTK_WIDGET(user_data);
		GAction *action = G_ACTION(object);
		GVariant *state = g_action_get_state(action);
		gboolean active = (NULL != state) ? g_variant_get_boolean(state) : FALSE;
		if (NULL != state) g_variant_unref(state);
		g_object_set(widget, "active", active, NULL);
	}

	static void radio_widget_toggled_cb(GtkWidget *widget, gpointer user_data) {
		const gchar *action_name = (const gchar*)user_data;
		GAction *action = QuiverUtils::GetAction(action_name);
		if (NULL == action) return;
		gboolean active = FALSE;
		g_object_get(widget, "active", &active, NULL);
		if (!active) return;
		g_action_activate(action, NULL);
	}

	void BindRadioWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name) {
		if (widget == NULL) return;
		GAction *action = QuiverUtils::GetAction(action_name);
		if (NULL == action) return;
		gtk_widget_insert_action_group(ancestor, "quiver", G_ACTION_GROUP(g_pActionGroup));
		gchar *name = g_strdup(action_name);
		g_object_set_data_full(G_OBJECT(widget), "quiver-action-name", name, g_free);
		g_signal_connect(widget, "toggled", G_CALLBACK(radio_widget_toggled_cb), name);
		g_signal_connect(action, "notify::state", G_CALLBACK(radio_action_state_changed_cb), widget);
		radio_action_state_changed_cb(G_OBJECT(action), NULL, widget);
	}

	/* One-shot: grab focus on the widget once it becomes mapped, then drop the
	 * handler so it only runs a single time. */
	struct FocusOnMap { GtkWidget *widget; gulong handler_id; };
	static void grab_focus_on_map(GtkWidget *widget, gpointer user_data)
	{
		FocusOnMap *f = (FocusOnMap *)user_data;
		g_signal_handler_disconnect(widget, f->handler_id);
		g_free(f);
		if (gtk_widget_get_mapped(widget) && !gtk_widget_has_focus(widget))
			gtk_widget_grab_focus(widget);
	}

	gboolean GrabFocusForWidget(GtkWidget *widget)
	{
		if (widget == NULL)
			return FALSE;
		/* If the widget is already mapped (and focusable), grab focus now. */
		if (gtk_widget_get_mapped(widget) && gtk_widget_get_focusable(widget)
			&& gtk_widget_grab_focus(widget))
			return TRUE;
		/* Otherwise defer until it is mapped — gtk_widget_grab_focus fails
		 * silently on a not-yet-mapped widget, which is what happens when the
		 * browser/viewer UI is switched just before the first layout pass. */
		FocusOnMap *f = g_new0(FocusOnMap, 1);
		f->widget = widget;
		f->handler_id = g_signal_connect(widget, "map", G_CALLBACK(grab_focus_on_map), f);
		return FALSE;
	}

	bool ConfirmDialog(const char *title, const std::string& message,
		const char *accept_label, const char *cancel_label)
	{
		GtkWidget* dialog = gtk_window_new();
		gtk_window_set_title(GTK_WINDOW(dialog), title);
		gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
		gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
		GtkWidget* dlgBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
		GtkWidget* dlgLabel = gtk_label_new(message.c_str());
		gtk_label_set_wrap(GTK_LABEL(dlgLabel), TRUE);
		gtk_label_set_xalign(GTK_LABEL(dlgLabel), 0.0);
		gtk_widget_set_margin_start(dlgLabel, 12);
		gtk_widget_set_margin_end(dlgLabel, 12);
		gtk_widget_set_margin_top(dlgLabel, 12);
		gtk_widget_set_margin_bottom(dlgLabel, 4);
		GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_widget_set_halign(btnBox, GTK_ALIGN_END);
		gtk_widget_set_margin_start(btnBox, 12);
		gtk_widget_set_margin_end(btnBox, 12);
		gtk_widget_set_margin_bottom(btnBox, 12);
		gtk_box_append(GTK_BOX(dlgBox), dlgLabel);
		gtk_box_append(GTK_BOX(dlgBox), btnBox);
		gtk_window_set_child(GTK_WINDOW(dialog), dlgBox);
		GtkWidget* btnCancel = gtk_button_new_with_label(cancel_label);
		GtkWidget* btnAccept = gtk_button_new_with_label(accept_label);
		gtk_box_append(GTK_BOX(btnBox), btnCancel);
		gtk_box_append(GTK_BOX(btnBox), btnAccept);

		bool bConfirmed = false;
		GMainLoop *loop = g_main_loop_new(NULL, FALSE);
		struct { bool confirmed; GMainLoop *loop; } d = { false, loop };
		g_signal_connect(btnCancel, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer user_data){
			auto *d2 = (decltype(&d))user_data;
			d2->confirmed = false;
			g_main_loop_quit(d2->loop);
		}), &d);
		g_signal_connect(btnAccept, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer user_data){
			auto *d2 = (decltype(&d))user_data;
			d2->confirmed = true;
			g_main_loop_quit(d2->loop);
		}), &d);
		gtk_window_present(GTK_WINDOW(dialog));
		g_main_loop_run(loop);
		bConfirmed = d.confirmed;
		g_main_loop_unref(loop);
		gtk_window_destroy(GTK_WINDOW(dialog));
		return bConfirmed;
	}

	/* Modal single-line text prompt (for renaming files/folders).  Returns a
	 * g_malloc'd string owned by the caller, or NULL when cancelled. */
	char* PromptForString(const char *title, const char *prompt, const char *initial)
	{
		GtkWidget* dialog = gtk_window_new();
		gtk_window_set_title(GTK_WINDOW(dialog), title);
		gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
		gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
		GtkWidget* dlgBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
		GtkWidget* dlgLabel = gtk_label_new(prompt);
		gtk_label_set_xalign(GTK_LABEL(dlgLabel), 0.0);
		gtk_widget_set_margin_start(dlgLabel, 12);
		gtk_widget_set_margin_end(dlgLabel, 12);
		gtk_widget_set_margin_top(dlgLabel, 12);
		gtk_widget_set_margin_bottom(dlgLabel, 4);
		GtkWidget* entry = gtk_entry_new();
		gtk_widget_set_hexpand(entry, TRUE);
		gtk_editable_set_text(GTK_EDITABLE(entry), initial ? initial : "");
		gtk_widget_set_margin_start(entry, 12);
		gtk_widget_set_margin_end(entry, 12);
		GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_widget_set_halign(btnBox, GTK_ALIGN_END);
		gtk_widget_set_margin_start(btnBox, 12);
		gtk_widget_set_margin_end(btnBox, 12);
		gtk_widget_set_margin_bottom(btnBox, 12);
		gtk_widget_set_margin_top(btnBox, 4);
		gtk_box_append(GTK_BOX(dlgBox), dlgLabel);
		gtk_box_append(GTK_BOX(dlgBox), entry);
		gtk_box_append(GTK_BOX(dlgBox), btnBox);
		gtk_window_set_child(GTK_WINDOW(dialog), dlgBox);
		GtkWidget* btnCancel = gtk_button_new_with_label("Cancel");
		GtkWidget* btnAccept = gtk_button_new_with_label("Rename");
		gtk_widget_add_css_class(btnAccept, "suggested-action");
		gtk_box_append(GTK_BOX(btnBox), btnCancel);
		gtk_box_append(GTK_BOX(btnBox), btnAccept);

		GMainLoop *loop = g_main_loop_new(NULL, FALSE);
		const int PROMPT_ACCEPTED = 1, PROMPT_CANCELLED = 2;
		int result = 0;

		struct { GMainLoop *loop; int *result; } d = { loop, &result };

		auto accept_clicked = +[](GtkWidget*, gpointer user_data) {
			auto *d2 = (decltype(&d))user_data;
			*d2->result = PROMPT_ACCEPTED;
			g_main_loop_quit(d2->loop);
		};
		auto cancel_clicked = +[](GtkWidget*, gpointer user_data) {
			auto *d2 = (decltype(&d))user_data;
			*d2->result = PROMPT_CANCELLED;
			g_main_loop_quit(d2->loop);
		};
		g_signal_connect(btnCancel, "clicked", G_CALLBACK(cancel_clicked), &d);
		g_signal_connect(btnAccept, "clicked", G_CALLBACK(accept_clicked), &d);
		/* Enter in the entry behaves like pressing the Rename button. */
		g_signal_connect(entry, "activate", G_CALLBACK(accept_clicked), &d);
		gtk_window_present(GTK_WINDOW(dialog));
		g_main_loop_run(loop);
		gchar *answer = NULL;
		if (PROMPT_ACCEPTED == result)
		{
			const char *text = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(entry)));
			if (text != NULL && text[0] != '\0')
				answer = g_strdup(text);
		}
		g_main_loop_unref(loop);
		gtk_window_destroy(GTK_WINDOW(dialog));
		return answer;
	}

	static void bg_provider_cleanup(gpointer data)
	{
		GtkCssProvider *provider = GTK_CSS_PROVIDER(data);
		GdkDisplay *display = gdk_display_get_default();
		if (display != NULL)
		{
			gtk_style_context_remove_provider_for_display(display, GTK_STYLE_PROVIDER(provider));
		}
		g_object_unref(provider);
	}

	void SetWidgetBgColor(GtkWidget *widget, const GdkRGBA *color)
	{
		if (widget == NULL)
			return;

		static guint class_counter = 0;

		GtkCssProvider *provider = (GtkCssProvider *)g_object_get_data(G_OBJECT(widget), "quiver-bg-provider");
		gchar *class_name = (gchar *)g_object_get_data(G_OBJECT(widget), "quiver-bg-class");

		if (color == NULL)
		{
			if (class_name != NULL)
			{
				gtk_widget_remove_css_class(widget, class_name);
				g_object_steal_data(G_OBJECT(widget), "quiver-bg-class");
				g_free(class_name);
			}
			if (provider != NULL)
			{
				g_object_set_data(G_OBJECT(widget), "quiver-bg-provider", NULL);
			}
			return;
		}

		gchar *color_str = gdk_rgba_to_string(color);

		if (provider == NULL)
		{
			provider = gtk_css_provider_new();
			class_name = g_strdup_printf("quiver-bg-%u", ++class_counter);

			gchar *css = g_strdup_printf(".%s { background-color: %s; }", class_name, color_str);
			gtk_css_provider_load_from_string(provider, css);
			g_free(css);

			GdkDisplay *display = gdk_display_get_default();
			if (display != NULL)
			{
				gtk_style_context_add_provider_for_display(
					display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
			}

			gtk_widget_add_css_class(widget, class_name);
			g_object_set_data_full(G_OBJECT(widget), "quiver-bg-class", class_name, g_free);
			g_object_set_data_full(G_OBJECT(widget), "quiver-bg-provider", provider, bg_provider_cleanup);
		}
		else
		{
			gchar *css = g_strdup_printf(".%s { background-color: %s; }", class_name, color_str);
			gtk_css_provider_load_from_string(provider, css);
			g_free(css);
		}

		g_free(color_str);
	}

	const char* GetSpecialFolderIconName(const char* path_or_uri)
	{
		if (NULL == path_or_uri || '\0' == path_or_uri[0])
			return NULL;

		char* path = NULL;
		if (g_str_has_prefix(path_or_uri, "file://"))
		{
			path = g_filename_from_uri(path_or_uri, NULL, NULL);
		}
		else
		{
			path = g_strdup(path_or_uri);
		}

		if (NULL == path)
			return NULL;

		// Strip trailing slashes (unless path is "/")
		size_t len = strlen(path);
		while (len > 1 && (path[len - 1] == '/' || path[len - 1] == '\\'))
		{
			path[len - 1] = '\0';
			len--;
		}

		const char* home_dir = g_get_home_dir();
		if (NULL != home_dir && 0 == g_strcmp0(path, home_dir))
		{
			g_free(path);
			return "user-home";
		}

		struct SpecialDirMapping {
			GUserDirectory dir_type;
			const char* fallback_subdir;
			const char* icon_name;
		};

		static const SpecialDirMapping mappings[] = {
			{ G_USER_DIRECTORY_DESKTOP, "Desktop", "user-desktop" },
			{ G_USER_DIRECTORY_DOCUMENTS, "Documents", "folder-documents" },
			{ G_USER_DIRECTORY_DOWNLOAD, "Downloads", "folder-download" },
			{ G_USER_DIRECTORY_MUSIC, "Music", "folder-music" },
			{ G_USER_DIRECTORY_PICTURES, "Pictures", "folder-pictures" },
			{ G_USER_DIRECTORY_PUBLIC_SHARE, "Public", "folder-publicshare" },
			{ G_USER_DIRECTORY_TEMPLATES, "Templates", "folder-templates" },
			{ G_USER_DIRECTORY_VIDEOS, "Videos", "folder-videos" }
		};

		const char* matched_icon = NULL;

		for (const auto& m : mappings)
		{
			const char* special_dir = g_get_user_special_dir(m.dir_type);
			if (NULL != special_dir && 0 != g_strcmp0(special_dir, home_dir))
			{
				char* norm_special = g_strdup(special_dir);
				size_t slen = strlen(norm_special);
				while (slen > 1 && (norm_special[slen - 1] == '/' || norm_special[slen - 1] == '\\'))
				{
					norm_special[slen - 1] = '\0';
					slen--;
				}
				if (0 == g_strcmp0(path, norm_special))
				{
					matched_icon = m.icon_name;
					g_free(norm_special);
					break;
				}
				g_free(norm_special);
			}

			if (NULL != home_dir && NULL != m.fallback_subdir)
			{
				char* fallback_path = g_build_filename(home_dir, m.fallback_subdir, NULL);
				if (0 == g_strcmp0(path, fallback_path))
				{
					matched_icon = m.icon_name;
					g_free(fallback_path);
					break;
				}
				g_free(fallback_path);
			}
		}

		g_free(path);
		return matched_icon;
	}

	const char* GetSpecialFolderIconName(GFile* file)
	{
		if (NULL == file)
			return NULL;

		char* path = g_file_get_path(file);
		if (NULL != path)
		{
			const char* icon = GetSpecialFolderIconName(path);
			g_free(path);
			return icon;
		}

		char* uri = g_file_get_uri(file);
		if (NULL != uri)
		{
			const char* icon = GetSpecialFolderIconName(uri);
			g_free(uri);
			return icon;
		}

		return NULL;
	}

	const char* GetSpecialFolderSymbolicIconName(const char* path_or_uri)
	{
		const char* icon = GetSpecialFolderIconName(path_or_uri);
		if (NULL == icon)
			return NULL;

		if (0 == strcmp(icon, "user-home"))
			return "user-home-symbolic";
		if (0 == strcmp(icon, "user-desktop"))
			return "user-desktop-symbolic";
		if (0 == strcmp(icon, "folder-documents"))
			return "folder-documents-symbolic";
		if (0 == strcmp(icon, "folder-download"))
			return "folder-download-symbolic";
		if (0 == strcmp(icon, "folder-music"))
			return "folder-music-symbolic";
		if (0 == strcmp(icon, "folder-pictures"))
			return "folder-pictures-symbolic";
		if (0 == strcmp(icon, "folder-publicshare"))
			return "folder-publicshare-symbolic";
		if (0 == strcmp(icon, "folder-templates"))
			return "folder-templates-symbolic";
		if (0 == strcmp(icon, "folder-videos"))
			return "folder-videos-symbolic";

		return NULL;
	}

	const char* GetSpecialFolderSymbolicIconName(GFile* file)
	{
		if (NULL == file)
			return NULL;

		char* path = g_file_get_path(file);
		if (NULL != path)
		{
			const char* icon = GetSpecialFolderSymbolicIconName(path);
			g_free(path);
			return icon;
		}

		char* uri = g_file_get_uri(file);
		if (NULL != uri)
		{
			const char* icon = GetSpecialFolderSymbolicIconName(uri);
			g_free(uri);
			return icon;
		}

		return NULL;
	}

	/*---------------------------------------------------------------------
	 * context-menu helpers
	 *---------------------------------------------------------------------
	 *
	 * These replace hand-built popovers full of GtkButtons with real menus.
	 * A context menu must:
	 *  - open exactly at the pointer (any coordinate space -- coordinates are
	 *    translated into the popover's parent widget),
	 *  - move to the new spot when the user right-clicks somewhere else
	 *    (so press-to-dismiss grab mechanics are switched off and a capture
	 *    controller dismisses the menu instead),
	 *  - dismiss on any outside press / Escape.
	 */

	/* The single currently-open context menu (recreated on every show).  The
	 * root-level dismiss controller below closes whatever is active. */
	static GtkPopover *s_active_popover = NULL;

	static void menu_dismiss_popover(void)
	{
		if (s_active_popover && gtk_widget_get_visible(GTK_WIDGET(s_active_popover)))
		{
			gtk_popover_popdown(s_active_popover);
		}
	}

	static gboolean menu_dismiss_cb(GtkEventController *controller, GdkEvent *ev, gpointer user_data)
	{
		(void)controller;
		(void)user_data;
		GdkEventType t = gdk_event_get_event_type(ev);
		if (t == GDK_BUTTON_PRESS)
		{
			/* A right-click is handled by the widget under the cursor (it may
			 * move or dismiss the menu itself); the capture controller only
			 * dismisses on other buttons and on Escape. */
			if (gdk_button_event_get_button(ev) != 3)
				menu_dismiss_popover();
		}
		else if (t == GDK_TOUCH_BEGIN)
		{
			menu_dismiss_popover();
		}
		else if (t == GDK_KEY_PRESS)
		{
			guint keyval = gdk_key_event_get_keyval(ev);
			if (GDK_KEY_Escape == keyval)
			{
				menu_dismiss_popover();
			}
		}
		return GDK_EVENT_PROPAGATE;
	}

	static void wire_menu_dismissal(GtkPopover *popover)
	{
		(void)popover;
		GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(popover));
		if (root == NULL || !GTK_IS_WINDOW(root))
			return;
		if (g_object_get_data(G_OBJECT(root), "quiver-menu-dismiss-wired"))
			return;
		g_object_set_data(G_OBJECT(root), "quiver-menu-dismiss-wired", GINT_TO_POINTER(1));

		GtkEventController *legacy = gtk_event_controller_legacy_new();
		gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(legacy), GTK_PHASE_CAPTURE);
		g_signal_connect(legacy, "event", G_CALLBACK(menu_dismiss_cb), NULL);
		gtk_widget_add_controller(GTK_WIDGET(root), GTK_EVENT_CONTROLLER(legacy));
	}

	void ShowContextMenuAt(GtkPopover *popover, GtkWidget *anchor_widget, gdouble x, gdouble y)
	{
		if (NULL == popover)
			return;

		GtkWidget *parent_widget = gtk_widget_get_parent(GTK_WIDGET(popover));

		wire_menu_dismissal(popover);

		if (x >= 0 && y >= 0)
		{
			GdkRectangle rect;
			rect.width = 1;
			rect.height = 1;
			rect.x = (int)x;
			rect.y = (int)y;

			/* The pointing-to rect uses the popover parent's coordinate space;
			 * the press reports coordinates in the anchor widget's space. */
			if (anchor_widget != NULL && parent_widget != NULL && anchor_widget != parent_widget)
			{
				graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y);
				graphene_point_t out;
				if (gtk_widget_compute_point(anchor_widget, parent_widget, &in, &out))
				{
					rect.x = (int)out.x;
					rect.y = (int)out.y;
				}
			}
			gtk_popover_set_pointing_to(popover, &rect);
		}

		gtk_popover_popup(popover);

		/* A GtkPopoverMenu inserts its section separators from an idle
		 * callback, so the very first present sizes the surface slightly
		 * smaller than the finished menu; the menu then ends up with a
		 * spurious vertical scrollbar.  Re-present once on an idle that runs
		 * after GTK's own separator sync so the surface is resized to the
		 * menu's full natural height. */
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
			+[](gpointer d) -> gboolean {
			GtkPopover *p = GTK_POPOVER(d);
			if (gtk_widget_get_visible(GTK_WIDGET(p)) &&
			    gtk_widget_get_parent(GTK_WIDGET(p)) != NULL)
			{
				gtk_widget_queue_resize(GTK_WIDGET(p));
				gtk_popover_present(p);
			}
			g_object_unref(p);
			return G_SOURCE_REMOVE;
		}, g_object_ref(popover), NULL);
	}

	void MenuAppendAction(GMenu *menu, const char *label, const char *action_name, const char *accel)
	{
		GMenuItem *item = g_menu_item_new(label, action_name);
		if (accel != NULL && accel[0] != '\0')
		{
			g_menu_item_set_attribute(item, "accel", "s", accel);
		}
		g_menu_append_item(menu, item);
		g_object_unref(item);
	}

	void MenuAppendItem(GMenu *menu, GMenuItem *item)
	{
		g_menu_append_item(menu, item);
	}

	/* Title row for the top of a context menu: the file name on the first
	 * line (bold) and its folder on the second (dimmed, wrapped). */
	GtkWidget* MakeMenuTitleLabel(const char *name, const char *location)
	{
		gchar *esc_name = g_markup_escape_text(name ? name : "", -1);
		gchar *esc_loc  = g_markup_escape_text(location ? location : "", -1);
		gchar *markup = g_strdup_printf("<b>%s</b>\n%s", esc_name, esc_loc);
		GtkWidget *label = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(label), markup);
		gtk_label_set_wrap(GTK_LABEL(label), TRUE);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
		gtk_widget_set_halign(label, GTK_ALIGN_FILL);
		gtk_widget_set_margin_top(label, 6);
		gtk_widget_set_margin_bottom(label, 2);
		gtk_widget_set_margin_start(label, 10);
		gtk_widget_set_margin_end(label, 10);
		gtk_widget_add_css_class(label, "dim-label");
		g_free(markup);
		g_free(esc_loc);
		g_free(esc_name);
		return label;
	}

}
