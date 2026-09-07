#include "ShortcutManager.h"
#include "Preferences.h"
#include <algorithm>
#include <cstring>

extern GtkApplication *g_pApp;

static const guint MODIFIER_MASK = GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK | GDK_SUPER_MASK | GDK_META_MASK;

static std::string normalize_accel(const std::string &accel)
{
    guint keyval = 0;
    GdkModifierType mods = (GdkModifierType)0;
    gtk_accelerator_parse(accel.c_str(), &keyval, &mods);
    if (keyval == 0) return "";
    gchar *name = gtk_accelerator_name(keyval, (GdkModifierType)(mods & MODIFIER_MASK));
    std::string result = name ? name : "";
    g_free(name);
    return result;
}

ShortcutManager& ShortcutManager::GetInstance()
{
    static ShortcutManager instance;
    return instance;
}

ShortcutManager::ShortcutManager()
{
}

void ShortcutManager::Init()
{
    if (m_bInitialized) return;
    RegisterDefaultActions();
    LoadFromPreferences();
    m_bInitialized = true;
    ApplyShortcuts();
}

void ShortcutManager::RegisterDefaultActions()
{
    m_actions.clear();

    // Viewer Navigation
    m_actions.push_back({
        "ImageNext", "Viewer Navigation", "Next File",
        "Navigate to the next file in the folder",
        {"Right", "Page_Down"}, {"Right", "Page_Down"}, true
    });
    m_actions.push_back({
        "ImagePrevious", "Viewer Navigation", "Previous File",
        "Navigate to the previous file in the folder",
        {"Left", "Page_Up"}, {"Left", "Page_Up"}, true
    });
    m_actions.push_back({
        "ImageFirst", "Viewer Navigation", "First File",
        "Jump to the first file in the folder",
        {"Home"}, {"Home"}, true
    });
    m_actions.push_back({
        "ImageLast", "Viewer Navigation", "Last File",
        "Jump to the last file in the folder",
        {"End"}, {"End"}, true
    });

    // Video Playback
    m_actions.push_back({
        "VideoPlay", "Video Playback", "Play / Pause",
        "Toggle video playback",
        {"space", "k", "P", "<Control>space"}, {"space", "k", "P", "<Control>space"}, true
    });
    m_actions.push_back({
        "VideoSeekBack5", "Video Playback", "Rewind 5s",
        "Rewind video by 5 seconds",
        {"comma"}, {"comma"}, true
    });
    m_actions.push_back({
        "VideoSeekFwd5", "Video Playback", "Fast Forward 5s",
        "Fast-forward video by 5 seconds",
        {"period"}, {"period"}, true
    });
    m_actions.push_back({
        "VideoSkipBack", "Video Playback", "Rewind 10s",
        "Rewind video by 10 seconds",
        {"j"}, {"j"}, true
    });
    m_actions.push_back({
        "VideoSkipForward", "Video Playback", "Fast Forward 10s",
        "Fast-forward video by 10 seconds",
        {"l"}, {"l"}, true
    });
    m_actions.push_back({
        "VideoFrameBack", "Video Playback", "Frame Step Back",
        "Step backward 1 video frame",
        {"<Shift>less", "<Shift>j"}, {"<Shift>less", "<Shift>j"}, true
    });
    m_actions.push_back({
        "VideoFrameFwd", "Video Playback", "Frame Step Forward",
        "Step forward 1 video frame",
        {"<Shift>greater", "<Shift>l"}, {"<Shift>greater", "<Shift>l"}, true
    });
    m_actions.push_back({
        "VideoSnapshot", "Video Playback", "Take Snapshot",
        "Save current video frame to an image file",
        {}, {}, true
    });

    // Image Manipulation
    m_actions.push_back({
        "RotateCW", "Image Manipulation", "Rotate Clockwise",
        "Rotate image 90 degrees clockwise",
        {"r", "bracketright"}, {"r", "bracketright"}, true
    });
    m_actions.push_back({
        "RotateCCW", "Image Manipulation", "Rotate Counter-Clockwise",
        "Rotate image 90 degrees counter-clockwise",
        {"<Shift>r", "bracketleft"}, {"<Shift>r", "bracketleft"}, true
    });
    m_actions.push_back({
        "FlipH", "Image Manipulation", "Flip Horizontally",
        "Mirror image horizontally",
        {"h"}, {"h"}, true
    });
    m_actions.push_back({
        "FlipV", "Image Manipulation", "Flip Vertically",
        "Mirror image vertically",
        {"v"}, {"v"}, true
    });
    m_actions.push_back({
        "ZoomIn", "Image Manipulation", "Zoom In",
        "Increase image magnification",
        {"equal", "plus"}, {"equal", "plus"}, true
    });
    m_actions.push_back({
        "ZoomOut", "Image Manipulation", "Zoom Out",
        "Decrease image magnification",
        {"minus"}, {"minus"}, true
    });
    m_actions.push_back({
        "ZoomFit", "Image Manipulation", "Fit to Window",
        "Scale image to fit within the window",
        {}, {}, true
    });
    m_actions.push_back({
        "ZoomFitStretch", "Image Manipulation", "Fit to Window (Stretch)",
        "Scale image to fit window, enlarging smaller images",
        {}, {}, true
    });
    m_actions.push_back({
        "Zoom100", "Image Manipulation", "Actual Size (100%)",
        "View image at 1:1 original pixel scale",
        {}, {}, true
    });
    m_actions.push_back({
        "ZoomFillScreen", "Image Manipulation", "Fill Screen",
        "Scale image to fill entire screen area",
        {}, {}, true
    });

    // Viewer Display
    m_actions.push_back({
        "FullScreen", "Viewer Display", "Full Screen",
        "Toggle full screen mode",
        {"F11", "f"}, {"F11", "f"}, false
    });
    m_actions.push_back({
        "ViewFilmStrip", "Viewer Display", "Toggle Filmstrip",
        "Show or hide thumbnail filmstrip",
        {}, {}, true
    });
    m_actions.push_back({
        "SlideShow", "Viewer Display", "Start Slideshow",
        "Start or pause slideshow presentation",
        {"s"}, {"s"}, true
    });

    // File & Window
    m_actions.push_back({
        "FileOpen", "File & Window", "Open File",
        "Open an image file with file chooser",
        {"<Control>o"}, {"<Control>o"}, false
    });
    m_actions.push_back({
        "FileOpenFolder", "File & Window", "Open Folder",
        "Open a folder in the browser",
        {"<Control>f"}, {"<Control>f"}, false
    });
    m_actions.push_back({
        "Save", "File & Window", "Save",
        "Save modifications to current image",
        {"<Control>s"}, {"<Control>s"}, false
    });
    m_actions.push_back({
        "SaveAs", "File & Window", "Save As",
        "Save a copy of the image under a new name",
        {}, {}, false
    });
    m_actions.push_back({
        "Close", "File & Window", "Close / Quit",
        "Close viewer or exit application",
        {"<Control>q", "q", "<Control>w", "Escape"}, {"<Control>q", "q", "<Control>w", "Escape"}, false
    });
    m_actions.push_back({
        "Preferences", "File & Window", "Preferences",
        "Open Preferences dialog",
        {"<Control>p"}, {"<Control>p"}, false
    });

    // Browser
    m_actions.push_back({
        "BrowserOpenLocation", "Browser", "Open Location Bar",
        "Focus folder path text entry",
        {"<Control>l"}, {"<Control>l"}, false
    });
    m_actions.push_back({
        "BrowserHistoryBack", "Browser", "History Back",
        "Navigate to previous folder in history",
        {"<Alt>Left"}, {"<Alt>Left"}, false
    });
    m_actions.push_back({
        "BrowserHistoryForward", "Browser", "History Forward",
        "Navigate to next folder in history",
        {"<Alt>Right"}, {"<Alt>Right"}, false
    });
    m_actions.push_back({
        "GoFolderParent", "Browser", "Parent Folder",
        "Navigate to parent directory",
        {"<Alt>Up"}, {"<Alt>Up"}, false
    });
    m_actions.push_back({
        "BrowserSelectAll", "Browser", "Select All",
        "Select all items in browser",
        {"<Control>a"}, {"<Control>a"}, false
    });
    m_actions.push_back({
        "BrowserTrash", "Browser", "Move to Trash",
        "Move selected file(s) to trash",
        {"Delete"}, {"Delete"}, false
    });
    m_actions.push_back({
        "BrowserReload", "Browser", "Reload",
        "Refresh folder contents",
        {"<Control>r"}, {"<Control>r"}, false
    });
}

