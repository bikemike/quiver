#ifndef QUIVER_CLIPBOARD_H
#define QUIVER_CLIPBOARD_H

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <list>
#include <string>

/* System clipboard helpers for file cut/copy/paste and drag & drop.
 *
 * The clipboard offers the selected file URIs under three formats, so the
 * *same* selection works everywhere:
 *  - text/plain (both "text/plain" and "text/plain;charset=utf-8"): the
 *    newline-joined local file paths, so drops into text editors and
 *    terminals insert the paths as text;
 *  - text/uri-list: what file managers understand;
 *  - application/x-gnome-copied-files: carries whether the operation is a
 *    cut ("cut\n") or a copy ("copy\n"), honored by Nautilus/Files.
 *
 * The in-app "internal clipboard" (QuiverFileOps) remains the source of truth
 * for paste between Quiver's own windows; this module is the bridge to and
 * from the rest of the desktop.  The gnome-copied-files marker can only set
 * the cut/copy hint in the *outgoing* direction - GDK always hands an
 * incoming read back as plain text, so a cut read back from an external app
 * is indistinguishable from a copy without their DnD action hint. */
namespace QuiverClipboard
{
	/* Register the text/plain, text/uri-list and
	 * application/x-gnome-copied-files serializers/deserializers.
	 * Idempotent; called lazily on first use. */
	void Init();

	/* Set the system clipboard to `uris` (a cut when bCut is TRUE). */
	void SetClipboard(const std::list<std::string>& uris, bool bCut,
	                  GdkClipboard *clipboard = NULL);

	/* Build a provider for the URIs suitable for the clipboard or a drag
	 * source.  With bTextOnly only the plain path-text payload is offered
	 * (for drops into text editors/terminals that would otherwise treat the
	 * drag as files to open).  Caller owns the reference (unref with
	 * g_object_unref). */
	GdkContentProvider * MakeContentProvider(const std::list<std::string>& uris,
	                                         bool bCut = false,
	                                         bool bTextOnly = false,
	                                         bool bIncludeUriList = false);

	/* Parse clipboard/drag payload text into file URIs.  Accepts the
	 * gnome-copied-files "copy\n"/"cut\n" header and bare URI lists (LF or
	 * CRLF).  Non-file lines are ignored; returns FALSE when no file URI was
	 * found.  cutOut is only meaningful when a header was present. */
	bool ParseClipboardText(const std::string& text,
	                        std::list<std::string>& urisOut, bool& cutOut);

	/* Best-effort read of the file URIs currently on the system clipboard.
	 * Returns TRUE when file URIs were found.  Runs its own nested main loop,
	 * so it must be called from the GUI thread. */
	bool GetClipboardUris(std::list<std::string>& urisOut, bool& cutOut,
	                      GdkClipboard *clipboard = NULL);
}

#endif