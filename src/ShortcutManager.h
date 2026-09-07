#ifndef FILE_SHORTCUT_MANAGER_H
#define FILE_SHORTCUT_MANAGER_H

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <memory>

struct ShortcutActionDef {
    std::string action_name;                   // e.g. "ImageNext", "VideoPlay"
    std::string category;                      // e.g. "Viewer Navigation", "Video Playback"
    std::string label;                         // e.g. "Next File"
    std::string description;                   // Brief description
    std::vector<std::string> default_accels;   // Default accelerators in GTK syntax
    std::vector<std::string> current_accels;   // Active accelerators
    bool is_viewer_only = false;               // If true, suppressed in Browser mode
};

class ShortcutManager {
public:
    static ShortcutManager& GetInstance();

    void Init();

    const std::vector<ShortcutActionDef>& GetActions() const { return m_actions; }
    const ShortcutActionDef* GetAction(const std::string &action_name) const;
    ShortcutActionDef* GetActionMutable(const std::string &action_name);

    bool SetAccelerators(const std::string &action_name, const std::vector<std::string> &accels);
    bool AddAccelerator(const std::string &action_name, const std::string &accel);
    bool RemoveAccelerator(const std::string &action_name, const std::string &accel);
    void ResetToDefault(const std::string &action_name);
    void ResetAllToDefaults();

    std::string FindConflictingAction(const std::string &accel, const std::string &exclude_action = "") const;

    void ApplyShortcuts();
    void ApplyShortcutsForAction(const std::string &action_name);

    void SetViewerMode(bool in_viewer);
    void SuppressUnmodifiedAccelerators(bool suppress);
    bool AreUnmodifiedSuppressed() const { return m_bUnmodifiedSuppressed; }

    void LoadFromPreferences();
    void SaveToPreferences();

    static std::string KeyvalAndModsToAccelString(guint keyval, GdkModifierType mods);
    static std::string AccelStringToHumanLabel(const std::string &accel);
    static bool IsUnmodifiedAccel(const std::string &accel);

private:
    ShortcutManager();
    ~ShortcutManager() = default;
    ShortcutManager(const ShortcutManager&) = delete;
    ShortcutManager& operator=(const ShortcutManager&) = delete;

    void RegisterDefaultActions();

    std::vector<ShortcutActionDef> m_actions;
    bool m_bInViewerMode = false;
    bool m_bUnmodifiedSuppressed = false;
    bool m_bInitialized = false;
};

#endif // FILE_SHORTCUT_MANAGER_H
