#ifndef QUIVER_EXIF_IO_H
#define QUIVER_EXIF_IO_H

#include <glib.h>
#include <gio/gio.h>

#include <exiv2/exiv2.hpp>

#include <string>

/*
 * Exiv2 reads metadata from a path, so a file on trash:/// - which has no path
 * for g_filename_from_uri(), g_file_get_path() or g_file_peek_path() to return
 * - used to be copied into a temp directory first, purely so Exiv2 had
 * something to open.
 *
 * GioBasicIo hands Exiv2 a read-only BasicIo backed by a GInputStream instead,
 * with seek emulation layered on top: metadata readers jump around the file
 * freely (an APP1 segment's offset is found by scanning forward, then read from
 * its start), and GInputStream only offers g_input_stream_skip() and no general
 * seek.
 *
 * A bounded sliding window turns those jumps into buffer hits, and a large read
 * that runs off the end of the window is streamed straight through to the
 * caller's buffer rather than buffered.  So peak memory tracks the window, not
 * the file: a 4 GB TIFF costs the same as a 4 kB one.  This matters because
 * videos never get here (QuiverFileImpl::LoadExifData() returns early for them,
 * and QuiverVideoOps uses the AVIO path instead) but a 300 MB multi-page scan
 * absolutely could.
 *
 * The write/mmap side of the BasicIo interface is stubbed out; it is only
 * reachable through write APIs Exiv2 does not call for a read-only image.
 */
#if EXIV2_TEST_VERSION(0, 28, 0)
using ExivBasicIoPtr = Exiv2::BasicIo::UniquePtr;
#else
using ExivBasicIoPtr = Exiv2::BasicIo::AutoPtr;
#endif

class GioBasicIo : public Exiv2::BasicIo
{
public:
	explicit GioBasicIo(GFile *file, std::string uri_for_path);
	~GioBasicIo() override;

	GioBasicIo(const GioBasicIo &) = delete;
	GioBasicIo &operator=(const GioBasicIo &) = delete;

	/* Opens @file for reading and returns a seek-capable BasicIo, or NULL if the
	 * content could not be read at all. */
	static ExivBasicIoPtr open(GFile *file, const std::string &uri_for_path);

	/* Adopts @stream, reading further from @file as needed.  @file is still
	 * required because a stream that cannot seek has to be reopened to go
	 * backwards; @stream is the first one, @file is what reopens come from.
	 * Exists so the non-seekable fallback can be exercised - a gvfs
	 * GFileInputStream is not always GSeekable. */
	static ExivBasicIoPtr open_stream(GFile *file, GInputStream *stream,
	                                  const std::string &uri_for_path);

	int open() override;
	int close() override;
#if EXIV2_TEST_VERSION(0, 28, 0)
	size_t write(const Exiv2::byte *data, size_t wcount) override;
	size_t write(Exiv2::BasicIo &src) override;
	int putb(Exiv2::byte data) override;
	Exiv2::DataBuf read(size_t rcount) override;
	size_t read(Exiv2::byte *buf, size_t rcount) override;
	int getb() override;
	void transfer(Exiv2::BasicIo &src) override;
	int seek(int64_t offset, Exiv2::BasicIo::Position pos) override;
	Exiv2::byte *mmap(bool isWriteable = false) override;
	int munmap() override;
	[[nodiscard]] size_t tell() const override;
	[[nodiscard]] size_t size() const override;
	[[nodiscard]] bool isopen() const override;
	[[nodiscard]] int error() const override;
	[[nodiscard]] bool eof() const override;
	[[nodiscard]] const std::string &path() const noexcept override;
	void populateFakeData() override;
#else
	long write(const Exiv2::byte *data, long wcount) override;
	long write(Exiv2::BasicIo &src) override;
	int putb(Exiv2::byte data) override;
	Exiv2::DataBuf read(long rcount) override;
	long read(Exiv2::byte *buf, long rcount) override;
	int getb() override;
	void transfer(Exiv2::BasicIo &src) override;
	int seek(long offset, Exiv2::BasicIo::Position pos) override;
	Exiv2::byte *mmap(bool isWriteable = false) override;
	int munmap() override;
	[[nodiscard]] long tell() const override;
	[[nodiscard]] size_t size() const override;
	[[nodiscard]] bool isopen() const override;
	[[nodiscard]] int error() const override;
	[[nodiscard]] bool eof() const override;
	[[nodiscard]] std::string path() const override;
	void populateFakeData() override;
#endif

	/* Peak allocation held by the sliding window.  This is the number that has
	 * to stay flat as the file grows, so it reports the buffer's capacity
	 * rather than how many of those bytes happen to be populated - a window
	 * holding 1 byte after a seek is not a small window, it is a big one. */
	[[nodiscard]] size_t buffered_bytes() const { return m_window.capacity(); }

private:
	/* Sliding window size.  Big enough that Exiv2's parser loop stays inside it,
	 * small enough to be irrelevant next to the images this handles. */
	static constexpr size_t kWindowSize = 256 * 1024;

	void release_stream();

	bool ensure_stream_at(goffset target);
	bool fill_window(goffset start);
	size_t read_windowed(Exiv2::byte *dest, size_t count);
	[[nodiscard]] bool window_covers(goffset start, size_t count) const;
	void note_size_from_pos(goffset pos);

	GFile *m_file;
	std::string m_path;
	GInputStream *m_stream = nullptr;

	/* Logical file position, i.e. what tell() reports. */
	goffset m_pos = 0;
	/* Where the underlying stream is parked, so reads can go straight through. */
	goffset m_stream_pos = 0;

	/* m_window holds the bytes starting at m_window_start. */
	std::string m_window;
	goffset m_window_start = 0;
	bool m_window_valid = false;

	/* Total length, resolved lazily and then cached; -1 until known. */
	mutable goffset m_size = -1;
	bool m_eof = false;
	bool m_open = false;
	int m_error = 0;
};

#endif /* QUIVER_EXIF_IO_H */