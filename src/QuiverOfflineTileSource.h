#ifndef FILE_QUIVER_OFFLINE_TILE_SOURCE_H
#define FILE_QUIVER_OFFLINE_TILE_SOURCE_H

#include <glib-object.h>
#include <shumate/shumate.h>

G_BEGIN_DECLS

#define QUIVER_TYPE_OFFLINE_TILE_SOURCE (quiver_offline_tile_source_get_type())
G_DECLARE_FINAL_TYPE(QuiverOfflineTileSource, quiver_offline_tile_source,
	QUIVER, OFFLINE_TILE_SOURCE, ShumateMapSource)

/* Creates a map source that renders the bundled data/world_boundaries.bin
 * with Cairo for every requested (z, x, y) tile.  Used when no network is
 * available so the map is never blank. */
ShumateMapSource* quiver_offline_tile_source_new(void);

G_END_DECLS

#endif // FILE_QUIVER_OFFLINE_TILE_SOURCE_H
