#ifndef QUIVER_EXTERNAL_TOOL_H
#define QUIVER_EXTERNAL_TOOL_H

#include <glib-object.h>
#include "ExternalTools.h"

G_BEGIN_DECLS

#define QUIVER_TYPE_EXTERNAL_TOOL (quiver_external_tool_get_type())
G_DECLARE_FINAL_TYPE(QuiverExternalTool, quiver_external_tool, QUIVER, EXTERNAL_TOOL, GObject)

QuiverExternalTool *quiver_external_tool_new(const ExternalTool& tool);

const ExternalTool& quiver_external_tool_get_tool(QuiverExternalTool* self);

G_END_DECLS

#endif /* QUIVER_EXTERNAL_TOOL_H */