const ShortcutActionDef* ShortcutManager::GetAction(const std::string &action_name) const
{
    for (const auto &def : m_actions) {
        if (def.action_name == action_name) {
            return &def;
        }
    }
    return nullptr;
}

ShortcutActionDef* ShortcutManager::GetActionMutable(const std::string &action_name)
{
    for (auto &def : m_actions) {
        if (def.action_name == action_name) {
            return &def;
        }
    }
    return nullptr;
}

bool ShortcutManager::SetAccelerators(const std::string &action_name, const std::vector<std::string> &accels)
{
    ShortcutActionDef *def = GetActionMutable(action_name);
    if (!def) return false;

    std::vector<std::string> normalized;
    for (const auto &a : accels) {
        std::string n = normalize_accel(a);
        if (!n.empty() && std::find(normalized.begin(), normalized.end(), n) == normalized.end()) {
            normalized.push_back(n);
        }
    }
    def->current_accels = normalized;
    SaveToPreferences();
    ApplyShortcutsForAction(action_name);
    return true;
}

bool ShortcutManager::AddAccelerator(const std::string &action_name, const std::string &accel)
{
    ShortcutActionDef *def = GetActionMutable(action_name);
    if (!def) return false;

    std::string n = normalize_accel(accel);
    if (n.empty()) return false;

    if (std::find(def->current_accels.begin(), def->current_accels.end(), n) == def->current_accels.end()) {
        def->current_accels.push_back(n);
        SaveToPreferences();
        ApplyShortcutsForAction(action_name);
        return true;
    }
    return false;
}

