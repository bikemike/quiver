#ifndef FILE_QUIVERUTILS_H
#define FILE_QUIVERUTILS_H

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string>
#include <list>

#include "QuiverFileOps.h"

namespace QuiverUtils
{
	/* Quick-rename of one file, shared by the browser and the viewer.  The
	 * browser and the viewer each used to register their own "F2" action, and
	 * an app-wide accel belongs to a single action, so whichever registered
	 * last won it - F2 in the viewer could run the browser's rename, which
	 * acted on the browser's selection and offered the wrong file.  There is
	 * one action now, aimed at whichever pane is showing. */
#define ACTION_QUIVER_QUICK_RENAME "QuickRename"

	/* Prompt user to add a bookmark for the given list of URIs.
	 * Displays BookmarkAddEditDlg prefilled with folder names.
	 * Returns true if the bookmark was created and added. */
	bool PromptAddBookmark(const std::list<std::string>& uris);
	GdkTexture * TextureExifReorientate(GdkTexture * texture, int orientation);
	/* The turn that gets a texture already at orientation @current round to
	 * orientation @wanted, or 1 when the two already agree - so getting from a
	 * 6 to an 8 is a 3.  Turning a texture by this is what keeps a photo from
	 * being turned twice, which is easy to do when the request is an
	 * orientation rather than a delta from what is already on screen.
	 *
	 * Values outside 1-8 are folded to 1 rather than used as an index, so a
	 * nonsensical stored or requested orientation cannot read off the end of
	 * the table. */
	int ExifOrientationTurn(int current, int wanted);

	/* Where the pixels end up, and what has to be done to get them there.
	 *
	 * This is the pairing that the cache stamp depends on. Asking for an
	 * orientation is not a request for a turn: a texture whose pixels already
	 * sit at orientation 6 and a request for orientation 6 need no turn at
	 * all, and one that needs a turn of 6 lands on the requested orientation
	 * rather than on 6.  Stamping the turn, or the orientation the pixels
	 * happened to start at, makes the next load turn an already-correct image
	 * a second time, which is how a file opens upright and then appears
	 * rotated the moment it is reached any other way.
	 *
	 * pixels_at is where the texture is now (use the file's own orientation
	 * after decoding); wanted is what was asked for.  *turn comes back as the
	 * transform to apply - 1 when the texture is already correct - and the
	 * return value is where the pixels sit once it has been, which is always
	 * wanted. */
	int ExifOrientationApplied(int pixels_at, int wanted, int *turn);

	GdkTexture * ScaleTexture(GdkTexture * texture, int dest_w, int dest_h);
#if HAVE_GDK_PIXBUF
	GdkTexture * PixbufToTexture(GdkPixbuf * pixbuf);
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation);
#endif

	/* Filmstrip (sprocket-hole) decoration for video thumbnails.
	 *
	 * The libquiver icon view draws a strip of sprocket holes along the left
	 * and right edges of each video thumbnail at snapshot time.  `thumb_dim`
	 * is the drawn thumbnail dimension: 128px thumbnails (or thumbnails scaled
	 * smaller than 128) get the small filmstrip.png pattern, while 256px
	 * thumbnails (or thumbnails scaled smaller than 256) get the dedicated
	 * filmstrip-big.png pattern.  The strip is scaled with the same ratio as
	 * the thumbnail when drawn, so it always matches the thumbnail at any drawn
	 * size.  The strip is transient: drawn on top of the thumbnail only, never
	 * baked into the cached or saved thumbnail. */
	std::string GetFilmstripPath(gint thumb_dim);

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

	/* Drop every action `owner` registered.  The action group is global and
	 * lives as long as the application, so a component that registers
	 * actions with its own pointer as user_data and then goes away would
	 * leave callbacks pointing at freed memory: the next activation (an
	 * action toggled from anywhere, a menu item, a keybinding) would call
	 * into it.  One action exists per name, so only the actions still owned
	 * by `owner` are removed - a name another component has since
	 * registered stays. */
	void RemoveActionsFor(gpointer owner);
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
	void SuppressAllAccelerators(bool suppress);

	/* Some keys (F2, Delete, Shift+Delete, Ctrl+C, Ctrl+X) are claimed by both
	 * the browser and the viewer.  An app-wide accel belongs to one action, so
	 * the pane that is showing keeps those keys and the hidden one gives them
	 * up.  Call this whenever the visible pane changes. */
	void SetActivePane(bool bViewer);

	/* Modal "OK / Cancel style" confirmation dialog.  Shows `message` (wrapped)
	 * with `accept_label` on the accept button.  Returns TRUE when accepted.
	 * GTK4 has no gtk_dialog_run(), so this runs its own nested GMainLoop.
	 * Must be called from the GUI thread. */
	bool ConfirmDialog(const char *title, const std::string& message,
		const char *accept_label = "OK", const char *cancel_label = "Cancel");

	/* Modal single-line text prompt (for renaming files/folders or creating folders).  Returns a
	 * g_malloc'd string owned by the caller, or NULL when cancelled. */
	char* PromptForString(const char *title, const char *prompt, const char *initial, const char *accept_label = NULL);

	/* Prompt for a new name for `file` and rename it in place.  Shared by the
	 * browser and the viewer so the two cannot drift apart.  Returns the new
	 * URI (caller frees), or NULL if the user cancelled or the rename failed
	 * (the failure is reported to the user). */
	char* PromptAndRenameFile(const QuiverFile& file);

	/* True for an EXIF row that is a MakerNote decode artifact rather than
	 * information.  MakerNotes are undocumented vendor blobs; Exiv2 walks them
	 * as extra IFDs, and when a camera encrypts its MakerNote (Sony "2010e"
	 * does) the walk runs off the end of the real data and invents thousands
	 * of unnamed tags.  A row Exiv2 could not even name (its key looks like
	 * "0x…") in a vendor group rather than a standard one is such an artifact.
	 * Everything Exiv2 could name still shows, as does all of
	 * Image/Photo/Iop/Thumbnail/GPSInfo. */
	bool IsMakernoteArtifact(const std::string& group_name, const std::string& key);

	/* True for an EXIF row whose value is a raw byte blob rather than
	 * something readable - MakerNote, PrintImageMatching and friends, which
	 * Exiv2 prints as space-separated decimal numbers.  Their content is
	 * already shown as decoded vendor tags, so the table skips them. */
	bool IsBinaryExifBlob(const std::string& key);

	/* True for the EXIF groups the standard defines, false for a camera
	 * maker's own groups (Sony1, Nikon3, CanonCs, ...).  The EXIF view uses
	 * this to keep the standard tags together and put the vendor blob data
	 * below them in their own section. */
	bool IsStandardExifGroup(const std::string& group_name);

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

	/* Return a friendly display string for a URI or path.
	 * Local file:// URI is converted to the filesystem path; other schemes
	 * and plain local paths are returned unchanged. */
	std::string GetDisplayPath(const char *uri_or_path);

	/* Return the decoded basename of a URI, for display only.
	 *
	 * GetURI() percent-encodes, so g_path_get_basename() on a URI yields
	 * "my%20photo.jpg" rather than "my photo.jpg".  This goes through the
	 * GFile and decodes.  QuiverFile::GetFileName() is the same thing as an
	 * instance method; use this variant where no QuiverFile exists (or where
	 * building one would be a costly g_file_query_info, as in menu builders).
	 * Never use the result to name a file on disk - that needs the unescaped
	 * path from GFile, not a UI label. */
	std::string GetDisplayBasename(const char *uri);

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
