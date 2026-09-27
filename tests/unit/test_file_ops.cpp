#include <catch2/catch_test_macros.hpp>

#include <list>
#include <string>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "QuiverClipboard.h"
#include "QuiverFileOps.h"
#include "test_helpers.h"

// IsInsideFolder() is what drop targets use to refuse a drop aimed at the
// folder the dragged item already lives in, and to refuse moving a folder
// into itself or its own subtree.  It is a pure URI containment test, so it
// needs no filesystem and no drag - which is exactly why it is worth
// covering here: the naive strncmp() version of this test passes every
// obvious case and still lets a folder be dropped into itself, because
// "file:///a/bc" has "file:///a/b" as a string prefix without being inside it.

TEST_CASE("QuiverFileOps folder containment", "[unit][fileops][fast]")
{
    SECTION("An item is inside the folder that contains it")
    {
        REQUIRE(QuiverFileOps::IsInsideFolder("file:///photos/a.jpg", "file:///photos"));
        REQUIRE(QuiverFileOps::IsInsideFolder("file:///photos/2019/b.jpg", "file:///photos/2019"));
    }

    SECTION("An item is inside the folder that is its own URI")
    {
        REQUIRE(QuiverFileOps::IsInsideFolder("file:///photos", "file:///photos"));
    }

    SECTION("A shared string prefix is not containment")
    {
        /* The regression this guards: both of these start with
         * "file:///photos" as a string, yet neither is inside that folder. */
        REQUIRE_FALSE(QuiverFileOps::IsInsideFolder("file:///photos2", "file:///photos"));
        REQUIRE_FALSE(QuiverFileOps::IsInsideFolder("file:///photosbackup", "file:///photos"));
    }

    SECTION("An item outside the folder is outside it")
    {
        REQUIRE_FALSE(QuiverFileOps::IsInsideFolder("file:///other/a.jpg", "file:///photos"));
    }

    SECTION("Containment only runs downwards")
    {
        /* Moving a folder into its own subtree is caught by the caller
         * asking the other way round; the ancestor is not "inside" the
         * descendant. */
        REQUIRE_FALSE(QuiverFileOps::IsInsideFolder("file:///photos", "file:///photos/2019"));
    }

    SECTION("A trailing slash on the folder URI does not change the result")
    {
        REQUIRE(QuiverFileOps::IsInsideFolder("file:///photos/a.jpg", "file:///photos/"));
        REQUIRE(QuiverFileOps::IsInsideFolder("file:///photos/sub/a.jpg", "file:///photos/"));
        REQUIRE_FALSE(QuiverFileOps::IsInsideFolder("file:///photosbackup", "file:///photos/"));
    }

    SECTION("Percent-encoded names compare as URIs, not as raw text")
    {
        REQUIRE(QuiverFileOps::IsInsideFolder("file:///photos/holiday%20photo.jpg", "file:///photos"));
        /* A raw prefix match on the encoded name would wrongly accept this. */
        REQUIRE_FALSE(QuiverFileOps::IsInsideFolder("file:///photos/holiday", "file:///photos/holiday%20photo.jpg"));
    }

    SECTION("Empty URIs are never contained")
    {
        REQUIRE_FALSE(QuiverFileOps::IsInsideFolder("", "file:///photos"));
        REQUIRE_FALSE(QuiverFileOps::IsInsideFolder("file:///photos/a.jpg", ""));
        REQUIRE_FALSE(QuiverFileOps::IsInsideFolder("", ""));
    }
}

