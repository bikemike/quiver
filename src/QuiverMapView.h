#ifndef FILE_QUIVER_MAP_VIEW_H
#define FILE_QUIVER_MAP_VIEW_H

#include <functional>
#include <memory>
#include <string>

#include <gtk/gtk.h>

#include "IImageListView.h"

// Map mode: a Libshumate map with clustered photo markers plus the controls
// for filtering the active photo list down to a geographic rectangle.
//
// The view does not know anything about the Browser/Viewer; the owning
// QuiverImpl wires the callbacks.  All GTK work happens on the main thread;
// the GPS scan runs on a worker thread and is applied via an idle callback.
class QuiverMapView
{
public:
	typedef std::function<void(const std::string&)> UriCallback;
	// top-latitude, left-longitude, bottom-latitude, right-longitude
	typedef std::function<void(double, double, double, double)> RegionCallback;
	typedef std::function<void(std::size_t, std::size_t)> MetadataProgressCallback;
	typedef std::function<void(bool)> MetadataReadyCallback;

	QuiverMapView();
	~QuiverMapView();

	GtkWidget* GetWidget() const;

	void SetImageList(IImageListViewPtr pList);
	IImageListViewPtr GetImageList() const;

	// Called while the background GPS/date scan runs.  total == 0 means the
	// previous scan was superseded and callers should clear stale progress;
	// the replacement reports its own (done, total) batches afterwards.
	void SetMetadataProgressCallback(MetadataProgressCallback cb);
	void SetMetadataReadyCallback(MetadataReadyCallback cb);

	// Rebuild the GPS index / marker clusters from the current list.
	void Refresh();

	// Single click on a marker (the photo should be highlighted) and double
	// click (open it in the viewer).
	void SetSelectCallback(UriCallback cb);
	void SetActivateCallback(UriCallback cb);

	// Called after the user pans or zooms the map; the rectangle is
	// top-latitude, left-longitude, bottom-latitude, right-longitude.
	void SetRegionFilterCallback(RegionCallback cb);

	bool HasActiveRegionFilter() const;
	void ClearRegionFilterVisual();

	// Centre the map on the given item when it is present in the index.
	void HighlightURI(const std::string& uri);

	// Zoom one level, anchored under the pointer when it is over the map and
	// at the map centre otherwise.
	void ZoomIn();
	void ZoomOut();
	// Reset the viewport to the whole world without repeating it.
	void ZoomToWorld();
	void ResetViewForImageList();
	void GrabFocus();

private:
	class Impl;
	std::shared_ptr<Impl> m_Impl;
};

#endif // FILE_QUIVER_MAP_VIEW_H
