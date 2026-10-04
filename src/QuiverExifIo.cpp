#include "QuiverExifIo.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>

GioBasicIo::GioBasicIo(GFile *file, std::string uri_for_path)
	: m_file(file), m_path(std::move(uri_for_path))
{
	/* Open the stream here rather than only in the factory: the constructor is
	 * public, so anything building one directly would otherwise get an object
	 * whose every read failed at EOF. */
	if (m_file != nullptr)
	{
		GError *error = nullptr;
		m_stream = G_INPUT_STREAM(g_file_read(m_file, nullptr, &error));
		if (nullptr != m_stream)
			m_open = true;
		else
			g_clear_error(&error);
	}
}

GioBasicIo::~GioBasicIo()
{
	/* release_stream() rather than close(): this is a final object, so a
	 * virtual call here would not dispatch. */
	release_stream();
	if (m_file != nullptr)
	{
		g_object_unref(m_file);
		m_file = nullptr;
	}
}

ExivBasicIoPtr GioBasicIo::open(GFile *file, const std::string &uri_for_path)
{
	if (nullptr == file)
#if EXIV2_TEST_VERSION(0, 28, 0)
		return nullptr;
#else
		return ExivBasicIoPtr();
#endif

	/* The constructor opens the stream; opening it again here would leak the
	 * first one. */
	std::unique_ptr<GioBasicIo> io(new GioBasicIo(g_object_ref(file), uri_for_path));
	if (nullptr == io->m_stream)
#if EXIV2_TEST_VERSION(0, 28, 0)
		return nullptr;
#else
		return ExivBasicIoPtr();
#endif

	io->m_open = true;

	/* Learn the total length up front where that is cheap, because seek() from
	 * the end is how Exiv2 sizes some formats.  GFileInfo is not queried on
	 * purpose: g_file_query_info() drives gvfs' D-Bus machinery, which is the
	 * same thing that faulted inside libgvfs on a worker thread before.  Reading
	 * to the end is O(file) in time but O(1) in memory, and it is only paid when
	 * size() is actually asked for. */
#if EXIV2_TEST_VERSION(0, 28, 0)
	return io;
#else
	return ExivBasicIoPtr(io.release());
#endif
}

ExivBasicIoPtr GioBasicIo::open_stream(GFile *file, GInputStream *stream,
                                      const std::string &uri_for_path)
{
	if (nullptr == file || nullptr == stream)
#if EXIV2_TEST_VERSION(0, 28, 0)
		return nullptr;
#else
		return ExivBasicIoPtr();
#endif

	std::unique_ptr<GioBasicIo> io(new GioBasicIo(g_object_ref(file), uri_for_path));
	io->m_stream = G_INPUT_STREAM(g_object_ref(stream));
	io->m_open = true;
#if EXIV2_TEST_VERSION(0, 28, 0)
	return io;
#else
	return ExivBasicIoPtr(io.release());
#endif
}

void GioBasicIo::release_stream()
{
	if (m_stream != nullptr)
	{
		g_input_stream_close(m_stream, nullptr, nullptr);
		g_object_unref(m_stream);
		m_stream = nullptr;
	}
}

void GioBasicIo::note_size_from_pos(goffset pos)
{
	if (m_eof && (m_size < 0 || pos > m_size))
		m_size = pos;
}

bool GioBasicIo::window_covers(goffset start, size_t count) const
{
	if (!m_window_valid)
		return false;
	const goffset end = start + static_cast<goffset>(count);
	return start >= m_window_start && end <= m_window_start + static_cast<goffset>(m_window.size());
}

/* Repositions the stream to @target, seeking when the backend can and skipping
 * (or reopening and skipping) when it cannot. */
