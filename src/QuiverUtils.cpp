#include <config.h>
#include "QuiverUtils.h"

static bool g_bAcceleratorsDisabled = false;

#define N_LOOPS 10

namespace QuiverUtils
{
	
	GtkAction* GetAction(GtkUIManager* ui,const char * action_name)
	{
		GList * action_groups = gtk_ui_manager_get_action_groups(ui);
		GtkAction * action = NULL;
		while (NULL != action_groups)
		{
			action = gtk_action_group_get_action (GTK_ACTION_GROUP(action_groups->data),action_name);
			if (NULL != action)
			{
				break;
			}                      
			action_groups = g_list_next(action_groups);
		}
	
		return action;
	}
	
	
	void SetActionsSensitive(GtkUIManager *pUIManager, const gchar** actions, gint n_actions, gboolean bSensitive)
	{
		gint i;
		for ( i = 0; i < n_actions; i++)
		{
			GtkAction* action = QuiverUtils::GetAction(pUIManager, actions[i]);
			if (NULL != action)
			{
				gtk_action_set_sensitive(action,bSensitive);
			}
		}
	}
	
	
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation)
	{
		//printf("orientation is: %d\n",orientation);

		/*
		  1        2       3      4         5            6           7          8
		888888  888888      88  88      8888888888  88                  88  8888888888
		88          88      88  88      88  88      88  88          88  88      88  88
		8888      8888    8888  8888    88          8888888888  8888888888          88
		88          88      88  88
		88          88  888888  888888
		1 = no rotation
		2 = flip h
		3 = rotate 180
		4 = flip v
		5 = flip v, rotate 90
		6 = rotate 90
		7 = flip v, rotate 270
		8 = rotae 270 


		*/
		//get rotaiton
		
		GdkPixbuf * modified = NULL;

		switch (orientation)
		{
			case 1:
				//1 = no rotation
				break;
			case 2:
				//2 = flip h
				modified = gdk_pixbuf_flip(pixbuf,TRUE);
				break;
			case 3:
				//3 = rotate 180
				modified = gdk_pixbuf_rotate_simple(pixbuf,(GdkPixbufRotation)180);
				break;
			case 4:
				//4 = flip v
				modified = gdk_pixbuf_flip(pixbuf,FALSE);
				break;
			case 5:
				//5 = flip v, rotate 90
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
				//7 = flip v, rotate 270
				{
					GdkPixbuf *tmp = gdk_pixbuf_flip(pixbuf,FALSE);
					modified = gdk_pixbuf_rotate_simple(tmp,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
					g_object_unref(tmp);
				}
				break;
			case 8:
				//8 = rotae 270 
				modified = gdk_pixbuf_rotate_simple(pixbuf,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
			default:
				break;
		}
		return modified;
	}


	void DisconnectUnmodifiedAccelerators(GtkUIManager *pUIManager)
	{
		GList * action_groups = gtk_ui_manager_get_action_groups(pUIManager);
		GtkAction * action = NULL;

		if (!g_bAcceleratorsDisabled)
		{		
			while (NULL != action_groups)
			{
				GList *actions_list = gtk_action_group_list_actions((GtkActionGroup*)action_groups->data);
				GList *actions = actions_list;
				while (NULL != actions)
				{
					action = (GtkAction*)actions->data;
					GtkAccelKey accel_key = {0};
					
					if (gtk_accel_map_lookup_entry(gtk_action_get_accel_path(action),&accel_key))
					{
						// list of modifiers to check
						guint mask = 0;
						
						mask |= GDK_CONTROL_MASK;
						mask |= GDK_MOD1_MASK;    // normally alt
						mask |= GDK_MOD2_MASK;
						mask |= GDK_MOD3_MASK;
						mask |= GDK_MOD4_MASK;
						mask |= GDK_MOD5_MASK;
	
						gchar *accel_label;
						accel_label = gtk_accelerator_get_label(accel_key.accel_key,accel_key.accel_mods);
						//printf("disable  accel key(%d): %s - %d\n",action->private_data->accel_count, accel_label, accel_key.accel_key);
						//printf("disable accel key: %s - %d\n",accel_label, accel_key.accel_key);
	
						if (0 == (mask & accel_key.accel_mods) && 0 != accel_key.accel_key)
						{
							for (int i = 0; i < N_LOOPS; i++)
								gtk_action_disconnect_accelerator(action);
						}
						//printf("disabled accel key(%d): %s - %d\n",action->private_data->accel_count, accel_label, accel_key.accel_key);
						g_free(accel_label);
					}
					actions = g_list_next(actions);
				}
				
				g_list_free(actions_list);
				
				action_groups = g_list_next(action_groups);
			}
			g_bAcceleratorsDisabled = true;
		}
	}
	
	void ConnectUnmodifiedAccelerators(GtkUIManager *pUIManager)
	{
		GList * action_groups = gtk_ui_manager_get_action_groups(pUIManager);
		GtkAction * action = NULL;
		if (g_bAcceleratorsDisabled)
		{
			while (NULL != action_groups)
			{
				GList *actions_list = gtk_action_group_list_actions((GtkActionGroup*)action_groups->data);
				GList *actions = actions_list;
				while (NULL != actions)
				{
					action = (GtkAction*)actions->data;
					GtkAccelKey accel_key = {0};
					
					if (gtk_accel_map_lookup_entry(gtk_action_get_accel_path(action),&accel_key))
					{
						// list of modifiers to check
						guint mask = 0;
						
						mask |= GDK_CONTROL_MASK;
						mask |= GDK_MOD1_MASK;    // normally alt
						mask |= GDK_MOD2_MASK;
						mask |= GDK_MOD3_MASK;
						mask |= GDK_MOD4_MASK;
						mask |= GDK_MOD5_MASK;
	
						if (0 == (mask & accel_key.accel_mods) && 0 != accel_key.accel_key)
						{
							gchar *accel_label;
							accel_label = gtk_accelerator_get_label(accel_key.accel_key,accel_key.accel_mods);
							//printf("enable accel key(%d): %s - %d\n",action->private_data->accel_count, accel_label, accel_key.accel_key);
							//printf("enable accel key: %s - %d\n",accel_label, accel_key.accel_key);
							for (int i = 0; i < N_LOOPS; i++)
								gtk_action_connect_accelerator(action);
							g_free(accel_label);
						}
					}
					actions = g_list_next(actions);
				}
				
				g_list_free(actions_list);
				
				action_groups = g_list_next(action_groups);
			}
			g_bAcceleratorsDisabled = false;
		}
	}
}

#include "QuiverUtils.h"
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
	static GtkAccelGroup *g_pAccelGroup = NULL;
	static GPtrArray *g_accelEntries = NULL;
	static GPtrArray *g_radioGroups = NULL;
	static bool g_bAcceleratorsSuppressed = false;

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

	static gboolean accel_activate_cb(gpointer data1, gpointer arg1, guint arg2, guint arg3, gpointer data2) {
		GAction *action = G_ACTION(data2);
		if (NULL == action || !G_IS_ACTION(action)) return TRUE;
		const GVariantType *ptype = g_action_get_parameter_type(action);
		if (NULL != ptype && g_variant_type_equal(ptype, G_VARIANT_TYPE_BOOLEAN))
		{
			GVariant *state = g_action_get_state(action);
			gboolean active = (NULL != state) ? g_variant_get_boolean(state) : FALSE;
			if (NULL != state) g_variant_unref(state);
			g_action_activate(action, g_variant_new_boolean(!active));
		}
		else
		{
			g_action_activate(action, NULL);
		}
		return TRUE;
	}

	static void connect_accel_entry(AccelEntry *entry) {
		if (NULL == g_pAccelGroup || entry->connected || entry->suppressed) return;
		GAction *action = QuiverUtils::GetAction(entry->action_name);
		if (NULL == action) return;
		
		GClosure *closure = g_cclosure_new(G_CALLBACK(accel_activate_cb), action, NULL);
		gtk_accel_group_connect(g_pAccelGroup, entry->keyval, entry->mods, GTK_ACCEL_VISIBLE, closure);
		entry->connected = TRUE;
	}

	static void register_accelerator(const char *action_name, const gchar *accel) {
		guint keyval;
		GdkModifierType mods;
		gtk_accelerator_parse(accel, &keyval, &mods);
		if (keyval == 0) return;

		AccelEntry *entry = g_new0(AccelEntry, 1);
		entry->action_name = g_strdup(action_name);
		entry->keyval = keyval;
		entry->mods = mods;
		entry->connected = FALSE;
		entry->suppressed = FALSE;

		g_ptr_array_add(g_accelEntries, entry);
		connect_accel_entry(entry);
	}

	static gboolean accel_has_modifier(guint keyval, GdkModifierType mods) {
		guint mask = GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_MOD2_MASK | GDK_MOD3_MASK | GDK_MOD4_MASK | GDK_MOD5_MASK;
		return 0 != (mask & mods);
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
		if (NULL != parameter && g_variant_is_of_type(parameter, G_VARIANT_TYPE_BOOLEAN)) {
			g_simple_action_set_state(action, parameter);
		}
		if (NULL != data->cb) {
			data->cb(action, parameter, data->user_data);
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
					if (entry->connected && NULL != g_pAccelGroup) {
						gtk_accel_group_disconnect_key(g_pAccelGroup, entry->keyval, entry->mods);
					}
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
		GSimpleAction *action = g_simple_action_new_stateful(name, G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(active));
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

	void AddAccelGroup(GtkWindow *window) {
		if (NULL == g_pAccelGroup) {
			g_pAccelGroup = gtk_accel_group_new();
			for (guint i = 0; i < g_accelEntries->len; i++) {
				AccelEntry *entry = (AccelEntry*)g_ptr_array_index(g_accelEntries, i);
				connect_accel_entry(entry);
			}
		}
		gtk_window_add_accel_group(window, g_pAccelGroup);
	}

	void DisconnectUnmodifiedAccelerators() {
		if (NULL == g_pAccelGroup || NULL == g_accelEntries) return;
		if (g_bAcceleratorsSuppressed) return;
		for (guint i = 0; i < g_accelEntries->len; i++) {
			AccelEntry *entry = (AccelEntry*)g_ptr_array_index(g_accelEntries, i);
			if (entry->connected && !accel_has_modifier(entry->keyval, entry->mods)) {
				gtk_accel_group_disconnect_key(g_pAccelGroup, entry->keyval, entry->mods);
				entry->connected = FALSE;
				entry->suppressed = TRUE;
			}
		}
		g_bAcceleratorsSuppressed = true;
	}

	void ConnectUnmodifiedAccelerators() {
		if (NULL == g_pAccelGroup || NULL == g_accelEntries) return;
		if (!g_bAcceleratorsSuppressed) return;
		for (guint i = 0; i < g_accelEntries->len; i++) {
			AccelEntry *entry = (AccelEntry*)g_ptr_array_index(g_accelEntries, i);
			if (entry->suppressed) {
				entry->suppressed = FALSE;
				connect_accel_entry(entry);
			}
		}
		g_bAcceleratorsSuppressed = false;
	}

	void BindWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name) {
		gchar *full_name = g_strdup_printf("quiver.%s", action_name);
		gtk_actionable_set_action_name(GTK_ACTIONABLE(widget), full_name);
		g_free(full_name);
		gtk_widget_insert_action_group(ancestor, "quiver", G_ACTION_GROUP(g_pActionGroup));
	}

	static void toggle_action_state_changed_cb(GObject *object, GParamSpec *pspec, gpointer user_data) {
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
		g_action_activate(action, g_variant_new_boolean(active));
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

	static void radio_action_state_changed_cb(GObject *object, GParamSpec *pspec, gpointer user_data) {
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
		GAction *action = QuiverUtils::GetAction(action_name);
		if (NULL == action) return;
		gtk_widget_insert_action_group(ancestor, "quiver", G_ACTION_GROUP(g_pActionGroup));
		gchar *name = g_strdup(action_name);
		g_object_set_data_full(G_OBJECT(widget), "quiver-action-name", name, g_free);
		g_signal_connect(widget, "toggled", G_CALLBACK(radio_widget_toggled_cb), name);
		g_signal_connect(action, "notify::state", G_CALLBACK(radio_action_state_changed_cb), widget);
		radio_action_state_changed_cb(G_OBJECT(action), NULL, widget);
	}

}
