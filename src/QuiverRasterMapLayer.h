#ifndef FILE_QUIVER_RASTER_MAP_LAYER_H
#define FILE_QUIVER_RASTER_MAP_LAYER_H

#include <shumate/shumate.h>

G_BEGIN_DECLS

ShumateLayer* quiver_raster_map_layer_new(ShumateMapSource* source,
	ShumateViewport* viewport);

G_END_DECLS

#endif // FILE_QUIVER_RASTER_MAP_LAYER_H
