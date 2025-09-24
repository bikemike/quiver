#ifndef ICON_MANAGER_H
#define ICON_MANAGER_H

#include <gtk/gtk.h>
#include <string>
#include <map>

class IconManager
{
public:
    static IconManager* GetInstance();
    GdkPaintable* GetIcon(const std::string& name);

private:
    IconManager();
    ~IconManager();

    static IconManager* m_pInstance;
    std::map<std::string, GdkPaintable*> m_iconMap;
};

#endif