bool GioBasicIo::ensure_stream_at(goffset target)
{
	if (m_stream == nullptr)
		return false;
	if (m_stream_pos == target)
		return true;

	if (m_size >= 0 && target > m_size)
	{
		/* Past the end: park at the end so reads report EOF rather than
		 * spinning. */
		target = m_size;
	}

	/* GSeekable covers local files; a gvfs-backed GFileInputStream implements it
	 * too for seekable streams. */
	if (G_IS_SEEKABLE(m_stream))
	{
		GError *error = nullptr;
		if (g_seekable_seek(G_SEEKABLE(m_stream), target, G_SEEK_SET, nullptr, &error))
		{
			m_stream_pos = target;
			return true;
		}
		g_clear_error(&error);
	}

	if (target > m_stream_pos)
	{
		goffset remaining = target - m_stream_pos;
		while (remaining > 0)
		{
			const gsize chunk = static_cast<gsize>(remaining > (1 << 20) ? (1 << 20) : remaining);
			GError *error = nullptr;
			const gssize skipped = g_input_stream_skip(m_stream, chunk, nullptr, &error);
			if (skipped < 0)
			{
				/* This stream cannot skip - gvfs does not always offer it.  Only
				 * reopening gives us a way forward, so stop trying. */
				g_clear_error(&error);
				break;
			}
			m_stream_pos += skipped;
			remaining -= skipped;
		}
		if (remaining == 0)
			return true;
	}

	/* Backwards, or skip was refused: reopen from the start and skip forward. */
	if (m_file == nullptr)
		return false;

	GError *error = nullptr;
	GInputStream *fresh = G_INPUT_STREAM(g_file_read(m_file, nullptr, &error));
	if (nullptr == fresh)
	{
		g_clear_error(&error);
		return false;
	}
	release_stream();
	m_stream = fresh;
	m_stream_pos = 0;
	m_eof = false;
	m_window_valid = false;
	return target == 0 ? true : ensure_stream_at(target);
}

bool GioBasicIo::fill_window(goffset start)
{
	if (!ensure_stream_at(start))
		return false;

	m_window.clear();
	m_window_start = start;
	m_window.resize(kWindowSize);
	m_window.shrink_to_fit();

	size_t filled = 0;
	while (filled < kWindowSize)
	{
		GError *error = nullptr;
		const gssize got = g_input_stream_read(
			m_stream, m_window.data() + filled, kWindowSize - filled, nullptr, &error);
		if (got < 0)
		{
			g_clear_error(&error);
			m_window_valid = false;
			return false;
		}
		if (got == 0)
		{
			m_eof = true;
			note_size_from_pos(m_stream_pos);
			break;
		}
		filled += static_cast<size_t>(got);
		m_stream_pos += got;
	}

	m_window.resize(filled);
	m_window_valid = true;
	return true;
}

/* Serves @count bytes at m_pos, using the window where it can and streaming
 * straight into @dest where it cannot, so a huge read never forces a huge
 * allocation. */
size_t GioBasicIo::read_windowed(Exiv2::byte *dest, size_t count)
{
	size_t done = 0;
	while (done < count)
	{
		if (window_covers(m_pos, 1) &&
		    static_cast<goffset>(m_window.size()) >= m_pos - m_window_start + 1)
		{
			const size_t off = static_cast<size_t>(m_pos - m_window_start);
			const size_t n = std::min(count - done, m_window.size() - off);
			std::memcpy(dest + done, m_window.data() + off, n);
			done += n;
			m_pos += static_cast<goffset>(n);
			continue;
		}

		/* Window miss.  If the rest is small enough, refill the window and let
		 * the loop above serve it; otherwise stream the remainder directly. */
		const goffset remaining = static_cast<goffset>(count - done);
		if (remaining <= static_cast<goffset>(kWindowSize))
		{
			const goffset before = m_pos;
			const bool filled = fill_window(m_pos);
			/* A fill that made no progress means EOF: without this the loop
			 * would refill the same empty window forever. */
			if (!filled || (m_pos == before && m_window.empty()))
				break;
			continue;
		}

		if (!ensure_stream_at(m_pos))
			break;
		GError *error = nullptr;
		const gssize got = g_input_stream_read(m_stream, dest + done, count - done, nullptr, &error);
		if (got < 0)
		{
			g_clear_error(&error);
			m_error = 1;
			break;
		}
		if (got == 0)
		{
			m_eof = true;
			note_size_from_pos(m_stream_pos);
			break;
		}
		done += static_cast<size_t>(got);
		m_pos += got;
		m_stream_pos += got;
		m_window_valid = false;   /* stream moved out from under the window */
	}

	/* Only a genuine short read from the end of the file is EOF.  Latching it
	 * whenever done < count made eof() true after a partially-served read, and
	 * Exiv2's format sniffer - which checks eof() between probes - then gave up
	 * on a perfectly valid file and refused to identify it. */
	if (done < count)
		m_eof = true;
	return done;
}

