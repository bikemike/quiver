#ifndef FILE_QUIVERUTILS_H
#define FILE_QUIVERUTILS_H

#include <gtk/gtk.h>
#include <gio/gio.h>

namespace QuiverUtils
{
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation);

	/* New GSimpleAction based action system (replaces GtkUIManager/GtkAction).
	 *
	 * The shared GSimpleActionGroup takes the place of the old GtkUIManager.
	 * Actions registered through the factories below are looked up by name
	 * with GetAction(), and their widgets are bound with BindWidget()
	 * (GtkActionable), which also handles sensitive/toggle state propagation.
	 * Keyboard accelerators are managed with a shared GtkAccelGroup. */
	typedef void (*QuiverActionCallback)(GSimpleAction *action, GVariant *parameter, gpointer user_data);

	void InitActions();                              // idempotent, call once from Quiver::Init()
	GSimpleActionGroup* GetActionGroup();
	void AddAction(GAction *action);
	void RemoveAction(const char *action_name);      // for dynamically reloaded action lists
	GAction* GetAction(const char *action_name);     // overload of the legacy GetAction
	void SetActionsSensitive(const gchar **actions, gint n_actions, gboolean bSensitive); // overload

	GSimpleAction* AddSimpleAction(const char *name, const gchar *accel, QuiverActionCallback cb, gpointer user_data);
	GSimpleAction* AddToggleAction(const char *name, const gchar *accel, gboolean active, QuiverActionCallback cb, gpointer user_data);
	void AddRadioActions(const char *const names[], const gint values[], gint n, gint current_value, QuiverActionCallback cb, gpointer user_data);

	gboolean ToggleActionGetActive(const char *action_name);
	void ToggleActionSetActive(const char *action_name, gboolean active);
	void ToggleActionSetState(const char *action_name, gboolean active);
	gint GetRadioActionCurrent(const char *action_name);
	void SetRadioActionCurrent(const char *action_name, gint value);

	void AddAccelGroup(GtkWindow *window);
	void DisconnectUnmodifiedAccelerators();         // overload
	void ConnectUnmodifiedAccelerators();            // overload

	void BindWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name);
	void BindBuilderAccelerators(GtkBuilder *builder);

	/* Bind a toggle or radio widget (GtkCheckMenuItem / GtkToggleToolButton /
	 * GtkRadioMenuItem) to a toggle/radio action. Unlike BindWidget these
	 * connect the widget's "toggled" signal and the action's state changes
	 * explicitly, because the GtkActionable binding sends NULL parameters for
	 * boolean stateful actions (which triggers a GTK warning and no-op). */
	void BindToggleWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name);
	void BindRadioWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name);
}

#endif
