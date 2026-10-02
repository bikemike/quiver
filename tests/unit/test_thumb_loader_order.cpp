#include <catch2/catch_test_macros.hpp>
#include "IconViewThumbLoader.h"
#include "QuiverFile.h"
#include "test_helpers.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <dirent.h>

#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <sys/stat.h>

#include <set>
#include <vector>

/* Load order for the icon view's thumbnail queue.
 *
 * Order matters because Run() consumes the queue FIFO, so a cell's position is
 * when a worker thread gets to it.  This is checked directly rather than by
 * driving a scroll: the queue used to have a reversed copy of itself appended,
 * which queued every item twice and - because the copy was last - left the
 * cells furthest from the viewport with the newest cache timestamps.
 */

static bool Contains(const std::vector<gulong>& v, gulong x)
{
	for (std::vector<gulong>::const_iterator i = v.begin(); i != v.end(); ++i)
		if (*i == x)
			return true;
	return false;
}

TEST_CASE("Visible cells are queued before anything off screen", "[unit][cache]")
{
	// 595 items, a 40-cell viewport near the top.
	const guint n = 595;
	const gulong vs = 0, ve = 40;

	std::vector<gulong> order =
		IconViewThumbLoader::BuildLoadOrder(vs, ve, n, 520, +1);

	REQUIRE(order.size() == 520);
	// The whole viewport leads, in list order.
	for (gulong i = vs; i <= ve; ++i)
		CHECK(order[i - vs] == i);
	// Nothing below the fold is queued before the viewport is finished.
	for (std::size_t i = ve + 1; i < order.size(); ++i)
		CHECK(order[i] > ve);
}

TEST_CASE("Scrolling up queues the cells about to arrive, nearest first", "[unit][cache]")
{
	// Near the bottom of a 595-item list, then scrolled up: the cells about to
	// arrive are the ones just above the viewport, so they must be queued before
	// the cells already left behind at the top.
	const guint n = 595;
	const gulong vs = 500, ve = 540;

	std::vector<gulong> order =
		IconViewThumbLoader::BuildLoadOrder(vs, ve, n, 200, -1);

	REQUIRE(order.size() == 200);

	// Travelling up, the list fills bottom-to-top: the bottom row of the
	// viewport leads, because it is the part nearest to where the view just
	// came from, and each row above it follows.
	for (gulong i = 0; i <= ve - vs; ++i)
		CHECK(order[i] == ve - i);

	// Then the nearest cell above the viewport, then the next one up.
	CHECK(order[41] == vs - 1);
	CHECK(order[42] == vs - 2);
	CHECK(order[43] == vs - 3);

	// Travelling up is preferred, so nothing below the viewport is queued while
	// cells just above it are still waiting.
	for (std::size_t i = 41; i < order.size(); ++i)
		CHECK(order[i] < vs);
}

TEST_CASE("Scrolling down queues the cells about to arrive", "[unit][cache]")
{
	const guint n = 595;
	const gulong vs = 100, ve = 140;

	std::vector<gulong> order =
		IconViewThumbLoader::BuildLoadOrder(vs, ve, n, 200, +1);

	REQUIRE(order.size() == 200);
	// Travelling down, the mirror of the case above: top-to-bottom.
	for (gulong i = vs; i <= ve; ++i)
		CHECK(order[i - vs] == i);
	CHECK(order[41] == ve + 1);
	CHECK(order[42] == ve + 2);

	// Travelling down is preferred, so nothing above the viewport is queued
	// while cells just below it are still waiting.
	for (std::size_t i = 41; i < order.size(); ++i)
		CHECK(order[i] > ve);
}

TEST_CASE("An unknown direction queues both sides nearest-first", "[unit][cache]")
{
	const guint n = 200;
	const gulong vs = 90, ve = 100;

	std::vector<gulong> order =
		IconViewThumbLoader::BuildLoadOrder(vs, ve, n, 30, 0);

	REQUIRE(order.size() == 30);
	for (gulong i = vs; i <= ve; ++i)
		CHECK(order[i - vs] == i);
	// No direction is known, so it settles on downwards - the same side the
	// first paint of a folder moves towards - and drains that side first.
	CHECK(order[11] == ve + 1);
	CHECK(order[12] == ve + 2);
	// The list is long enough below that the leading side never runs out, so
	// nothing above the viewport is queued yet.
	CHECK(!Contains(order, vs - 1));
}

