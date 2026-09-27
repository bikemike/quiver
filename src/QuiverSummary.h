#ifndef QUIVER_SUMMARY_H
#define QUIVER_SUMMARY_H

// Layout of the property view's Summary tab.
//
// Kept apart from the widget code because the ordering rules are the part
// worth protecting: the properties an image and a video have in common are
// emitted in the same order for both, so those rows line up when the user
// flips between media types.  The properties that only apply to one media
// type follow underneath.  Rows run from most to least useful to the user.

#include <string>
#include <vector>

namespace QuiverSummary
{

// Properties shared by every file type, in display order.
//
// Camera trails the rest: plenty of images and videos were shot on a device
// that records no make or model, so the row is often missing, and the file
// properties below it are the ones that are always there.
inline std::vector<std::string> CommonRows()
{
	return {
		"File Name",
		"Date Taken",
		"Dimensions",
		"GPS Location",
		"File Size",
		"Last Modified",
		"Type",
		"Camera",
	};
}

// Properties only a photo has, in display order.
inline std::vector<std::string> ImageRows()
{
	return {
		"Software",
		"Artist",
		"GPS Altitude",
	};
}

// Properties only a video has, in display order.
inline std::vector<std::string> VideoRows()
{
	return {
		"Duration",
		"Codecs",
		"Container",
	};
}

// Full row order for the given media type: the shared properties, then the
// ones specific to that media type.  The caller skips the properties it has
// no value for.
inline std::vector<std::string> Layout(bool bVideo)
{
	std::vector<std::string> rows = CommonRows();
	const std::vector<std::string>& tail = bVideo ? VideoRows() : ImageRows();
	rows.insert(rows.end(), tail.begin(), tail.end());
	return rows;
}

} // namespace QuiverSummary

#endif // QUIVER_SUMMARY_H
