#include "QuiverAvio.h"

#include <gio/gio.h>

#include <cerrno>
#include <cstdio>
#include <utility>

namespace QuiverAvio
{

/* PIMPL: keeps the ffmpeg AVIOContext and the GIO stream out of the header, so
 * callers do not need either. */
struct Container::StreamImpl
{
	GFile* file = nullptr;
	GInputStream* stream = nullptr;
	AVIOContext* avio = nullptr;
	/* av_malloc(), not new/std::vector: with AVFMT_FLAG_CUSTOM_IO set,
	 * avformat_close_input() calls avio_context_free(), which releases the
	 * buffer with av_free().  A std::vector's storage would be released by the
	 * wrong allocator. */
	uint8_t* buffer = nullptr;
	int buffer_size = 0;
};

namespace
{
	constexpr int kBufferSize = 65536;

	int read_cb(void* opaque, uint8_t* buf, int buf_size)
	{
		auto* impl = static_cast<Container::StreamImpl*>(opaque);
		if (impl->stream == nullptr) return AVERROR(EIO);

		/* A short read is not an error: hand back what arrived and report EOF
		 * only once the stream really is drained, or libavformat reads a
		 * partial buffer as a truncated file. */
		GError* error = nullptr;
		gssize n = g_input_stream_read(impl->stream, buf, (gsize)buf_size, nullptr, &error);
		if (n < 0)
		{
			g_clear_error(&error);
			return AVERROR(EIO);
		}
		return (int)n;
	}

	int64_t seek_cb(void* opaque, int64_t offset, int whence)
	{
		auto* impl = static_cast<Container::StreamImpl*>(opaque);
		if (impl->stream == nullptr) return AVERROR(EIO);
		if (!G_IS_SEEKABLE(impl->stream)) return AVERROR(ENOSYS);

		/* AVSEEK_SIZE has to answer with the total length, not the current
		 * offset - returning the offset makes libavformat conclude the file is
		 * a few bytes long and then report no video stream at all. */
		if (whence == AVSEEK_SIZE)
		{
			GSeekable* seekable = G_SEEKABLE(impl->stream);
			gint64 saved = g_seekable_tell(seekable);
			gint64 length = -1;
			GError* seek_error = nullptr;
			if (g_seekable_seek(seekable, 0, G_SEEK_END, nullptr, &seek_error))
				length = g_seekable_tell(seekable);
			g_clear_error(&seek_error);
			g_seekable_seek(seekable, saved, G_SEEK_SET, nullptr, nullptr);
			return length;
		}

		GSeekType type;
		switch (whence)
		{
		case SEEK_SET: type = G_SEEK_SET; break;
		case SEEK_CUR: type = G_SEEK_CUR; break;
		case SEEK_END: type = G_SEEK_END; break;
		default: return AVERROR(EINVAL);
		}

		GError* seek_error = nullptr;
		if (!g_seekable_seek(G_SEEKABLE(impl->stream), offset, type, nullptr, &seek_error))
		{
			g_clear_error(&seek_error);
			return AVERROR(EIO);
		}
		return g_seekable_tell(G_SEEKABLE(impl->stream));
	}