TEST_CASE("The load queue holds no duplicates and stays in range", "[unit][cache]")
{
	const guint n = 595;
	for (int dir = -1; dir <= 1; ++dir)
	{
		for (gulong vs = 0; vs < n; vs += 37)
		{
			for (gulong ve = vs; ve < vs + 60; ve += 13)
			{
				for (guint cap = 1; cap <= 300; cap += 47)
				{
					std::vector<gulong> order =
						IconViewThumbLoader::BuildLoadOrder(vs, ve, n, cap, dir);

					CHECK(order.size() <= cap);
					std::set<gulong> seen;
					for (std::size_t i = 0; i < order.size(); ++i)
					{
						CHECK(order[i] < n);          // never past the end
						CHECK(seen.insert(order[i]).second);  // queued once only
					}
				}
			}
		}
	}
}

TEST_CASE("A queue smaller than the viewport still covers what is on screen", "[unit][cache]")
{
	const guint n = 100;
	std::vector<gulong> order =
		IconViewThumbLoader::BuildLoadOrder(10, 20, n, 5, +1);

	REQUIRE(order.size() == 5);
	// Travelling down, the visible cells are queued from the edge the view came
	// from, so with room for only five it is these five.  The cells about to
	// scroll into view are dropped rather than the ones already on screen.
	for (gulong i = 10; i <= 14; ++i)
		CHECK(Contains(order, i));
	CHECK(!Contains(order, 15));
}

TEST_CASE("The load order survives an unsettled viewport and an empty list", "[unit][cache]")
{
	// The visible range can briefly run past the end of the list.
	std::vector<gulong> order =
		IconViewThumbLoader::BuildLoadOrder(90, 140, 100, 200, +1);
	CHECK(order.size() == 100);
	for (std::size_t i = 0; i < order.size(); ++i)
		CHECK(order[i] < 100);

	// Empty list.
	CHECK(IconViewThumbLoader::BuildLoadOrder(0, 0, 0, 100, +1).empty());
	// A single visible cell.
	CHECK(IconViewThumbLoader::BuildLoadOrder(0, 0, 100, 100, +1).size() == 1);
	// Zero capacity means no work at all.
	CHECK(IconViewThumbLoader::BuildLoadOrder(0, 40, 100, 0, +1).empty());
	// Viewport at the very top and very bottom.
	CHECK(Contains(IconViewThumbLoader::BuildLoadOrder(0, 10, 50, 50, -1), 0));
	CHECK(Contains(IconViewThumbLoader::BuildLoadOrder(40, 49, 50, 50, +1), 49));
}

/* Cache sizing.
 *
 * The size used to be visible_cells * 13, which is a multiple of a page count
 * rather than anything to do with memory.  In a 595-item folder at 128px it
 * produced a 520-entry cache, so the 75 items off the top were evicted while
 * still on screen and re-decoded on every repaint.
 */

TEST_CASE("The cache is sized to hold the whole folder when it fits", "[unit][cache]")
{
	// The captured case: 595 items at 128x128 is 64 KB each, ~38 MB.
	CHECK(IconViewThumbLoader::ComputeCacheSize(595, 128 * 128 * 4,
		256u * 1024u * 1024u, 26) == 595);
	// At 256px it is 256 KB each, ~150 MB, still inside the budget.
	CHECK(IconViewThumbLoader::ComputeCacheSize(595, 256 * 256 * 4,
		256u * 1024u * 1024u, 26) == 595);
	// A small folder still gets the prefetch floor so a scroll has room.
	CHECK(IconViewThumbLoader::ComputeCacheSize(10, 128 * 128 * 4,
		256u * 1024u * 1024u, 26) == 26);
}

