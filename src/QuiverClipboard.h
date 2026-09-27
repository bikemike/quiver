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

	/* The contents of a drop that a GtkDropTarget has preloaded, judged
	 * against the folder the drag is currently over. */
	class DropContents
	{
	public:
		/* TRUE when the drop carries files and the shared policy allows
		 * them on `target_uri`; the drag is then described by `uris` (a
		 * move when `cut` is set) and the caller may offer the target.
		 *
		 * FALSE means the hover must not be shown as a target, and covers
		 * two cases a caller must not tell apart: the contents are not
		 * known yet, because GTK is still preloading them, or they are
		 * known and the drop is refused - a file over the folder it
		 * already lives in, a folder over itself or its own subtree, or a
		 * drag that carries no files at all. */
		bool Accepts(GtkDropTarget* target, const std::string& target_uri,
			std::list<std::string>& uris, bool& cut);

		/* Forget the cached contents, so the next drag is read again. */
		void Reset();

	private:
		/* the drop `m_Uris` was parsed from; NULL while nothing is cached,
		 * which also covers "the contents of the current drop are unknown" */
		GdkDrop*               m_pDrop;
		std::list<std::string> m_Uris;
		bool                   m_bCut;
	};

	/* Best-effort read of the file URIs currently on the system clipboard.
	 * Returns TRUE when file URIs were found.  Runs its own nested main loop,
	 * so it must be called from the GUI thread. */
	bool GetClipboardUris(std::list<std::string>& urisOut, bool& cutOut,
	                      GdkClipboard *clipboard = NULL);
}

#endif
