#include "QuiverExternalTool.h"

struct _QuiverExternalTool
{
    GObject parent_instance;
    ExternalTool tool;
};

G_DEFINE_TYPE(QuiverExternalTool, quiver_external_tool, G_TYPE_OBJECT)

static void
quiver_external_tool_finalize(GObject *object)
{
    //QuiverExternalTool *self = QUIVER_EXTERNAL_TOOL(object);
    // Nothing to free here, as ExternalTool is a C++ object with its own destructor
    G_OBJECT_CLASS(quiver_external_tool_parent_class)->finalize(object);
}

static void
quiver_external_tool_class_init(QuiverExternalToolClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = quiver_external_tool_finalize;
}

static void
quiver_external_tool_init(QuiverExternalTool *self)
{
}

QuiverExternalTool *
quiver_external_tool_new(const ExternalTool& tool)
{
    QuiverExternalTool *self = (QuiverExternalTool*)g_object_new(QUIVER_TYPE_EXTERNAL_TOOL, NULL);
    self->tool = tool;
    return self;
}

const ExternalTool&
quiver_external_tool_get_tool(QuiverExternalTool* self)
{
    return self->tool;
}
