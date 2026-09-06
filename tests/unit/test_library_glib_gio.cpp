#include <catch2/catch_test_macros.hpp>
#include <glib.h>
#include <gio/gio.h>
#include <vector>
#include <string>

TEST_CASE("GLib GKeyFile Configuration Engine", "[lib][glib][fast]")
{
    GKeyFile* kf = g_key_file_new();
    REQUIRE(kf != nullptr);

    g_key_file_set_string(kf, "Viewer", "Mode", "Fullscreen");
    g_key_file_set_boolean(kf, "Viewer", "AutoRotate", TRUE);
    g_key_file_set_integer(kf, "Viewer", "ZoomStep", 15);

    gint intList[] = {10, 20, 30, 40};
    g_key_file_set_integer_list(kf, "Viewer", "ZoomLevels", intList, 4);

    SECTION("Key and Section existence")
    {
        REQUIRE(g_key_file_has_group(kf, "Viewer") == TRUE);
        REQUIRE(g_key_file_has_group(kf, "Missing") == FALSE);
        REQUIRE(g_key_file_has_key(kf, "Viewer", "Mode", NULL) == TRUE);
        REQUIRE(g_key_file_has_key(kf, "Viewer", "Missing", NULL) == FALSE);
    }

    SECTION("Retrieving typed values")
    {
        gchar* mode = g_key_file_get_string(kf, "Viewer", "Mode", NULL);
        REQUIRE(std::string(mode) == "Fullscreen");
        g_free(mode);

        REQUIRE(g_key_file_get_boolean(kf, "Viewer", "AutoRotate", NULL) == TRUE);
        REQUIRE(g_key_file_get_integer(kf, "Viewer", "ZoomStep", NULL) == 15);

        gsize len = 0;
        gint* retrievedList = g_key_file_get_integer_list(kf, "Viewer", "ZoomLevels", &len, NULL);
        REQUIRE(len == 4);
        REQUIRE(retrievedList[0] == 10);
        REQUIRE(retrievedList[3] == 40);
        g_free(retrievedList);
    }

    SECTION("Buffer serialization and reload")
    {
        gsize dataLen = 0;
        gchar* data = g_key_file_to_data(kf, &dataLen, NULL);
        REQUIRE(data != nullptr);
        REQUIRE(dataLen > 0);

        GKeyFile* reloaded = g_key_file_new();
        gboolean loaded = g_key_file_load_from_data(reloaded, data, dataLen, G_KEY_FILE_NONE, NULL);
        REQUIRE(loaded == TRUE);
        REQUIRE(g_key_file_get_boolean(reloaded, "Viewer", "AutoRotate", NULL) == TRUE);

        g_free(data);
        g_key_file_free(reloaded);
    }

    g_key_file_free(kf);
}

TEST_CASE("GIO GCancellable Token Mechanism", "[lib][gio][fast]")
{
    GCancellable* cancellable = g_cancellable_new();
    REQUIRE(cancellable != nullptr);
    REQUIRE(g_cancellable_is_cancelled(cancellable) == FALSE);

    g_cancellable_cancel(cancellable);
    REQUIRE(g_cancellable_is_cancelled(cancellable) == TRUE);

    g_cancellable_reset(cancellable);
    REQUIRE(g_cancellable_is_cancelled(cancellable) == FALSE);

    g_object_unref(cancellable);
}

TEST_CASE("GIO GSimpleActionGroup Action State", "[lib][gio][fast]")
{
    GSimpleActionGroup* group = g_simple_action_group_new();
    REQUIRE(group != nullptr);

    // Create stateful toggle action
    GSimpleAction* action = g_simple_action_new_stateful("show-sidebar", nullptr, g_variant_new_boolean(FALSE));
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(action));

    GAction* found = g_action_map_lookup_action(G_ACTION_MAP(group), "show-sidebar");
    REQUIRE(found != nullptr);

    GVariant* state = g_action_get_state(found);
    REQUIRE(g_variant_get_boolean(state) == FALSE);
    g_variant_unref(state);

    // Change state
    g_action_change_state(found, g_variant_new_boolean(TRUE));
    state = g_action_get_state(found);
    REQUIRE(g_variant_get_boolean(state) == TRUE);
    g_variant_unref(state);

    g_object_unref(action);
    g_object_unref(group);
}
