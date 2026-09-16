#include <catch2/catch_test_macros.hpp>
#include <glib.h>
#include <glib/gstdio.h>

#include <list>
#include <string>

#include "QuiverFile.h"
#include "QuiverFileOps.h"

// The trash undo stack is a pure in-memory structure (it records batches of
// trashed QuiverFiles so Ctrl+Z / the menu can restore them).  Restoring
// itself needs a working trash, which this headless test cannot guarantee, so
// only the stack bookkeeping and callback signalling are exercised here.

namespace {
struct CbRecorder {
    int delete_calls = 0;
    int restore_calls = 0;
    unsigned int delete_count = 0;
    unsigned int restore_count = 0;
    std::list<QuiverFileOps::TrashUndoChangedReason> reasons;
};

void cb(QuiverFileOps::TrashUndoChangedReason reason, unsigned int count, gpointer data) {
    CbRecorder* r = static_cast<CbRecorder*>(data);
    r->reasons.push_back(reason);
    if (reason == QuiverFileOps::TRASH_UNDO_DELETED) {
        r->delete_calls++;
        r->delete_count = count;
    } else {
        r->restore_calls++;
        r->restore_count = count;
    }
}
}

TEST_CASE("Trash undo stack records batches newest-first", "[unit][undo][trash]")
{
    QuiverFileOps::UndoStackClear();

    std::list<QuiverFile> batch1;
    batch1.push_back(QuiverFile("file:///pics/a.jpg"));
    std::list<QuiverFile> batch2;
    batch2.push_back(QuiverFile("file:///pics/b.jpg"));
    batch2.push_back(QuiverFile("file:///pics/c.jpg"));

    REQUIRE(QuiverFileOps::UndoStackRecord(batch1));
    REQUIRE(QuiverFileOps::UndoStackSize() == 1);

    REQUIRE(QuiverFileOps::UndoStackRecord(batch2));
    REQUIRE(QuiverFileOps::UndoStackSize() == 2);

    // Newest batch first.
    REQUIRE(QuiverFileOps::UndoStackPeekTop() != nullptr);
    REQUIRE(QuiverFileOps::UndoStackPeekTop()->size() == 2);
    const std::list<QuiverFile>* second = QuiverFileOps::UndoStackAt(1);
    REQUIRE(second != nullptr);
    REQUIRE(second->size() == 1);

    // Empty batches are rejected and do not grow the stack.
    std::list<QuiverFile> empty;
    REQUIRE_FALSE(QuiverFileOps::UndoStackRecord(empty));
    REQUIRE(QuiverFileOps::UndoStackSize() == 2);

    // Out-of-range access is null-safe.
    REQUIRE(QuiverFileOps::UndoStackAt(99) == nullptr);

    // A restore attempt drops the batch even if the sandbox trash is missing.
    // (RestoreFromTrash fails headless, so the count is 0, not the batch size.)
    int restored = QuiverFileOps::UndoStackPop();
    REQUIRE((restored == 0 || restored == 2));
    REQUIRE(QuiverFileOps::UndoStackSize() == 1);

    QuiverFileOps::UndoStackClear();
    REQUIRE(QuiverFileOps::UndoStackSize() == 0);
    REQUIRE_FALSE(QuiverFileOps::UndoStackHasItems());
}

TEST_CASE("Trash undo stack notifies about deletes", "[unit][undo][trash]")
{
    CbRecorder rec;
    QuiverFileOps::SetTrashUndoChangedCallback(cb, &rec);
    QuiverFileOps::UndoStackClear();

    std::list<QuiverFile> batch;
    batch.push_back(QuiverFile("file:///pics/a.jpg"));
    batch.push_back(QuiverFile("file:///pics/b.jpg"));

    REQUIRE(QuiverFileOps::UndoStackRecord(batch));
    REQUIRE(rec.delete_calls == 1);
    REQUIRE(rec.delete_count == 2);

    // Recording an empty batch must not fire.
    std::list<QuiverFile> empty;
    REQUIRE_FALSE(QuiverFileOps::UndoStackRecord(empty));
    REQUIRE(rec.delete_calls == 1);

    QuiverFileOps::UndoStackClear();
    QuiverFileOps::SetTrashUndoChangedCallback(nullptr, nullptr);
}

