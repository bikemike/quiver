#ifndef FILE_QUIVER_RASTER_TILE_SOURCE_H
#define FILE_QUIVER_RASTER_TILE_SOURCE_H

#include <shumate/shumate.h>

G_BEGIN_DECLS

#define QUIVER_TYPE_RASTER_TILE_SOURCE (quiver_raster_tile_source_get_type())

G_DECLARE_FINAL_TYPE(QuiverRasterTileSource, quiver_raster_tile_source,
	QUIVER, RASTER_TILE_SOURCE, ShumateMapSource)

/* A small HTTP raster tile source used for online providers that are not in
 * Libshumate's built-in registry (e.g. Google Maps).  The URI template may
 * contain {x}, {y} and {z} placeholders. */
ShumateMapSource* quiver_raster_tile_source_new(const char* id,
	const char* name, const char* license, const char* uriFormat,
	guint minZoom, guint maxZoom);

G_END_DECLS

#endif // FILE_QUIVER_RASTER_TILE_SOURCE_H