int GioBasicIo::open()
{
	/* Exiv2::ImageFactory::open() opens the io more than once while sniffing the
	 * format, and each open must genuinely rewind to byte 0.  Just resetting the
	 * counters is not enough: the underlying GInputStream keeps its own offset,
	 * so a stream left sitting past the first probe would report EOF immediately
	 * and the second probe would see nothing.  Rewind the real stream, and fall
	 * back to reopening it when the backend cannot seek. */
	m_open = true;
	m_pos = 0;
	m_eof = false;
	m_error = 0;
	m_window_valid = false;
	m_stream_pos = 0;

	if (m_stream != nullptr && G_IS_SEEKABLE(m_stream))
	{
		GError *error = nullptr;
		if (g_seekable_seek(G_SEEKABLE(m_stream), 0, G_SEEK_SET, nullptr, &error))
			return 0;
		g_clear_error(&error);
	}
	if (m_stream != nullptr)
	{
		/* Cannot seek: drop it and take a fresh stream from the file. */
		release_stream();
		if (m_file != nullptr)
		{
			GError *error = nullptr;
			m_stream = G_INPUT_STREAM(g_file_read(m_file, nullptr, &error));
			if (nullptr == m_stream)
			{
				g_clear_error(&error);
				m_open = false;
				return -1;
			}
		}
	}
	return 0;
}
int GioBasicIo::close()
{
	/* Not a real close: the stream is kept so that a later open() - Exiv2 does
	 * open/close/open while working through a file - can still read.  Tearing the
	 * stream down here left the next open() with nothing to reopen from, and the
	 * metadata read failed outright. */
	m_open = false;
	m_pos = 0;
	m_eof = false;
	m_error = 0;
	m_window_valid = false;
	m_stream_pos = 0;
	return 0;
}

#if EXIV2_TEST_VERSION(0, 28, 0)
size_t GioBasicIo::write(const Exiv2::byte *, size_t)
{
	/* Read-only: this exists to let Exiv2 parse a file it cannot open by path,
	 * never to modify one.  Writing would mean saving metadata back to a URI
	 * Exiv2 could not have opened anyway. */
	m_error = 1;
	return 0;
}

size_t GioBasicIo::write(Exiv2::BasicIo &)
{
	m_error = 1;
	return 0;
}
#else
long GioBasicIo::write(const Exiv2::byte *, long)
{
	/* Read-only: this exists to let Exiv2 parse a file it cannot open by path,
	 * never to modify one.  Writing would mean saving metadata back to a URI
	 * Exiv2 could not have opened anyway. */
	m_error = 1;
	return 0;
}

long GioBasicIo::write(Exiv2::BasicIo &)
{
	m_error = 1;
	return 0;
}
#endif

int GioBasicIo::putb(Exiv2::byte)
{
	m_error = 1;
	return 0;
}

#if EXIV2_TEST_VERSION(0, 28, 0)
Exiv2::DataBuf GioBasicIo::read(size_t rcount)
{
	Exiv2::DataBuf buf(rcount);
	const size_t n = read_windowed(buf.data(), rcount);
	if (n < rcount)
		buf.resize(n);   /* DataBuf tracks length via std::vector::size() */
	return buf;
}

size_t GioBasicIo::read(Exiv2::byte *buf, size_t rcount)
{
	return read_windowed(buf, rcount);
}
#else
Exiv2::DataBuf GioBasicIo::read(long rcount)
{
	Exiv2::DataBuf buf(rcount);
	const size_t n = read_windowed(buf.pData_, static_cast<size_t>(rcount));
	if (static_cast<long>(n) < rcount)
		return Exiv2::DataBuf(buf.pData_, static_cast<long>(n));
	return buf;
}

long GioBasicIo::read(Exiv2::byte *buf, long rcount)
{
	return static_cast<long>(read_windowed(buf, static_cast<size_t>(rcount)));
}
#endif

