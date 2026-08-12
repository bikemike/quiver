#include <glib.h>

static GPrivate gui_thread_key = G_PRIVATE_INIT(NULL);

namespace ThreadUtil
{
void Init()
{
	static bool initialized = false;
	if (!initialized)
	{
		initialized = true;
		g_private_set(&gui_thread_key, GUINT_TO_POINTER(1));
	}
}

bool IsGUIThread()
{
	return 1 == GPOINTER_TO_UINT(g_private_get(&gui_thread_key));
}
}
