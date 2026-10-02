#ifndef QUIVER_AVIO_H
#define QUIVER_AVIO_H

/*
 * Opening a media container for libav* straight from a URI.
 *
 * avformat_open_input() wants a native filesystem path.  Quiver hands around
 * URIs instead, and for "trash:///" there is no path at all - not through
 * g_filename_from_uri(), and not through g_file_get_path() or
 * g_file_peek_path() either, because gvfs' trash backend is a virtual
 * namespace rather than a directory.  (A FUSE mount is a different matter: it
 * is an ordinary local path, GIO resolves it natively, and these functions take
 * the cheap route with no extra work.)
 *
 * So when a URI has no path we read it as a GInputStream and drive libavformat
 * through a custom AVIOContext.  Nothing is copied to disk at all: staging the
 * file into a temp directory used to duplicate every trashed video into /tmp,
 * leak that copy whenever the process died before shutdown cleanup, and -
 * because g_file_copy() drives gvfs' D-Bus copy machinery - fault inside
 * libgvfs on the thumbnail worker thread.
 */

#include <glib.h>

extern "C" {
#include <libavformat/avformat.h>
}

#include <string>

namespace QuiverAvio
{
	/* Read-only container handle.  Frees the AVFormatContext and, when the URI
	 * had no local path, the GInputStream and custom AVIOContext with it.
	 * Close() rather than destroying, because ordering matters: once
	 * AVFMT_FLAG_CUSTOM_IO is set, avformat_close_input() is what releases the
	 * AVIOContext. */
	/* Matches QuiverVideoOps::VideoAbortFn so callers can hand their own
	 * cancellation straight through. */
	typedef gboolean (*AbortFn)(gpointer data);

	class Container
	{
	public:
		/* Opaque; defined in QuiverAvio.cpp.  Public only so the file-local
		 * AVIO callbacks can name it - nothing outside sees its contents. */
		struct StreamImpl;
		/* Definition lives below with the other public nested types; the
		 * interrupt callback in QuiverAvio.cpp has to name it. */
		struct Interrupt
		{
			gint64 deadline_us = 0;
			AbortFn abort_fn = nullptr;
			gpointer abort_data = nullptr;
		};

		Container() = default;
		~Container() { Close(); }

		Container(const Container&) = delete;
		Container& operator=(const Container&) = delete;
		Container(Container&& other) noexcept { *this = std::move(other); }
		Container& operator=(Container&& other) noexcept;

		/* Opens the container and runs avformat_find_stream_info() on it.
		 * Returns false on failure, leaving *fmt() NULL.
		 *
		 * The returned context is already probed.  Do NOT call
		 * avformat_find_stream_info() on it yourself: the second pass walks
		 * per-stream parse buffers the first one freed, and corrupts the heap -
		 * it shows up as a crash inside libavformat on some files and as
		 * garbage values in unrelated fields on others.  Everything Open()
		 * found is readable straight off fmt(). */
		bool Open(const gchar* uri);

		/* Aborts in-flight blocking calls - the interrupt callback returns
		 * non-zero and libavformat gives up.  Used to cancel thumbnail work.
		 * Must be set before Open(). */
		void SetDeadlineUs(gint64 deadline_us);
		void SetAbort(AbortFn fn, gpointer data) { m_abortFn = fn; m_abortData = data; }

		/* Run avformat_find_stream_info() during Open().  Turn it off for
		 * header-only callers that just want container-level tags. */
		void SetProbeStreams(bool probe) { m_probeStreams = probe; }

		/* Ignore any local path and read from a GInputStream even for file://
		 * URIs.  Test seam only: it is how the streaming path gets covered on a
		 * machine with no trash:/// to hand. */
		void ForceStreaming(bool force) { m_forceStream = force; }

		AVFormatContext* fmt() const { return m_fmt; }
		bool ok() const { return m_fmt != nullptr; }

		/* Local path when the URI had one, otherwise empty.  Empty is the
		 * signal that the bytes are being streamed. */
		const std::string& native_path() const { return m_nativePath; }
		bool streaming() const { return m_nativePath.empty(); }

		void Close();

	private:
		void CloseStream();

		AVFormatContext* m_fmt = nullptr;
		std::string m_nativePath;
		StreamImpl* m_stream = nullptr;
		gint64 m_deadlineUs = 0;
		AbortFn m_abortFn = nullptr;
		gpointer m_abortData = nullptr;
		bool m_forceStream = false;
		bool m_probeStreams = true;
		Interrupt m_interrupt;
	};
}

#endif /* QUIVER_AVIO_H */