#include <catch2/catch_test_macros.hpp>
#include "ImageLoader.h"
#include "QuiverUtils.h"

/* Everything here is stated against a 4080x3072 stored image, so "transposed"
 * has one unambiguous meaning: the axes are swapped relative to stored, i.e.
 * 3072x4080.  Orientations 5-8 are the transposed half of the EXIF table - each
 * is a quarter turn from one of 1-4 - so pixels resting at one are rotated
 * relative to the stored width and height. */
static const int kStoredW = 4080;
static const int kStoredH = 3072;

static bool IsTransposedOrientation(int orientation) { return 4 < orientation; }

TEST_CASE("Declared size follows the orientation the pixels rest at",
          "[unit][loader][orientation]")
{
    int w = -1, h = -1;

    for (int pixels_at = 1; pixels_at <= 8; ++pixels_at)
    {
        DYNAMIC_SECTION("pixels at " << pixels_at)
        {
            ImageLoader::DeclaredSize(kStoredW, kStoredH, pixels_at, &w, &h);

            if (IsTransposedOrientation(pixels_at))
                REQUIRE((w == kStoredH && h == kStoredW));
            else
                REQUIRE((w == kStoredW && h == kStoredH));
        }
    }
}

TEST_CASE("Declared size always describes the texture actually delivered",
          "[unit][loader][orientation]")
{
    /* The regression as an invariant, checked over the whole EXIF table so no
     * combination can hide it.
     *
     * A load decodes at the file's own orientation, then applies the turn needed
     * to reach the requested one.  The size announced to observers has to be the
     * size of the texture that comes out the far side - if it is the transpose of
     * it, the view is told the image arrived with the opposite aspect ratio, and
     * the reload it then asks for is answered by another resample. */
    for (int file_ori = 1; file_ori <= 8; ++file_ori)
    {
        for (int request = 1; request <= 8; ++request)
        {
            DYNAMIC_SECTION("file orientation " << file_ori << " requested as " << request)
            {
                int turn = 1;
                const int pixels_at =
                    QuiverUtils::ExifOrientationApplied(file_ori, request, &turn);

                /* Texture as EnsureExifOrientation leaves it ... */
                int tex_w = kStoredW, tex_h = kStoredH;
                if (IsTransposedOrientation(file_ori)) std::swap(tex_w, tex_h);
                /* ... then the loader's turn. */
                if (IsTransposedOrientation(turn)) std::swap(tex_w, tex_h);

                int w = -1, h = -1;
                ImageLoader::DeclaredSize(kStoredW, kStoredH, pixels_at, &w, &h);

                REQUIRE((w == tex_w));
                REQUIRE((h == tex_h));
            }
        }
    }
}

TEST_CASE("An image needing no turn is still declared at its transposed size",
          "[unit][loader][orientation]")
{
    /* The reported fault, exactly.  A file with orientation 8 requested as 8:
     * EnsureExifOrientation has already turned the pixels into 3072x4080 and
     * the loader then applies no turn at all.  Keying the swap on the turn -
     * which is what this used to do - announced the untouched 4080x3072, so the
     * final delivery of an image flashed at the wrong dimensions. */
    int turn = 1;
    const int pixels_at = QuiverUtils::ExifOrientationApplied(8, 8, &turn);
    REQUIRE(pixels_at == 8);
    REQUIRE(turn == 1);

    int w = -1, h = -1;
    ImageLoader::DeclaredSize(kStoredW, kStoredH, pixels_at, &w, &h);
    REQUIRE((w == kStoredH && h == kStoredW));

    /* And the rule that was wrong, stated so it cannot be reintroduced. */
    REQUIRE(IsTransposedOrientation(turn) == false);
    REQUIRE(IsTransposedOrientation(pixels_at) == true);
}

TEST_CASE("A portrait photo is declared at its display size", "[unit][loader][orientation]")
{
    /* Orientation 6 over a stored 2304x1728: what the viewer must be told is
     * 1728x2304.  This is every photo shot in portrait, and the case in which
     * getting it backwards inverts the aspect ratio on screen. */
    int w = -1, h = -1;
    ImageLoader::DeclaredSize(2304, 1728, 6, &w, &h);
    REQUIRE((w == 1728 && h == 2304));
}

TEST_CASE("Quarter turns without transposition keep the stored size",
          "[unit][loader][orientation]")
{
    int w = -1, h = -1;
    for (int pixels_at : {1, 2, 3, 4})
    {
        ImageLoader::DeclaredSize(kStoredW, kStoredH, pixels_at, &w, &h);
        REQUIRE((w == kStoredW && h == kStoredH));
    }
}