	int interrupt_cb(void* opaque)
	{
		auto* ctx = static_cast<Container::Interrupt*>(opaque);
		if (ctx == nullptr) return 0;
		if (ctx->abort_fn != nullptr && ctx->abort_fn(ctx->abort_data)) return 1;
		if (ctx->deadline_us > 0 && g_get_monotonic_time() > ctx->deadline_us) return 1;
		return 0;
	}
}

Container& Container::operator=(Container&& other) noexcept
{
	if (this == &other) return *this;
	Close();
	m_fmt = other.m_fmt;
	m_nativePath = std::move(other.m_nativePath);
	m_stream = other.m_stream;
	m_deadlineUs = other.m_deadlineUs;
	other.m_fmt = nullptr;
	other.m_stream = nullptr;
	return *this;
}

void Container::CloseStream()
{
	if (m_stream == nullptr) return;

	/* If the AVIOContext never reached a format context it is ours to free;
	 * avio_context_free() releases m_stream->buffer along with it.  Once
	 * AVFMT_FLAG_CUSTOM_IO is set, avformat owns it instead, and the format
	 * context's teardown has already released it - which is why Close() clears
	 * this pointer before calling CloseStream(). */
	if (m_stream->avio != nullptr)
	{
		avio_context_free(&m_stream->avio);
		m_stream->buffer = nullptr;
		m_stream->buffer_size = 0;
	}
	if (m_stream->stream != nullptr)
	{
		g_input_stream_close(m_stream->stream, nullptr, nullptr);
		g_object_unref(m_stream->stream);
		m_stream->stream = nullptr;
	}
	if (m_stream->file != nullptr)
	{
		g_object_unref(m_stream->file);
		m_stream->file = nullptr;
	}
	delete m_stream;
	m_stream = nullptr;
}

bool Container::Open(const gchar* uri)
{
	Close();

	if (uri == nullptr) return false;

	/* Fast route: anything GIO can hand back as a real path, which is every
	 * file:// URI regardless of the filesystem underneath (including FUSE). */
	if (!m_forceStream)
	{
		GFile* local = g_file_new_for_uri(uri);
		const gchar* path = g_file_peek_path(local);
		if (path != nullptr)
			m_nativePath.assign(path);
		g_object_unref(local);
	}

	m_fmt = avformat_alloc_context();
	if (m_fmt == nullptr) return false;

	if (m_deadlineUs > 0 || m_abortFn != nullptr)
	{
		/* Owned by the Container; outlives the open call, and is only read
		 * from libavformat's own threads. */
		m_interrupt.deadline_us = m_deadlineUs;
		m_interrupt.abort_fn = m_abortFn;
		m_interrupt.abort_data = m_abortData;
		m_fmt->interrupt_callback.callback = interrupt_cb;
		m_fmt->interrupt_callback.opaque = &m_interrupt;
	}

	AVDictionary* opts = nullptr;
	av_dict_set(&opts, "probesize", "1000000", 0); // 1 MB
	av_dict_set(&opts, "analyzeduration", "500000", 0); // 500 ms

	int rc;
	if (!m_nativePath.empty())
	{
		rc = avformat_open_input(&m_fmt, m_nativePath.c_str(), nullptr, &opts);
	}
	else
	{
		m_stream = new StreamImpl();
		m_stream->file = g_file_new_for_uri(uri);
		GError* read_error = nullptr;
		m_stream->stream = G_INPUT_STREAM(g_file_read(m_stream->file, nullptr, &read_error));
		if (m_stream->stream == nullptr)
		{
			g_clear_error(&read_error);
			av_dict_free(&opts);
			CloseStream();
			m_fmt = nullptr;
			return false;
		}

		m_stream->buffer = static_cast<uint8_t*>(av_malloc(kBufferSize));
		if (m_stream->buffer == nullptr)
		{
			av_dict_free(&opts);
			CloseStream();
			m_fmt = nullptr;
			return false;
		}
		m_stream->buffer_size = kBufferSize;
		m_stream->avio = avio_alloc_context(m_stream->buffer, m_stream->buffer_size,
		                                    0, m_stream, read_cb, nullptr, seek_cb);
		if (m_stream->avio == nullptr)
		{
			av_dict_free(&opts);
			CloseStream();
			m_fmt = nullptr;
			return false;
		}
		/* A stream that cannot seek still has to be told the truth, or ffmpeg
		 * assumes full random access and lands seeks in the wrong place. */
		if (!G_IS_SEEKABLE(m_stream->stream))
			m_stream->avio->seek = nullptr;

		m_fmt->pb = m_stream->avio;
		m_fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
		rc = avformat_open_input(&m_fmt, nullptr, nullptr, &opts);
	}

	av_dict_free(&opts);

	if (rc != 0)
	{
		/* avformat_open_input() freed the format context, and with it any
		 * custom AVIOContext.  Clear our pointer so CloseStream() does not
		 * free it a second time; the stream and GFile are still ours. */
		if (m_stream != nullptr) m_stream->avio = nullptr;
		CloseStream();
		m_fmt = nullptr;
		return false;
	}

	if (m_probeStreams)
	{
		/* Smaller probe units keep thumbnailing cheap; must be set before the
		 * probe, not after. */
		m_fmt->fps_probe_size = 0;
		avformat_find_stream_info(m_fmt, nullptr);
	}
	return true;
}

void Container::SetDeadlineUs(gint64 deadline_us)
{
	m_deadlineUs = deadline_us;
}

void Container::Close()
{
	if (m_fmt != nullptr)
	{
		/* Releases the custom AVIOContext and its buffer, if any. */
		avformat_close_input(&m_fmt);
	}
	m_fmt = nullptr;
	if (m_stream != nullptr)
	{
		m_stream->avio = nullptr;
		CloseStream();
	}
	m_nativePath.clear();
}

}