int GioBasicIo::getb()
{
	if (m_pos >= 0 && m_size >= 0 && m_pos >= m_size)
	{
		m_eof = true;
		return EOF;
	}
	Exiv2::byte b = 0;
	if (read_windowed(&b, 1) != 1)
	{
		m_eof = true;
		return EOF;
	}
	return b;
}

void GioBasicIo::transfer(Exiv2::BasicIo &)
{
	throw Exiv2::Error(Exiv2::ErrorCode::kerGeneralError,
	                   "GioBasicIo is read-only");
}

#if EXIV2_TEST_VERSION(0, 28, 0)
int GioBasicIo::seek(int64_t offset, Exiv2::BasicIo::Position pos)
#else
int GioBasicIo::seek(long offset, Exiv2::BasicIo::Position pos)
#endif
{
	goffset base = 0;
	switch (pos)
	{
	case Exiv2::BasicIo::beg: base = 0; break;
	case Exiv2::BasicIo::cur: base = m_pos; break;
	case Exiv2::BasicIo::end:
		if (size() == 0 && m_size == 0)
			base = 0;
		else
			base = m_size >= 0 ? m_size : static_cast<goffset>(size());
		break;
	default:
		return -1;
	}

	const int64_t target = base + offset;
	if (target < 0)
		return -1;

	m_pos = static_cast<goffset>(target);
	/* Seeking past the end is legal, but landing exactly on the end must not
	 * latch eof: Exiv2 seeks back from the end and then reads, and a latched
	 * eof made every one of those reads return nothing. */
	m_eof = false;
	return 0;
}

Exiv2::byte *GioBasicIo::mmap(bool)
{
	/* Deliberately NULL: mmap() must be able to hand back the whole file, and
	 * this io holds only a window of it.  Exiv2 falls back to read() when this
	 * returns NULL, which is exactly the path already covered by the tests. */
	return nullptr;
}

int GioBasicIo::munmap()
{
	return 0;
}

#if EXIV2_TEST_VERSION(0, 28, 0)
size_t GioBasicIo::tell() const
{
	return static_cast<size_t>(m_pos);
}
#else
long GioBasicIo::tell() const
{
	return static_cast<long>(m_pos);
}
#endif

size_t GioBasicIo::size() const
{
	if (m_size >= 0)
		return static_cast<size_t>(m_size);

	/* Count the stream to the end, discarding the bytes.  O(file) in time,
	 * O(1) in memory, and it leaves the read position untouched so a size()
	 * call in the middle of a parse does not disturb the reader. */
	if (m_stream == nullptr || m_file == nullptr)
		return 0;

	GError *error = nullptr;
	GInputStream *probe = G_INPUT_STREAM(g_file_read(m_file, nullptr, &error));
	if (nullptr == probe)
	{
		g_clear_error(&error);
		return 0;
	}

	goffset total = 0;
	char sink[64 * 1024];
	for (;;)
	{
		const gssize got = g_input_stream_read(probe, sink, sizeof(sink), nullptr, &error);
		if (got < 0)
		{
			g_clear_error(&error);
			break;
		}
		if (got == 0)
			break;
		total += got;
	}
	g_input_stream_close(probe, nullptr, nullptr);
	g_object_unref(probe);

	m_size = total;
	return static_cast<size_t>(m_size);
}

bool GioBasicIo::isopen() const
{
	return m_open;
}

int GioBasicIo::error() const
{
	return m_error;
}

bool GioBasicIo::eof() const
{
	/* Report EOF from the position and the known length, not from a sticky flag
	 * left behind by the window fill.  A file smaller than the window fills
	 * short and trips that flag even though plenty of buffered bytes are still
	 * to be served; Exiv2's format sniffer consults eof() between probe reads,
	 * so a premature true made it reject valid files outright. */
	if (m_pos >= 0 && m_size >= 0 && m_pos >= m_size)
		return true;
	return m_eof && !window_covers(m_pos, 1);
}

#if EXIV2_TEST_VERSION(0, 28, 0)
const std::string &GioBasicIo::path() const noexcept
{
	return m_path;
}
#else
std::string GioBasicIo::path() const
{
	return m_path;
}
#endif

void GioBasicIo::populateFakeData()
{
	/* Image data is never parsed for metadata, so there is nothing to fake. */
}