bool ShortcutManager::RemoveAccelerator(const std::string &action_name, const std::string &accel)
{
    ShortcutActionDef *def = GetActionMutable(action_name);
    if (!def) return false;

    std::string n = normalize_accel(accel);
    auto it = std::find(def->current_accels.begin(), def->current_accels.end(), n.empty() ? accel : n);
    if (it != def->current_accels.end()) {
        def->current_accels.erase(it);
        SaveToPreferences();
        ApplyShortcutsForAction(action_name);
        return true;
    }
    return false;
}

void ShortcutManager::ResetToDefault(const std::string &action_name)
{
    ShortcutActionDef *def = GetActionMutable(action_name);
    if (!def) return;
    def->current_accels = def->default_accels;
    SaveToPreferences();
    ApplyShortcutsForAction(action_name);
}

void ShortcutManager::ResetAllToDefaults()
{
    for (auto &def : m_actions) {
        def.current_accels = def.default_accels;
    }
    SaveToPreferences();
    ApplyShortcuts();
}

std::string ShortcutManager::FindConflictingAction(const std::string &accel, const std::string &exclude_action) const
{
    std::string target = normalize_accel(accel);
    if (target.empty()) return "";

    for (const auto &def : m_actions) {
        if (def.action_name == exclude_action) continue;
        for (const auto &a : def.current_accels) {
            std::string na = normalize_accel(a);
            if (na == target) {
                return def.label;
            }
            if ((target == "<Shift>less" || target == "less" || target == "<Shift>comma") &&
                (na == "<Shift>less" || na == "less" || na == "<Shift>comma")) {
                return def.label;
            }
            if ((target == "<Shift>greater" || target == "greater" || target == "<Shift>period") &&
                (na == "<Shift>greater" || na == "greater" || na == "<Shift>period")) {
                return def.label;
            }
        }
    }
    return "";
}

bool ShortcutManager::IsUnmodifiedAccel(const std::string &accel)
{
    guint keyval = 0;
    GdkModifierType mods = (GdkModifierType)0;
    gtk_accelerator_parse(accel.c_str(), &keyval, &mods);
    if (keyval == 0) return false;
    guint mask = GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK;
    return (mods & mask) == 0;
}

void ShortcutManager::ApplyShortcuts()
{
    if (!g_pApp) return;
    for (const auto &def : m_actions) {
        ApplyShortcutsForAction(def.action_name);
    }
}