// DropRefused() is the single answer both the hover handlers and the drop
// handlers give for a drag.  It refuses a drop with no target, a file aimed
// at itself, and a folder aimed at itself or its own subtree.  It does NOT
// refuse an item that merely lives under the target: dragging /a/b/f.jpg
// onto /a is a real move, and the tree only ever shows /a as a row.
TEST_CASE("QuiverFileOps drop refusal policy", "[unit][fileops][fast]")
{
    std::list<std::string> one_file;
    one_file.push_back("file:///downloads/holiday.jpg");

    SECTION("No target means no drop")
    {
        REQUIRE(QuiverFileOps::DropRefused(one_file, ""));
    }

    SECTION("An unrelated folder accepts the file")
    {
        REQUIRE_FALSE(QuiverFileOps::DropRefused(one_file, "file:///photos"));
    }

    SECTION("An ancestor folder accepts a file that lives under it")
    {
        /* The regression this guards: "file:///a/b/f.jpg" does live inside
         * "file:///a", but dropping it there is the move the folder tree
         * exists for, not a no-op. */
        std::list<std::string> nested;
        nested.push_back("file:///a/b/f.jpg");
        REQUIRE_FALSE(QuiverFileOps::DropRefused(nested, "file:///a"));
    }

    SECTION("The folder the file already sits in is refused")
    {
        /* "file:///a/b/f.jpg" is directly in "file:///a/b": dropping it
         * there would land it back where it started. */
        std::list<std::string> nested;
        nested.push_back("file:///a/b/f.jpg");
        REQUIRE(QuiverFileOps::DropRefused(nested, "file:///a/b"));
        REQUIRE_FALSE(QuiverFileOps::DropRefused(nested, "file:///a"));
        REQUIRE(QuiverFileOps::DropRefused(one_file, "file:///downloads"));
    }

    SECTION("A trailing slash on the target does not change the answer")
    {
        std::list<std::string> nested;
        nested.push_back("file:///a/b/f.jpg");
        REQUIRE(QuiverFileOps::DropRefused(nested, "file:///a/b/"));
        REQUIRE_FALSE(QuiverFileOps::DropRefused(nested, "file:///a/"));
    }

    SECTION("A file is not a destination")
    {
        REQUIRE(QuiverFileOps::DropRefused(one_file, "file:///downloads/holiday.jpg"));
    }

    SECTION("A folder is refused into itself and into its own subtree")
    {
        std::list<std::string> one_folder;
        one_folder.push_back("file:///photos");
        REQUIRE(QuiverFileOps::DropRefused(one_folder, "file:///photos"));
        REQUIRE(QuiverFileOps::DropRefused(one_folder, "file:///photos/2019"));
    }

    SECTION("A child folder dropped on its parent is a no-op, so it is refused")
    {
        /* Dropping /photos/2019 on /photos resolves to /photos/2019 again:
         * the folder is already directly in the target. */
        std::list<std::string> one_folder;
        one_folder.push_back("file:///photos/2019");
        REQUIRE(QuiverFileOps::DropRefused(one_folder, "file:///photos"));
        /* its own parent is different: that one is a real move */
        REQUIRE_FALSE(QuiverFileOps::DropRefused(one_folder, "file:///"));
    }

    SECTION("A name that merely starts with the target name is not inside it")
    {
        REQUIRE_FALSE(QuiverFileOps::DropRefused(one_file, "file:///downloads2"));
    }

    SECTION("A file at the root has no parent to compare against")
    {
        std::list<std::string> root_file;
        root_file.push_back("file:///f.jpg");
        REQUIRE_FALSE(QuiverFileOps::DropRefused(root_file, "file:///photos"));
    }

    SECTION("One refused item refuses the whole drop")
    {
        std::list<std::string> mixed;
        mixed.push_back("file:///downloads/holiday.jpg");
        mixed.push_back("file:///photos");
        REQUIRE(QuiverFileOps::DropRefused(mixed, "file:///photos/2019"));
    }

    SECTION("An empty payload is still refused without a target")
    {
        std::list<std::string> nothing;
        REQUIRE(QuiverFileOps::DropRefused(nothing, ""));
        /* Nothing known about the drag: the caller decides, so the policy
         * itself must not silently answer "allowed". */
        REQUIRE_FALSE(QuiverFileOps::DropRefused(nothing, "file:///photos"));
    }
}

