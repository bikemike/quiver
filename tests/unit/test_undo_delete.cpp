#include <catch2/catch_test_macros.hpp>
#include <glib.h>

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