TEST_CASE("Undo stack records and undoes Move, Copy, and Rotate", "[unit][undo]")
{
    QuiverFileOps::UndoStackClear();

    // 1. Move undo
    std::vector<QuiverFileOps::UndoFilePair> move_pairs;
    QuiverFileOps::UndoFilePair p1;
    p1.src_uri = "file:///dirA/file1.jpg";
    p1.dst_uri = "file:///dirB/file1.jpg";
    move_pairs.push_back(p1);

    REQUIRE(QuiverFileOps::UndoStackRecordMove(move_pairs));
    REQUIRE(QuiverFileOps::UndoStackSize() == 1);
    REQUIRE(QuiverFileOps::UndoStackTopType() == QuiverFileOps::UNDO_TYPE_MOVE);

    // 2. Copy undo
    std::vector<std::string> copies = { "file:///dirB/copy1.jpg", "file:///dirB/copy2.jpg" };
    REQUIRE(QuiverFileOps::UndoStackRecordCopy(copies));
    REQUIRE(QuiverFileOps::UndoStackSize() == 2);
    REQUIRE(QuiverFileOps::UndoStackTopType() == QuiverFileOps::UNDO_TYPE_COPY);

    // 3. Rotate undo
    static int s_rotated_dir = 0;
    static std::string s_rotated_uri;
    QuiverFileOps::SetRotateUndoCallback(+[](const char* uri, int dir, gpointer) {
        s_rotated_uri = uri ? uri : "";
        s_rotated_dir = dir;
    }, nullptr);

    REQUIRE(QuiverFileOps::UndoStackRecordRotate("file:///dirA/image.png", +1));
    REQUIRE(QuiverFileOps::UndoStackSize() == 3);
    REQUIRE(QuiverFileOps::UndoStackTopType() == QuiverFileOps::UNDO_TYPE_ROTATE);

    // Pop Rotate: should invoke rotate callback with opposite direction (-1)
    int rot_ret = QuiverFileOps::UndoStackPop();
    REQUIRE(rot_ret == 1);
    REQUIRE(s_rotated_dir == -1);
    REQUIRE(s_rotated_uri == "file:///dirA/image.png");
    REQUIRE(QuiverFileOps::UndoStackSize() == 2);

    // Top is now Copy
    REQUIRE(QuiverFileOps::UndoStackTopType() == QuiverFileOps::UNDO_TYPE_COPY);

    QuiverFileOps::UndoStackClear();
    QuiverFileOps::SetRotateUndoCallback(nullptr, nullptr);
}

TEST_CASE("Undo stack records and undoes New Folder", "[unit][undo][new_folder]")
{
    QuiverFileOps::UndoStackClear();

    static std::string s_undone_folder;
    QuiverFileOps::SetNewFolderUndoCallback(+[](const char* folder_uri, gpointer) {
        s_undone_folder = folder_uri ? folder_uri : "";
    }, nullptr);

    // Create a temporary directory so we can test the undo deletion
    char* tmp_dir = g_dir_make_tmp("quiver_test_new_folder_XXXXXX", nullptr);
    REQUIRE(tmp_dir != nullptr);

    char* test_folder = g_build_filename(tmp_dir, "MyNewFolder", nullptr);
    REQUIRE(g_mkdir(test_folder, 0755) == 0);
    REQUIRE(g_file_test(test_folder, G_FILE_TEST_IS_DIR));

    GFile* f = g_file_new_for_path(test_folder);
    char* folder_uri = g_file_get_uri(f);

    REQUIRE(QuiverFileOps::UndoStackRecordNewFolder(folder_uri));
    REQUIRE(QuiverFileOps::UndoStackSize() == 1);
    REQUIRE(QuiverFileOps::UndoStackTopType() == QuiverFileOps::UNDO_TYPE_NEW_FOLDER);

    const QuiverFileOps::UndoEntry* entry = QuiverFileOps::UndoStackEntryAt(0);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->type == QuiverFileOps::UNDO_TYPE_NEW_FOLDER);
    REQUIRE(entry->new_folder_uri == folder_uri);

    // Pop/undo New Folder: should remove the directory and invoke callback
    s_undone_folder.clear();
    int ret = QuiverFileOps::UndoStackPop();
    REQUIRE(ret == 1);
    REQUIRE(s_undone_folder == folder_uri);
    REQUIRE(!g_file_test(test_folder, G_FILE_TEST_EXISTS));
    REQUIRE(QuiverFileOps::UndoStackSize() == 0);

    g_free(folder_uri);
    g_object_unref(f);
    g_free(test_folder);
    g_rmdir(tmp_dir);
    g_free(tmp_dir);

    QuiverFileOps::SetNewFolderUndoCallback(nullptr, nullptr);
    QuiverFileOps::UndoStackClear();
}