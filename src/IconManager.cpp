#include "IconManager.h"

IconManager* IconManager::m_pInstance = NULL;

IconManager* IconManager::GetInstance()
{
    if (!m_pInstance)
    {
        m_pInstance = new IconManager();
    }
    return m_pInstance;
}

IconManager::IconManager()
{
    // Load all the icons here
    const char* icon_names[] = {
        "document-new",
        "folder",
        "edit-redo",
        "edit-undo",
        "help-contents",
        "help-about",
        "object-select-symbolic",
        "window-close",
        "document-open",
        "document-save",
        "application-exit",
        "edit-edit",
        "document-properties",
        "preferences-system",
        "view-refresh",
        "list-add",
        "list-remove",
        "system-run",
        "edit-delete",
        "go-up",
        "go-down",
        "go-first",
        "go-last",
        "go-previous",
        "go-next",
        "zoom-in",
        "zoom-out",
        "zoom-fit-best",
        "zoom-original",
        "view-fullscreen",
        "view-restore",
        "edit-cut",
        "edit-copy",
        "edit-paste",
        "go-top",
        "view-sort-descending",
        "dialog-information",
        "dialog-warning",
        "dialog-error",
        NULL
    };

    for (int i = 0; icon_names[i] != NULL; ++i)
    {
        GtkIconTheme* theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
        GdkPaintable* paintable = GDK_PAINTABLE(gtk_icon_theme_lookup_icon(theme, icon_names[i], NULL, 24, 1, GTK_TEXT_DIR_NONE, (GtkIconLookupFlags)0));

        if (paintable)
        {
            m_iconMap[icon_names[i]] = paintable;
        }
    }
}

IconManager::~IconManager()
{
    for (auto const& [key, val] : m_iconMap)
    {
        g_object_unref(val);
    }
}

GdkPaintable* IconManager::GetIcon(const std::string& name)
{
    auto it = m_iconMap.find(name);
    if (it != m_iconMap.end())
    {
        return it->second;
    }
    return NULL;
}
