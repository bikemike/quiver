#!/usr/bin/env python3
"""Generate data/world_boundaries.bin, the vector fallback used by Quiver's
offline Libshumate tile source.

The default input is Natural Earth 1:50m (public domain):
  * Admin 0 countries, outer rings only, used for land fill and country borders
  * Admin 1 states/provinces lines, used for province/state borders

Format (little endian):
    char    magic[4]        "QWVB"
    uint16  version         1
    uint16  reserved
    uint32  feature_count
    repeated features:
        uint8   type        0 = land/country polygon, 1 = country border line,
                            2 = state/province border line
        uint8   reserved[3]
        uint32  point_count
        int32   lat_e6       latitude  * 1e6
        int32   lon_e6       longitude * 1e6

GeoJSON coordinates are [longitude, latitude]; the file stores latitude first,
matching the renderer's internal WorldFeature points.
"""

import argparse
import json
import os
import struct
import urllib.request

LAND = 0
BORDER = 1
ADMIN1 = 2

DEFAULT_SOURCES = {
    "countries": "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/"
                 "master/geojson/ne_50m_admin_0_countries.geojson",
    "admin1": "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/"
              "master/geojson/ne_50m_admin_1_states_provinces_lines.geojson",
}


def load_geojson(url, source_dir):
    if source_dir:
        path = os.path.join(source_dir, os.path.basename(url))
        with open(path, "rb") as f:
            return json.load(f)
    with urllib.request.urlopen(url, timeout=60) as response:
        return json.loads(response.read().decode("utf-8"))


def clean_points(points, minimum):
    cleaned = []
    for lon, lat in points:
        # GeoJSON may carry a closing point; cairo_close_path() handles it.
        if cleaned and abs(cleaned[-1][0] - lon) < 1e-7 and \
                abs(cleaned[-1][1] - lat) < 1e-7:
            continue
        cleaned.append((float(lon), float(lat)))
    if len(cleaned) > 1 and abs(cleaned[0][0] - cleaned[-1][0]) < 1e-7 and \
            abs(cleaned[0][1] - cleaned[-1][1]) < 1e-7:
        cleaned.pop()
    return cleaned if len(cleaned) >= minimum else []


def polygon_outer_rings(geometry):
    kind = geometry.get("type")
    coords = geometry.get("coordinates") or []
    if kind == "Polygon":
        polygons = [coords]
    elif kind == "MultiPolygon":
        polygons = coords
    else:
        return
    for polygon in polygons:
        if not polygon:
            continue
        points = clean_points(polygon[0], 3)
        if points:
            yield points


def line_parts(geometry):
    kind = geometry.get("type")
    coords = geometry.get("coordinates") or []
    if kind == "LineString":
        lines = [coords]
    elif kind == "MultiLineString":
        lines = coords
    else:
        return
    for line in lines:
        points = clean_points(line, 2)
        if points:
            yield points


def collect_features(countries, admin1):
    features = []
    for feature in countries.get("features", []):
        for ring in polygon_outer_rings(feature.get("geometry") or {}):
            features.append((LAND, ring))
    for feature in admin1.get("features", []):
        for line in line_parts(feature.get("geometry") or {}):
            features.append((ADMIN1, line))
    return features


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir",
                        help="directory containing the Natural Earth GeoJSON files")
    parser.add_argument("--output", default=os.path.join(
        os.path.dirname(here), "data", "world_boundaries.bin"))
    args = parser.parse_args()

    countries = load_geojson(DEFAULT_SOURCES["countries"], args.source_dir)
    admin1 = load_geojson(DEFAULT_SOURCES["admin1"], args.source_dir)
    features = collect_features(countries, admin1)

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(b"QWVB")
        f.write(struct.pack("<HHI", 1, 0, len(features)))
        for kind, points in features:
            f.write(struct.pack("<BBBB", kind, 0, 0, 0))
            f.write(struct.pack("<I", len(points)))
            for lon, lat in points:
                f.write(struct.pack("<ii",
                                    int(round(lat * 1_000_000)),
                                    int(round(lon * 1_000_000))))

    print("wrote %s (%d bytes, %d features)" %
          (args.output, os.path.getsize(args.output), len(features)))


if __name__ == "__main__":
    main()