TEST_CASE("A folder larger than the budget is capped, not grown", "[unit][cache]")
{
	// 50k items at 128px would be ~3 GB uncapped; the budget holds 4096.
	CHECK(IconViewThumbLoader::ComputeCacheSize(50000, 128 * 128 * 4,
		256u * 1024u * 1024u, 26) == 4096);
	// The cap always respects the byte budget, whatever the item size.
	for (guint size = 16; size <= 512; size *= 2)
	{
		guint bytes_per_item = size * size * 4;
		guint budget = 256u * 1024u * 1024u;
		guint n = IconViewThumbLoader::ComputeCacheSize(100000,
			bytes_per_item, budget, 26);
		CHECK(n <= budget / bytes_per_item);
	}
	// Degenerate inputs must not divide by zero or return nothing.
	CHECK(IconViewThumbLoader::ComputeCacheSize(0, 128 * 128 * 4, 1u << 20, 26) == 1);
	CHECK(IconViewThumbLoader::ComputeCacheSize(100, 0, 1u << 20, 26) == 1);
}

/* Regression guard: video thumbnails used to disappear completely.
 *
 * QuiverFileImpl::GetThumbnailTexture() bails out on its first line when
 * IsFolder() is true, so IsFolder() sits directly on the video thumbnail path.
 * It once fetched the GFileInfo on demand.  The constructor already queries
 * eagerly, so that only ever fired when the eager query had FAILED - which is
 * what happens on trash:// - and it then re-ran a failing query_info() on every
 * single call, from sort comparators and from the thumbnail worker thread.
 * Over gvfs each attempt is a D-Bus round trip, the queue stalled, and no video
 * produced either a thumbnail or a quick preview: strictly worse than the
 * handful of failures it was meant to fix.
 *
 * So IsFolder() must stay a pure read of whatever the constructor obtained.
 * Enumeration supplies the info directly (ImageList::EnumerateChildren passes
 * the GFileInfo into the QuiverFile ctor), so the common path is already warm
 * and nothing needs to be fetched here.
 */
TEST_CASE("Folder detection reads listing info instead of querying again", "[unit][cache][video]")
{
	std::string imagesDir = QuiverTest_GetImagesDir();
	gchar *dirUri = g_filename_to_uri(imagesDir.c_str(), NULL, NULL);
	REQUIRE(dirUri != NULL);

	/* The normal case: info handed over by the enumeration, no query needed. */
	GFile *dir = g_file_new_for_uri(dirUri);
	GFileInfo *info = g_file_query_info(dir, G_FILE_ATTRIBUTE_STANDARD_TYPE,
	                                    G_FILE_QUERY_INFO_NONE, NULL, NULL);
	REQUIRE(info != NULL);
	REQUIRE(g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY);

	QuiverFile listed(dirUri, info);
	CHECK(listed.IsFolder() == true);

	/* A URI that resolves to nothing must stay false so callers do not go
	 * drawing folders for it.  This is the state that used to trigger a
	 * blocking retry inside the predicate. */
	QuiverFile missing("trash:///quiver-does-not-exist", NULL);
	CHECK(missing.IsFolder() == false);

	g_object_unref(info);
	g_object_unref(dir);
	g_free(dirUri);
}

/* ---- failure-marker helpers -------------------------------------------- */

/* Thumbnails are cached per size, but the failure marker used to be keyed by
 * URI alone, so these helpers reason about the on-disk layout directly:
 *
 *     $XDG_CACHE_HOME/thumbnails/fail/quiver-<version>/<size>/<md5>.png
 *
 * The version segment is a build-time define the tests do not see, so it is
 * located by probing for our own size subdirectory.
 */

/* Raw MD5 hex of the URI with the .png suffix, matching
 * quiver_thumbnail_hash_filename().  g_compute_checksum_for_string() would not
 * line up, so build it the same way. */
static gchar* TestThumbnailHash(const gchar* uri)
{
	GChecksum* sum = g_checksum_new(G_CHECKSUM_MD5);
	g_checksum_update(sum, (const guchar*)uri, strlen(uri));
	gchar* hash = g_strconcat(g_checksum_get_string(sum), ".png", NULL);
	g_checksum_free(sum);
	return hash;
}