// EmptyAreaDropTarget() decides whether the empty space of a list view is a
// drop destination at all.  Only ordinary folder navigation qualifies: one
// non-recursive folder root.  In a search, tag, album or recursive
// collection the empty space spans several possible destinations, so
// offering one of them would move files somewhere the user did not point.
TEST_CASE("QuiverFileOps empty area drop target", "[unit][fileops][fast]")
{
    std::list<std::string> one_folder;
    one_folder.push_back("file:///photos");

    SECTION("A plain folder view drops into the folder it shows")
    {
        REQUIRE(QuiverFileOps::EmptyAreaDropTarget(one_folder, false, true)
            == "file:///photos");
    }

    SECTION("A single file root is not a destination")
    {
        /* The location bar accepts one file, and the list then shows that
         * file: there is no folder to drop into. */
        std::list<std::string> one_file;
        one_file.push_back("file:///photos/a.jpg");
        REQUIRE(QuiverFileOps::EmptyAreaDropTarget(one_file, false, false).empty());
    }

    SECTION("A recursive folder is not a destination")
    {
        REQUIRE(QuiverFileOps::EmptyAreaDropTarget(one_folder, true, true).empty());
    }

    SECTION("Several roots are not a destination")
    {
        std::list<std::string> two_folders;
        two_folders.push_back("file:///photos");
        two_folders.push_back("file:///videos");
        REQUIRE(QuiverFileOps::EmptyAreaDropTarget(two_folders, false, true).empty());
    }

    SECTION("No roots at all are not a destination")
    {
        std::list<std::string> nothing;
        REQUIRE(QuiverFileOps::EmptyAreaDropTarget(nothing, false, true).empty());
    }

    SECTION("The filesystem answer is what makes a root usable")
    {
        /* Same roots, only the stat differs: a root that turned out not to
         * be a directory must not be offered. */
        REQUIRE(QuiverFileOps::EmptyAreaDropTarget(one_folder, false, false).empty());
    }
}


