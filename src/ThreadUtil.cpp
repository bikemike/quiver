#include <glib.h>

static GStaticPrivate gui_thread_key = G_STATIC_PRIVATE_INIT;

namespace ThreadUtil
{
void Init()
{
	static bool initialized = false;
	if (!initialized)
	{
		initialized = true;
		g_static_private_set(&gui_thread_key, GUINT_TO_POINTER(1),NULL);
	}
}

bool IsGUIThread()
{
	return 1 == GPOINTER_TO_UINT(g_static_private_get(&gui_thread_key));
}
}