void ShortcutManager::ApplyShortcutsForAction(const std::string &action_name)
{
    if (!g_pApp) return;
    const ShortcutActionDef *def = GetAction(action_name);
    if (!def) return;

    gchar *detailed_name = g_strdup_printf("quiver.%s", def->action_name.c_str());

    // If in browser mode and action is viewer-only, disable accelerators
    if (!m_bInViewerMode && def->is_viewer_only) {
        const gchar *empty[] = {NULL};
        gtk_application_set_accels_for_action(g_pApp, detailed_name, empty);
        g_free(detailed_name);
        return;
    }

    std::vector<std::string> expanded;
    for (const auto &accel : def->current_accels) {
        if (m_bUnmodifiedSuppressed && IsUnmodifiedAccel(accel)) {
            continue;
        }
        expanded.push_back(accel);
        if (accel == "<Shift>less") {
            expanded.push_back("less");
            expanded.push_back("<Shift>comma");
        } else if (accel == "less") {
            expanded.push_back("<Shift>less");
        } else if (accel == "<Shift>comma") {
            expanded.push_back("<Shift>less");
            expanded.push_back("less");
        } else if (accel == "<Shift>greater") {
            expanded.push_back("greater");
            expanded.push_back("<Shift>period");
        } else if (accel == "greater") {
            expanded.push_back("<Shift>greater");
        } else if (accel == "<Shift>period") {
            expanded.push_back("<Shift>greater");
            expanded.push_back("greater");
        } else if (accel == "<Shift>j") {
            expanded.push_back("J");
        } else if (accel == "<Shift>l") {
            expanded.push_back("L");
        }
    }

    std::vector<const gchar*> accels_ptrs;
    for (const auto &s : expanded) {
        accels_ptrs.push_back(s.c_str());
    }
    accels_ptrs.push_back(NULL);

    gtk_application_set_accels_for_action(g_pApp, detailed_name, accels_ptrs.data());
    g_free(detailed_name);
}

void ShortcutManager::SetViewerMode(bool in_viewer)
{
    if (m_bInViewerMode == in_viewer) return;
    m_bInViewerMode = in_viewer;
    ApplyShortcuts();
}

void ShortcutManager::SuppressUnmodifiedAccelerators(bool suppress)
{
    if (m_bUnmodifiedSuppressed == suppress) return;
    m_bUnmodifiedSuppressed = suppress;
    ApplyShortcuts();
}

void ShortcutManager::LoadFromPreferences()
{
    PreferencesPtr prefs = Preferences::GetInstance();
    if (!prefs) return;

    for (auto &def : m_actions) {
        if (prefs->HasKey("shortcuts", def.action_name)) {
            std::list<std::string> saved_list = prefs->GetStringList("shortcuts", def.action_name);
            std::vector<std::string> loaded;
            for (const auto &s : saved_list) {
                std::string norm = normalize_accel(s);
                if (norm == "<Shift>comma") norm = "<Shift>less";
                else if (norm == "<Shift>period") norm = "<Shift>greater";
                if (!norm.empty()) {
                    loaded.push_back(norm);
                }
            }
            def.current_accels = loaded;
        } else {
            def.current_accels = def.default_accels;
        }
    }
}

void ShortcutManager::SaveToPreferences()
{
    PreferencesPtr prefs = Preferences::GetInstance();
    if (!prefs) return;

    for (const auto &def : m_actions) {
        // If current accels equal default accels, remove key to keep config clean
        if (def.current_accels == def.default_accels) {
            if (prefs->HasKey("shortcuts", def.action_name)) {
                prefs->RemoveKey("shortcuts", def.action_name);
            }
        } else {
            std::list<std::string> save_list(def.current_accels.begin(), def.current_accels.end());
            prefs->SetStringList("shortcuts", def.action_name, save_list);
        }
    }
}

std::string ShortcutManager::KeyvalAndModsToAccelString(guint keyval, GdkModifierType mods)
{
    if (keyval == 0) return "";
    gchar *name = gtk_accelerator_name(keyval, (GdkModifierType)(mods & MODIFIER_MASK));
    std::string result = name ? name : "";
    g_free(name);
    return result;
}

std::string ShortcutManager::AccelStringToHumanLabel(const std::string &accel)
{
    guint keyval = 0;
    GdkModifierType mods = (GdkModifierType)0;
    gtk_accelerator_parse(accel.c_str(), &keyval, &mods);
    if (keyval == 0) return accel;

    gchar *label = gtk_accelerator_get_label(keyval, mods);
    std::string result = label ? label : accel;
    g_free(label);
    return result;
}