/* The <size> directory holding the marker for this uri, or NULL if the file has
 * no marker recorded for that size. */
static gchar* TestFailureMarkerDir(const gchar* uri, const gchar* size_name)
{
	gchar* hash = TestThumbnailHash(uri);
	gchar* failRoot = g_build_filename(g_get_user_cache_dir(), "thumbnails",
		"fail", NULL);
	gchar* found = NULL;

	if (DIR* d = opendir(failRoot))
	{
		while (struct dirent* ent = readdir(d))
		{
			if ('.' == ent->d_name[0])
				continue;
			gchar* cand = g_build_filename(failRoot, ent->d_name,
				size_name, hash, NULL);
			if (g_file_test(cand, G_FILE_TEST_EXISTS))
			{
				found = g_path_get_dirname(cand);
				g_free(cand);
				break;
			}
			g_free(cand);
		}
		closedir(d);
	}

	g_free(failRoot);
	g_free(hash);
	return found;
}

/* ---- tests ------------------------------------------------------------- */

/* A thumbnail that fails to generate used to block retries by writing a single
 * failure marker keyed only by the URI.  The skip check runs before the size is
 * considered, so one failure - a transient gvfs read, or a file still being
 * copied in - suppressed every larger size for the rest of the file's life,
 * even though those sizes had never been attempted.
 *
 * Both sizes fail here because the file really is undecodable, so the return
 * value cannot distinguish "attempted and failed" from "skipped without trying".
 * The marker is the observable: each size writes its own, and only when it is
 * actually tried.
 */
TEST_CASE("A thumbnail failure at one size does not block other sizes", "[unit][cache][thumbnail]")
{
	const std::string dir = "/tmp/quiver-thumb-size";
	g_mkdir_with_parents(dir.c_str(), 0755);
	const std::string path = dir + "/sized.jpg";

	const uint8_t junk[] = { 'n', 'o', 't', ' ', 'a', ' ', 'j', 'p', 'g' };
	FILE* f = fopen(path.c_str(), "wb");
	REQUIRE(f != nullptr);
	fwrite(junk, 1, sizeof(junk), f);
	fclose(f);

	gchar* uri = g_filename_to_uri(path.c_str(), NULL, NULL);
	REQUIRE(uri != nullptr);
	QuiverFile qf(uri);
	qf.RemoveCachedThumbnail(128);
	qf.RemoveCachedThumbnail(512);

	GdkTexture* small = qf.GetThumbnailTexture(128);
	CHECK(small == nullptr);              /* fails, and records the failure */
	if (small != nullptr)
		g_object_unref(small);

	GdkTexture* large = qf.GetThumbnailTexture(512);
	CHECK(large == nullptr);              /* also genuinely undecodable */

	/* The real assertion: both sizes recorded their own failure, which only
	 * happens if both were attempted.  A size-agnostic marker suppresses the
	 * 512 request outright and leaves no trace here. */
	gchar* normal = TestFailureMarkerDir(uri, "normal");
	gchar* xlarge = TestFailureMarkerDir(uri, "x-large");
	INFO("no marker for 128px: it was never tried");
	CHECK(normal != nullptr);
	INFO("no marker for 512px: it was skipped by the 128px failure");
	CHECK(xlarge != nullptr);
	g_free(normal);
	g_free(xlarge);

	if (large != nullptr)
		g_object_unref(large);
	qf.RemoveCachedThumbnail(128);
	qf.RemoveCachedThumbnail(512);
	g_free(uri);
	g_remove(path.c_str());
	g_rmdir(dir.c_str());
}

/* The marker records the failure against the file's mtime, so a file replaced
 * on disk is retried without the object changing.
 *
 * Written in three phases because the retry is only observable against a
 * control: with the mtime held still the marker stays valid and generation is
 * correctly *not* attempted, and only advancing the mtime lets it through.  A
 * test that skipped the middle phase would also pass with the marker mechanism
 * removed entirely, which is why it is asserted that a marker exists at all.
 */
