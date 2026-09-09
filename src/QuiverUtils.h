#ifndef FILE_QUIVERUTILS_H
#define FILE_QUIVERUTILS_H

#include <gtk/gtk.h>
#include <gio/gio.h>

namespace QuiverUtils
{
	GdkTexture * TextureExifReorientate(GdkTexture * texture, int orientation);
	GdkTexture * ScaleTexture(GdkTexture * texture, int dest_w, int dest_h);
#if HAVE_GDK_PIXBUF
	GdkTexture * PixbufToTexture(GdkPixbuf * pixbuf);
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation);
#endif

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

	/* Grab keyboard focus on `widget` so it can receive key events (e.g.
	 * arrow-key navigation in the browser/icon view and viewer).  GTK4's
	 * gtk_widget_grab_focus() only succeeds once the widget is mapped and
	 * focusable, so if it is not mapped yet (common right after ShowViewer()/
	 * ShowBrowser() switch the UI before the frame is laid out), a one-shot
	 * handler grabs focus as soon as the widget is mapped.  Returns TRUE if
	 * focus was grabbed immediately. */
	gboolean GrabFocusForWidget(GtkWidget *widget);

	void BindWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name);
	void BindBuilderAccelerators(GtkBuilder *builder);

	/* Bind a toggle or radio widget (GtkCheckMenuItem / GtkToggleToolButton /
	 * GtkRadioMenuItem) to a toggle/radio action. Unlike BindWidget these
	 * connect the widget's "toggled" signal and the action's state changes
	 * explicitly, because the GtkActionable binding sends NULL parameters for
	 * boolean stateful actions (which triggers a GTK warning and no-op). */
	void BindToggleWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name);
	void BindRadioWidget(GtkWidget *widget, GtkWidget *ancestor, const char *action_name);

	/* Set or clear custom background color on a widget via a scoped GTK4 CSS provider.
	 * If color is NULL, any previously attached background provider and class are
	 * removed, restoring default system theme styling. */
	void SetWidgetBgColor(GtkWidget *widget, const GdkRGBA *color);

	/* Returns the Freedesktop themed icon name (e.g. "user-desktop", "folder-documents",
	 * "folder-download", "folder-music", "folder-pictures", "folder-publicshare",
	 * "folder-templates", "folder-videos", "user-home") if the given path or URI
	 * corresponds to the user's home directory or an XDG user directory.
	 * Returns NULL if the folder does not match a special user directory. */
	const char* GetSpecialFolderIconName(const char* path_or_uri);
	const char* GetSpecialFolderIconName(GFile* file);

	/* Returns the Freedesktop symbolic themed icon name (e.g. "user-desktop-symbolic",
	 * "folder-documents-symbolic", "folder-pictures-symbolic", "user-home-symbolic")
	 * for XDG user directories and the user home directory, or NULL if not special. */
	const char* GetSpecialFolderSymbolicIconName(const char* path_or_uri);
	const char* GetSpecialFolderSymbolicIconName(GFile* file);
}

#endif
