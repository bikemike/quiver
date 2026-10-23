#include <catch2/catch_test_macros.hpp>
#include "QuiverFile.h"
#include "ImageList.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <vector>
#include <string>

TEST_CASE("QuiverFile SetURI in-place update preserves metadata", "[unit][quiverfile][rename]")
{
    std::string samplePath = QUIVER_TEST_IMAGES_DIR_DEF "/sample_rotated.jpg";
    char* srcUri = g_filename_to_uri(samplePath.c_str(), NULL, NULL);
    REQUIRE(srcUri != NULL);

    QuiverFile file(srcUri);
    int origWidth = file.GetWidth();
    int origHeight = file.GetHeight();
    int origOri = file.GetOrientation();
    time_t origTime = file.GetTimeT();

    REQUIRE(origWidth > 0);
    REQUIRE(origHeight > 0);
    REQUIRE(origOri > 0);

    // Rename in-place using SetURI
    gchar* newPath = g_build_filename(g_get_tmp_dir(), "quiver_test_renamed_123.jpg", NULL);
    char* dstUri = g_filename_to_uri(newPath, NULL, NULL);

    file.SetURI(dstUri);

    CHECK(std::string(file.GetURI()) == std::string(dstUri));
    CHECK(file.GetWidth() == origWidth);
    CHECK(file.GetHeight() == origHeight);
    CHECK(file.GetOrientation() == origOri);
    CHECK(file.GetTimeT() == origTime);

    g_free(newPath);
    g_free(srcUri);
    g_free(dstUri);
}

TEST_CASE("QuiverFile CreateCopy clones metadata without disk parsing", "[unit][quiverfile][copy]")
{
    std::string samplePath = QUIVER_TEST_IMAGES_DIR_DEF "/sample_rotated.jpg";
    char* srcUri = g_filename_to_uri(samplePath.c_str(), NULL, NULL);
    REQUIRE(srcUri != NULL);

    QuiverFile srcFile(srcUri);
    int origWidth = srcFile.GetWidth();
    int origHeight = srcFile.GetHeight();
    int origOri = srcFile.GetOrientation();
    time_t origTime = srcFile.GetTimeT();

    gchar* dstPath = g_build_filename(g_get_tmp_dir(), "quiver_test_copied.jpg", NULL);
    char* dstUri = g_filename_to_uri(dstPath, NULL, NULL);

    QuiverFile copiedFile = QuiverFile::CreateCopy(srcFile, dstUri);

    CHECK(std::string(copiedFile.GetURI()) == std::string(dstUri));
    CHECK(copiedFile.GetWidth() == origWidth);
    CHECK(copiedFile.GetHeight() == origHeight);
    CHECK(copiedFile.GetOrientation() == origOri);
    CHECK(copiedFile.GetTimeT() == origTime);

    g_free(dstPath);
    g_free(srcUri);
    g_free(dstUri);
}

TEST_CASE("ImageList Active Rename and Copy Registry", "[unit][imagelist][rename]")
{
    std::string src1 = "file:///tmp/img1.jpg";
    std::string dst1 = "file:///tmp/renamed1.jpg";
    std::string src2 = "file:///tmp/img2.jpg";
    std::string dst2 = "file:///tmp/renamed2.jpg";

    std::vector<std::pair<std::string, std::string>> renames = {
        {src1, dst1},
        {src2, dst2}
    };

    CHECK_FALSE(ImageList::IsActiveRename(src1));
    CHECK_FALSE(ImageList::IsActiveRename(dst1));

    ImageList::RegisterActiveRenames(renames);

    CHECK(ImageList::IsActiveRename(src1));
    CHECK(ImageList::IsActiveRename(dst1));
    CHECK(ImageList::IsActiveRename(src2));
    CHECK(ImageList::IsActiveRename(dst2));
    CHECK_FALSE(ImageList::IsActiveRename("file:///tmp/other.jpg"));

    ImageList::UnregisterActiveRenames(renames);

    CHECK_FALSE(ImageList::IsActiveRename(src1));
    CHECK_FALSE(ImageList::IsActiveRename(dst1));

    // Copy registry test
    std::string copySrc = "file:///tmp/original.jpg";
    std::string copyDst = "file:///tmp/copy.jpg";

    ImageList::RegisterActiveCopy(copySrc, copyDst);
    std::string retrievedSrc;
    CHECK(ImageList::GetActiveCopySource(copyDst, retrievedSrc));
    CHECK(retrievedSrc == copySrc);

    ImageList::UnregisterActiveCopy(copyDst);
    CHECK_FALSE(ImageList::GetActiveCopySource(copyDst, retrievedSrc));
}