TEST_CASE("A thumbnail is retried once the file's mtime changes", "[unit][cache][thumbnail]")
{
	const std::string dir = "/tmp/quiver-thumb-retry";
	g_mkdir_with_parents(dir.c_str(), 0755);
	const std::string path = dir + "/retried.jpg";

	const uint8_t junk[] = { 'n', 'o', 't', ' ', 'a', ' ', 'j', 'p', 'g' };
	FILE* f = fopen(path.c_str(), "wb");
	REQUIRE(f != nullptr);
	fwrite(junk, 1, sizeof(junk), f);
	fclose(f);

	/* A fixed, unmistakably old mtime keeps both phases independent of how
	 * fast the filesystem stamps writes. */
	const time_t base = 1700000000;
	g_autoptr(GFile) gf = g_file_new_for_path(path.c_str());
	g_autoptr(GFileInfo) info = g_file_query_info(gf,
		G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	REQUIRE(info != NULL);
	g_file_set_attribute_uint64(gf, G_FILE_ATTRIBUTE_TIME_MODIFIED,
		(guint64)base, G_FILE_QUERY_INFO_NONE, NULL, NULL);

	gchar* uri = g_filename_to_uri(path.c_str(), NULL, NULL);
	REQUIRE(uri != nullptr);
	QuiverFile qf(uri);
	/* A thumbnail left in the on-disk cache by an earlier run would be returned
	 * before the marker is ever consulted, which would make every phase below
	 * pass for the wrong reason.  Start from nothing. */
	qf.RemoveCachedThumbnail(128);

	CHECK(qf.GetThumbnailTexture(128) == nullptr);   /* phase 1: records a failure */

	/* The marker must exist, or nothing below proves anything. */
	gchar* marker = TestFailureMarkerDir(uri, "normal");
	REQUIRE(marker != NULL);
	g_free(marker);

	/* Swap in a real, decodable JPEG but leave the mtime alone: the marker is
	 * still valid, so this must stay suppressed.  This is the control that
	 * gives the final phase its meaning. */
	const std::string good = QuiverTest_GetImagesDir() + "/sample_rotated.jpg";
	gchar* goodBytes = NULL;
	gsize goodLen = 0;
	REQUIRE(g_file_get_contents(good.c_str(), &goodBytes, &goodLen, NULL));
	f = fopen(path.c_str(), "wb");
	REQUIRE(f != nullptr);
	fwrite(goodBytes, 1, goodLen, f);
	fclose(f);
	g_free(goodBytes);
	g_file_set_attribute_uint64(gf, G_FILE_ATTRIBUTE_TIME_MODIFIED,
		(guint64)base, G_FILE_QUERY_INFO_NONE, NULL, NULL);

	qf.Reload();
	CHECK(qf.GetThumbnailTexture(128) == nullptr);   /* phase 2: still suppressed */

	/* Now let the mtime move on, as an external edit would, and the stale
	 * marker no longer applies. */
	g_file_set_attribute_uint64(gf, G_FILE_ATTRIBUTE_TIME_MODIFIED,
		(guint64)(base + 60), G_FILE_QUERY_INFO_NONE, NULL, NULL);
	qf.Reload();

	GdkTexture* second = qf.GetThumbnailTexture(128);
	CHECK(second != nullptr);                        /* phase 3: retried, succeeded */
	if (second != nullptr)
		g_object_unref(second);

	qf.RemoveCachedThumbnail(128);
	g_free(uri);
	g_remove(path.c_str());
	g_rmdir(dir.c_str());
}

/* ---- GetCachedThumbnailAtMost ----------------------------------------- */

/* The real thumbnail cache, which unlike the failure markers is not version
 * stamped:  $XDG_CACHE_HOME/thumbnails/<size>/<md5>.png */
static gchar* TestThumbnailPath(const gchar* uri, const gchar* size_name)
{
	gchar* hash = TestThumbnailHash(uri);
	gchar* path = g_build_filename(g_get_user_cache_dir(), "thumbnails",
		size_name, hash, NULL);
	g_free(hash);
	return path;
}

/* A unique decodable image, so no other test can leave state behind for it. */
static gchar* TestMakeTempJpeg(const gchar* name)
{
	gchar* dir = g_build_filename("/tmp", "quiver-thumb-bucket", NULL);
	g_mkdir_with_parents(dir, 0755);
	gchar* path = g_build_filename(dir, name, NULL);
	g_free(dir);

	gchar* good = g_build_filename(QuiverTest_GetImagesDir().c_str(),
		"sample_rotated.jpg", NULL);
	gchar* bytes = NULL;
	gsize len = 0;
	REQUIRE(g_file_get_contents(good, &bytes, &len, NULL));
	REQUIRE(g_file_set_contents(path, bytes, len, NULL));
	g_free(bytes);
	g_free(good);

	gchar* uri = g_filename_to_uri(path, NULL, NULL);
	REQUIRE(uri != NULL);
	g_free(path);
	return uri;
}

/* SaveThumbnail() hands the write to a thread pool, so a thumbnail that was
 * just generated is not on disk yet.  Tests that assert against the disk path
 * have to wait for it rather than assume it landed. */
static bool TestWaitForFile(const gchar* path, int timeout_ms)
{
	for (int waited = 0; waited < timeout_ms; waited += 20)
	{
		if (g_file_test(path, G_FILE_TEST_EXISTS))
			return true;
		g_usleep(20000);
	}
	return g_file_test(path, G_FILE_TEST_EXISTS);
}

/* Generates a thumbnail of the given bucket and waits for it to reach the disk,
 * returning the size name it was cached under. */
static void TestGenerateAndPersist(QuiverFile& f, const gchar* uri, int size,
	const gchar* size_name)
{
	GdkTexture* tex = f.GetThumbnailTexture(size);
	REQUIRE(tex != NULL);
	g_object_unref(tex);

	gchar* path = TestThumbnailPath(uri, size_name);
	INFO("thumbnail was not written for the " << size_name << " bucket");
	REQUIRE(TestWaitForFile(path, 5000));
	g_free(path);
}

/* Drops every cached copy of uri and its on-disk thumbnails, so each test starts
 * from nothing regardless of what ran before it. */
static void TestPurgeAllThumbnails(const gchar* uri)
{
	QuiverFile f(uri);
	for (int size : {128, 256, 512})
		f.RemoveCachedThumbnail(size);
	/* The per-size caches are global, so a stale entry for this uri would
	 * otherwise satisfy the lookup without ever touching the disk path. */
	QuiverFile::ClearThumbnailCache();
}

/* The whole point of the method is that it must not decode, since the caller is
 * covering for a decode that is already in flight. */
TEST_CASE("The placeholder lookup never generates a thumbnail", "[unit][cache][thumbnail]")
{
	gchar* uri = TestMakeTempJpeg("never-generated.jpg");
	TestPurgeAllThumbnails(uri);

	QuiverFile f(uri);
	CHECK(f.GetCachedThumbnailAtMost(512) == NULL);

	/* Nothing was asked for, so nothing should have been written. */
	for (const char* sz : {"normal", "large", "x-large"})
	{
		gchar* path = TestThumbnailPath(uri, sz);
		INFO("a thumbnail was written for size " << sz
			<< ": the lookup generated instead of only reading");
		CHECK_FALSE(g_file_test(path, G_FILE_TEST_EXISTS));
		g_free(path);
	}

	TestPurgeAllThumbnails(uri);
	g_free(uri);
}

/* A cell smaller than the smallest bucket still gets a 128 thumbnail, so that is
 * the one to fall back on.  Comparing buckets against the raw request matches
 * nothing here and would disable the feature for every small icon size. */
TEST_CASE("A cell smaller than the smallest bucket falls back to 128", "[unit][cache][thumbnail]")
{
	gchar* uri = TestMakeTempJpeg("small-cell.jpg");
	TestPurgeAllThumbnails(uri);

	QuiverFile f(uri);
	/* Produce a 128 the ordinary way, so it lands in the cache on disk. */
	TestGenerateAndPersist(f, uri, 128, "normal");

	/* Now forget the decoded copy, so the only way to find it is the disk. */
	QuiverFile::ClearThumbnailCache();

	GdkTexture* fallback = f.GetCachedThumbnailAtMost(40);
	CHECK(fallback != NULL);
	if (fallback != NULL)
		g_object_unref(fallback);

	TestPurgeAllThumbnails(uri);
	g_free(uri);
}

/* One failure or eviction at a size must not leave a cell with nothing at all:
 * the smaller bucket is still better than the generic icon. */
TEST_CASE("The placeholder lookup falls back to a smaller cached size", "[unit][cache][thumbnail]")
{
	gchar* uri = TestMakeTempJpeg("smaller-only.jpg");
	TestPurgeAllThumbnails(uri);

	QuiverFile f(uri);
	TestGenerateAndPersist(f, uri, 128, "normal");
	QuiverFile::ClearThumbnailCache();

	/* Asking for something that resolves to 512, with only a 128 available. */
	GdkTexture* fallback = f.GetCachedThumbnailAtMost(512);
	CHECK(fallback != NULL);
	if (fallback != NULL)
	{
		guint side = std::max(gdk_texture_get_width(fallback),
			gdk_texture_get_height(fallback));
		CHECK(side <= 128);
		g_object_unref(fallback);
	}

	TestPurgeAllThumbnails(uri);
	g_free(uri);
}

/* Never hand back something larger than the bucket the request resolves to. */
TEST_CASE("The placeholder lookup never returns a larger size than asked", "[unit][cache][thumbnail]")
{
	gchar* uri = TestMakeTempJpeg("too-large.jpg");
	TestPurgeAllThumbnails(uri);

	QuiverFile f(uri);
	TestGenerateAndPersist(f, uri, 256, "large");
	QuiverFile::ClearThumbnailCache();

	/* 128 is cached, 128 is asked for, so nothing smaller qualifies and the
	 * 256 must not be handed back as a stand-in. */
	CHECK(f.GetCachedThumbnailAtMost(128) == NULL);

	TestPurgeAllThumbnails(uri);
	g_free(uri);
}

/* With several sizes cached the largest qualifying one wins. */
TEST_CASE("The placeholder lookup prefers the largest cached size", "[unit][cache][thumbnail]")
{
	gchar* uri = TestMakeTempJpeg("several-sizes.jpg");
	TestPurgeAllThumbnails(uri);

	QuiverFile f(uri);
	TestGenerateAndPersist(f, uri, 128, "normal");
	TestGenerateAndPersist(f, uri, 256, "large");
	QuiverFile::ClearThumbnailCache();

	GdkTexture* fallback = f.GetCachedThumbnailAtMost(512);
	REQUIRE(fallback != NULL);
	if (fallback != NULL)
	{
		guint side = std::max(gdk_texture_get_width(fallback),
			gdk_texture_get_height(fallback));
		/* The 256, not the 128: taking the first bucket that exists instead of
		 * the largest would hand back a smaller thumbnail than is available. */
		INFO("handed back " << side << "px, expected the 256 bucket");
		CHECK(side > 128);
		CHECK(side <= 256);
		g_object_unref(fallback);
	}

	TestPurgeAllThumbnails(uri);
	g_free(uri);
}

/* HasThumbnail() also answers true from m_mapThumbnailExists, which is not
 * cleared when the decoded copy is evicted or the file is removed.  Believing it
 * would send the lookup down the decode path in exactly the situation it exists
 * to avoid, so this covers the stale-exists case specifically. */
TEST_CASE("The placeholder lookup ignores a stale exists flag", "[unit][cache][thumbnail]")
{
	gchar* uri = TestMakeTempJpeg("stale-exists.jpg");

	QuiverFile f(uri);
	TestGenerateAndPersist(f, uri, 128, "normal");

	/* Drop the decoded copy and the file, leaving m_mapThumbnailExists set -
	 * the state where HasThumbnail() is true but there is nothing to read. */
	QuiverFile::ClearThumbnailCache();
	gchar* path = TestThumbnailPath(uri, "normal");
	REQUIRE(g_remove(path) == 0);
	g_free(path);

	CHECK(f.HasThumbnail(128));
	CHECK(f.GetCachedThumbnailAtMost(512) == NULL);

	TestPurgeAllThumbnails(uri);
	g_free(uri);
}
