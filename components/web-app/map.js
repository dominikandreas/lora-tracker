export class MapManager {
  constructor(containerId) {
    this.map = L.map(containerId).setView([51.1657, 10.4515], 6);

    // Grid (Offline) layer
    this.gridLayer = L.GridLayer.extend({
      createTile: function (coords) {
        const tile = document.createElement("div");
        tile.style.outline = "1px solid #333";
        tile.style.backgroundColor = "#1e1e1e";
        tile.style.color = "#555";
        tile.style.display = "flex";
        tile.style.alignItems = "center";
        tile.style.justifyContent = "center";
        tile.style.fontSize = "12px";
        tile.innerHTML = `${coords.z}/${coords.x}/${coords.y}`;
        return tile;
      },
    });
    this.currentLayer = new this.gridLayer().addTo(this.map);

    this.markers = new Map();
    this.routes = new Map();
  }

  async setLayer(type, fileHandleOrFile) {
    let nextLayer;

    if (type === "pmtiles" && fileHandleOrFile) {
      // Implement PMTiles source wrapper
      const file =
        fileHandleOrFile instanceof File
          ? fileHandleOrFile
          : await fileHandleOrFile.getFile();
      const source = {
        getKey: () => file.name,
        getBytes: async (offset, length) => {
          const slice = file.slice(offset, offset + length);
          return { data: await slice.arrayBuffer() };
        },
      };
      const archive = new pmtiles.PMTiles(source);
      const header = await archive.getHeader();
      if (header.tileType === pmtiles.TileType.Mvt) {
        throw new Error(
          "Vector PMTiles are not supported; import a raster archive",
        );
      }
      nextLayer = pmtiles.leafletRasterLayer(archive, {
        attribution: "Offline PMTiles archive",
      });
    } else if (type === "osm") {
      nextLayer = L.tileLayer(
        "https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",
        {
          maxZoom: 19,
          attribution: "© OpenStreetMap contributors",
        },
      );
    } else {
      nextLayer = new this.gridLayer();
    }

    this.map.removeLayer(this.currentLayer);
    this.currentLayer = nextLayer;
    this.currentLayer.addTo(this.map);
  }

  updateTracker(hash, point, name) {
    if (!this.markers.has(hash)) {
      const marker = L.circleMarker([point.latitude, point.longitude], {
        radius: 8,
        color: "#3388ff",
        fillColor: "#3388ff",
        fillOpacity: 0.8,
      }).addTo(this.map);
      marker.bindTooltip(name);
      this.markers.set(hash, { layer: marker });
    } else {
      this.markers.get(hash).layer.setLatLng([point.latitude, point.longitude]);
    }
  }

  drawRoute(hash, points) {
    if (this.routes.has(hash)) {
      this.map.removeLayer(this.routes.get(hash).layer);
    }
    if (points.length === 0) return;

    const latlngs = points.map((p) => [p.latitude, p.longitude]);
    const route = L.polyline(latlngs, {
      color: "#3388ff",
      weight: 3,
    });
    this.routes.set(hash, { layer: route });
    return route.getBounds();
  }

  fitBounds(bounds) {
    if (bounds && bounds.isValid()) {
      this.map.fitBounds(bounds, { padding: [50, 50] });
    }
  }

  clearLocal() {
    this.markers.forEach((m) => m.layer.remove());
    this.routes.forEach((r) => r.layer.remove());
    this.markers.clear();
    this.routes.clear();
  }

}