// The hover decision in both views runs through this: a folder is only
// highlighted when the drag carries files the shared policy allows on it.
// The interesting case is a drag whose contents GTK has not finished
// preloading - highlighting then is what made a refused drop look accepted
// (a file over the folder it already lives in showed green and moved
// nothing), so "unknown" has to answer as "not a target".
TEST_CASE("QuiverClipboard drop contents decide a hover", "[unit][fileops][dnd]")
{
    REQUIRE_DISPLAY();

    GtkDropTarget* target = gtk_drop_target_new(G_TYPE_STRING,
        static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    REQUIRE(target != nullptr);
    /* the views set this so the contents are known before the release */
    g_object_set(target, "preload", TRUE, nullptr);
    REQUIRE(gtk_drop_target_get_preload(target));

    std::list<std::string> uris;
    bool cut = true;
    QuiverClipboard::DropContents contents;

    SECTION("No drop over the target means no target to offer")
    {
        REQUIRE_FALSE(contents.Accepts(target, "file:///photos", uris, cut));
        REQUIRE(uris.empty());
    }

    SECTION("A NULL target is no target either")
    {
        REQUIRE_FALSE(contents.Accepts(nullptr, "file:///photos", uris, cut));
        REQUIRE(uris.empty());
    }

    SECTION("A reset is inert")
    {
        contents.Reset();
        REQUIRE_FALSE(contents.Accepts(target, "file:///photos", uris, cut));
    }

    g_object_unref(target);
}

namespace
{
    /* g_remove() only unlinks a name: it does not empty a directory that
     * still has entries in it, so a fixture would leak files into the next
     * Catch2 section and turn a real transfer into a skipped conflict. */
    void remove_tree(const std::string& path)
    {
        GDir* dir = g_dir_open(path.c_str(), 0, nullptr);
        if (dir != nullptr) {
            const gchar* name = nullptr;
            while ((name = g_dir_read_name(dir)) != nullptr) {
                remove_tree(path + "/" + name);
            }
            g_dir_close(dir);
        }
        g_remove(path.c_str());
    }
}

// TransferFiles() is the last step of a drop: the hover only promises a
// target, this actually moves the files.  It runs on real files here because
// a broken transfer looks exactly like a drop that "did nothing".
TEST_CASE("QuiverFileOps transfer", "[unit][fileops][temp]")
{
    const std::string base = "/tmp/quiver_file_ops_test";
    const std::string a = base + "/a";
    const std::string b = base + "/a/b";
    const std::string other = base + "/other";

    /* Catch2 re-runs this body once per section, so start from a clean tree
     * every time: a leftover destination from the previous section would
     * turn a successful transfer into a skipped conflict. */
    remove_tree(base);
    g_mkdir_with_parents(a.c_str(), 0755);
    g_mkdir_with_parents(b.c_str(), 0755);
    g_mkdir_with_parents(other.c_str(), 0755);

    auto write_file = [](const std::string& path) {
        gchar* contents = g_strdup("payload");
        gboolean ok = g_file_set_contents(path.c_str(), contents, -1, nullptr);
        g_free(contents);
        REQUIRE(ok);
    };
    auto exists = [](const std::string& path) { return g_file_test(path.c_str(), G_FILE_TEST_EXISTS); };

    const std::string src = b + "/f.jpg";

    SECTION("A move leaves the source and creates the destination")
    {
        write_file(src);
        std::list<std::string> uris;
        uris.push_back("file://" + src);
        REQUIRE(QuiverFileOps::TransferFiles(uris, true, ("file://" + other).c_str(), nullptr, nullptr) == 1);
        REQUIRE_FALSE(exists(src));
        REQUIRE(exists(other + "/f.jpg"));
    }

    SECTION("A copy keeps the source")
    {
        write_file(src);
        std::list<std::string> uris;
        uris.push_back("file://" + src);
        REQUIRE(QuiverFileOps::TransferFiles(uris, false, ("file://" + other).c_str(), nullptr, nullptr) == 1);
        REQUIRE(exists(src));
        REQUIRE(exists(other + "/f.jpg"));
    }

    SECTION("An item already in the target folder is a no-op, not a loss")
    {
        write_file(src);
        std::list<std::string> uris;
        uris.push_back("file://" + src);
        /* Moving /a/b/f.jpg onto /a/b resolves to the same path. */
        REQUIRE(QuiverFileOps::TransferFiles(uris, true, ("file://" + b).c_str(), nullptr, nullptr) == 0);
        REQUIRE(exists(src));
    }

    SECTION("A copy onto itself is refused, and the file survives")
    {
        write_file(src);
        std::list<std::string> uris;
        uris.push_back("file://" + src);
        /* Even an "overwrite everything" conflict handler must not turn a
         * file into itself. */
        REQUIRE(QuiverFileOps::TransferFiles(uris, false, ("file://" + b).c_str(), nullptr, nullptr) == 0);
        REQUIRE(exists(src));
    }

    SECTION("A file that lives under the destination is moved out of its folder")
    {
        /* The reported case: /a/b/f.jpg dropped on /a.  Refusing this (the
         * guard used to ask the containment question the wrong way round)
         * made an accepted drop do nothing at all. */
        write_file(src);
        std::list<std::string> uris;
        uris.push_back("file://" + src);
        REQUIRE(QuiverFileOps::TransferFiles(uris, true, ("file://" + a).c_str(), nullptr, nullptr) == 1);
        REQUIRE_FALSE(exists(src));
        REQUIRE(exists(a + "/f.jpg"));
    }

    SECTION("A folder into its own subtree is skipped")
    {
        std::list<std::string> uris;
        uris.push_back("file://" + a);
        REQUIRE(QuiverFileOps::TransferFiles(uris, true, ("file://" + b).c_str(), nullptr, nullptr) == 0);
        REQUIRE_FALSE(exists(b + "/a"));
    }

    SECTION("A folder into itself is skipped")
    {
        std::list<std::string> uris;
        uris.push_back("file://" + a);
        REQUIRE(QuiverFileOps::TransferFiles(uris, true, ("file://" + a).c_str(), nullptr, nullptr) == 0);
        REQUIRE(exists(a));
    }

    SECTION("Missing source files are reported, not moved")
    {
        std::list<std::string> uris;
        uris.push_back("file://" + b + "/missing.jpg");
        REQUIRE(QuiverFileOps::TransferFiles(uris, true, ("file://" + other).c_str(), nullptr, nullptr) == 0);
    }

    SECTION("The destination folder needs no trailing slash from the caller")
    {
        write_file(src);
        std::list<std::string> uris;
        uris.push_back("file://" + src);
        REQUIRE(QuiverFileOps::TransferFiles(uris, true, ("file://" + other).c_str(), nullptr, nullptr) == 1);
        REQUIRE(exists(other + "/f.jpg"));
    }

    remove_tree(base);
}
