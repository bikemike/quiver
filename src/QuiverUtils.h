#ifndef FILE_QUIVERUTILS_H
#define FILE_QUIVERUTILS_H

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string>

#include "QuiverFileOps.h"

namespace QuiverUtils
{
	GdkTexture * TextureExifReorientate(GdkTexture * texture, int orientation);
	GdkTexture * ScaleTexture(GdkTexture * texture, int dest_w, int dest_h);
#if HAVE_GDK_PIXBUF
	GdkTexture * PixbufToTexture(GdkPixbuf * pixbuf);
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation);
#endif

	/* Filmstrip (sprocket-hole) decoration for video thumbnails.
	 *
	 * The libquiver icon view draws a strip of sprocket holes along the left
	 * and right edges of each video thumbnail at snapshot time.  `thumb_natural_max_dim`
	 * is the larger of the loaded thumbnail's natural width and height, which
	 * matches the size the thumbnail the loader produced (see IconViewThumbLoader):
	 * normal 128px thumbnails get the small filmstrip.png pattern, while
	 * 256px thumbnails get the dedicated filmstrip-big.png pattern.  The strip
	 * is scaled with the same ratio as the thumbnail when drawn, so it always
	 * matches the thumbnail at any drawn size.  The strip is transient: drawn
	 * on top of the thumbnail only, never baked into the cached or saved
	 * thumbnail. */
	std::string GetFilmstripPath(gint thumb_natural_max_dim);

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

	/* Modal "OK / Cancel style" confirmation dialog.  Shows `message` (wrapped)
	 * with `accept_label` on the accept button.  Returns TRUE when accepted.
	 * GTK4 has no gtk_dialog_run(), so this runs its own nested GMainLoop.
	 * Must be called from the GUI thread. */
	bool ConfirmDialog(const char *title, const std::string& message,
		const char *accept_label = "OK", const char *cancel_label = "Cancel");

	/* Modal single-line text prompt (for renaming files/folders or creating folders).  Returns a
	 * g_malloc'd string owned by the caller, or NULL when cancelled. */
	char* PromptForString(const char *title, const char *prompt, const char *initial, const char *accept_label = NULL);

	/* Determine the character length of the base name (excluding extension).
	 * Returns -1 if the full string is the base name. */
	glong GetBasenameCharLength(const char *filename);

	/* Compute the renamed filename given the original filename and user input.
	 * If original filename had an extension and the entered name lacks one,
	 * the original extension is appended. */
	std::string ResolveRenameString(const char *initial, const char *entered);

	/* Create a GFile from either a URI (file://...) or a local filesystem path */
	GFile* FileFromURIOrPath(const char *uri_or_path);

	/* Normalize a path or URI to a canonical URI string */
	std::string NormalizeURI(const char *uri_or_path);

	/* Check if a URI or path points to an existing directory */
	bool IsDirectoryURI(const char *uri);

	/* Generate a unique folder name like "New Folder", "New Folder 2", etc. */
	std::string GetUniqueFolderName(GFile *parent, const char *base_name);

	/* Modal overwrite/skip prompt for one paste or drop name conflict.
	 *
	 * `src_uri` / `dest_uri` identify the colliding pair.  When the user
	 * ticks "do this for the remaining conflicts" the choice is remembered by
	 * setting *apply_to_all (the caller then stops prompting).  Returns
	 * QuiverFileOps::PASTE_OVERWRITE when the existing destination should be
	 * replaced, else PASTE_SKIP.
	 * GTK4 has no gtk_dialog_run(), so it runs its own nested GMainLoop.
	 * Must be called from the GUI thread. */
	QuiverFileOps::PasteConflictAction ResolvePasteConflict(const char *src_uri,
		const char *dest_uri, bool& apply_to_all);

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

	/* Context-menu helpers.
	 *
	 * ShowContextMenuAt() pops a context menu up at (x, y), where the point is
	 * given in `anchor_widget`'s coordinate space and is translated into the
	 * popover's parent widget before being used as the pointing-to rect.  It
	 * turns off the popover's press-outside-dismiss-grab, moves the menu when
	 * it is already open and another location is right-clicked, wires a
	 * capture controller that dismisses the menu on any outside press or
	 * Escape, and pops it up.  The popover must already be parented (GTK 4
	 * requires a parent before popup()). */
	void ShowContextMenuAt(GtkPopover *popover, GtkWidget *anchor_widget, gdouble x, gdouble y);

	/* Build a menu model with standard-looking items.  `action_name` is the
	 * full action prefix + name (e.g. "quiver.BrowserCopy"); `accel` is an
	 * optional shortcut string ("<Control>c", "F2") shown as a hint. */
	void MenuAppendAction(GMenu *menu, const char *label, const char *action_name, const char *accel);
	void MenuAppendItem(GMenu *menu, GMenuItem *item);

	/* Title row for the top of a context menu (file name + folder). */
	GtkWidget* MakeMenuTitleLabel(const char *name, const char *location);

	/* Traverses a GtkPopoverMenu hierarchy and ensures that all GtkImage icons
	 * associated with menu items are made visible. */
	void EnablePopoverMenuIcons(GtkWidget *popover);
}

#endif
