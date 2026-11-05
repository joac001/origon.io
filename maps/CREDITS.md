# Map credits

## Plaza de Mayo (`plaza_de_mayo.json`)

Building footprints, heights, streets, the plaza layout and street furniture positions come from
OpenStreetMap: © OpenStreetMap contributors, available under the Open Database License (ODbL),
https://www.openstreetmap.org/copyright.

The geometry is generated from that data:

    python3 tools/mapgen/plaza_layout.py --fetch      # OSM extract -> .cache/osm/plaza_layout.json
    blender --background --factory-startup --python tools/blender/gen_plaza.py

The second step writes the scenery chunks to `assets/models/maps/plaza_de_mayo/` (committed, so the
map works out of the box) and rewrites `maps/plaza_de_mayo.json`. Landmark facades (Casa Rosada,
Cabildo, Catedral, Pirámide de Mayo, Belgrano monument) are simplified by hand.