TEST_CASE("ImageList ApplyRenames in-place batch update", "[unit][imagelist][batch]")
{
    gchar* dirPath = g_dir_make_tmp("quiver_batch_rename_XXXXXX", NULL);
    REQUIRE(dirPath != NULL);

    gchar* f1 = g_build_filename(dirPath, "a_photo1.jpg", NULL);
    gchar* f2 = g_build_filename(dirPath, "b_photo2.jpg", NULL);
    gchar* f3 = g_build_filename(dirPath, "c_photo3.jpg", NULL);

    g_file_set_contents(f1, "data", 4, NULL);
    g_file_set_contents(f2, "data", 4, NULL);
    g_file_set_contents(f3, "data", 4, NULL);

    char* uri1 = g_filename_to_uri(f1, NULL, NULL);
    char* uri2 = g_filename_to_uri(f2, NULL, NULL);
    char* uri3 = g_filename_to_uri(f3, NULL, NULL);

    std::list<std::string> fileList = { uri1, uri2, uri3 };

    ImageListPtr imgList(new ImageList(false));
    imgList->SetImageList(&fileList, false);

    REQUIRE(imgList->GetSize() == 3);

    // Apply batch rename of photo1 and photo2
    gchar* f1_new = g_build_filename(dirPath, "z_photo1_renamed.jpg", NULL);
    gchar* f2_new = g_build_filename(dirPath, "y_photo2_renamed.jpg", NULL);
    char* uri1_new = g_filename_to_uri(f1_new, NULL, NULL);
    char* uri2_new = g_filename_to_uri(f2_new, NULL, NULL);

    std::vector<std::pair<std::string, std::string>> batch = {
        {uri1, uri1_new},
        {uri2, uri2_new}
    };

    imgList->ApplyRenames(batch);

    // Verify all 3 files remain in list with updated URIs
    REQUIRE(imgList->GetSize() == 3);
    std::vector<QuiverFile> files = imgList->GetQuiverFiles();
    bool foundRenamed1 = false;
    bool foundRenamed2 = false;
    bool foundUntouched3 = false;

    for (const auto& qf : files)
    {
        if (std::string(qf.GetURI()) == std::string(uri1_new)) foundRenamed1 = true;
        if (std::string(qf.GetURI()) == std::string(uri2_new)) foundRenamed2 = true;
        if (std::string(qf.GetURI()) == std::string(uri3)) foundUntouched3 = true;
    }

    CHECK(foundRenamed1);
    CHECK(foundRenamed2);
    CHECK(foundUntouched3);

    // Cleanup
    g_remove(f1);
    g_remove(f2);
    g_remove(f3);
    g_remove(dirPath);
    g_free(f1);
    g_free(f2);
    g_free(f3);
    g_free(f1_new);
    g_free(f2_new);
    g_free(dirPath);
    g_free(uri1);
    g_free(uri2);
    g_free(uri3);
    g_free(uri1_new);
    g_free(uri2_new);
}

TEST_CASE("ImageList ApplyRenames removes files moved outside watched folders", "[unit][imagelist][batch][membership]")
{
    gchar* dirPath = g_dir_make_tmp("quiver_watched_dir_XXXXXX", NULL);
    gchar* outsideDir = g_dir_make_tmp("quiver_outside_dir_XXXXXX", NULL);
    REQUIRE(dirPath != NULL);
    REQUIRE(outsideDir != NULL);

    gchar* f1 = g_build_filename(dirPath, "stay.jpg", NULL);
    gchar* f2 = g_build_filename(dirPath, "move_out.jpg", NULL);

    g_file_set_contents(f1, "data", 4, NULL);
    g_file_set_contents(f2, "data", 4, NULL);

    char* uri1 = g_filename_to_uri(f1, NULL, NULL);
    char* uri2 = g_filename_to_uri(f2, NULL, NULL);
    char* dirUri = g_filename_to_uri(dirPath, NULL, NULL);

    std::list<std::string> folderList = { dirUri };

    ImageListPtr imgList(new ImageList(false));
    imgList->SetImageList(&folderList, false);

    REQUIRE(imgList->GetSize() == 2);

    // f1 is renamed within dirPath
    gchar* f1_new = g_build_filename(dirPath, "stay_renamed.jpg", NULL);
    char* uri1_new = g_filename_to_uri(f1_new, NULL, NULL);

    // f2 is moved to outsideDir
    gchar* f2_new = g_build_filename(outsideDir, "organized_out.jpg", NULL);
    char* uri2_new = g_filename_to_uri(f2_new, NULL, NULL);

    CHECK(imgList->IsURIInImageList(uri1_new));
    CHECK_FALSE(imgList->IsURIInImageList(uri2_new));

    std::vector<std::pair<std::string, std::string>> batch = {
        {uri1, uri1_new},
        {uri2, uri2_new}
    };

    imgList->ApplyRenames(batch);

    // imgList should now have exactly 1 file: stay_renamed.jpg
    REQUIRE(imgList->GetSize() == 1);
    CHECK(std::string((*imgList)[0].GetURI()) == std::string(uri1_new));

    // Cleanup
    g_remove(f1);
    g_remove(f2);
    g_remove(dirPath);
    g_remove(outsideDir);
    g_free(f1);
    g_free(f2);
    g_free(f1_new);
    g_free(f2_new);
    g_free(dirPath);
    g_free(outsideDir);
    g_free(uri1);
    g_free(uri2);
    g_free(uri1_new);
    g_free(uri2_new);
    g_free(dirUri);
}
