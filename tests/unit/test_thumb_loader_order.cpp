#include <catch2/catch_test_macros.hpp>
#include "IconViewThumbLoader.h"
#include "QuiverFile.h"
#include "test_helpers.h"

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
