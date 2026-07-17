export class MapManager {
  constructor(containerId) {
    this.map = L.map(containerId).setView([51.505, -0.09], 13);

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
    this.appMode = "dashboard";
  }

  setMode(mode) {
    this.appMode = mode;
    this._updateVisibility();
  }

  _updateVisibility() {
    for (const m of this.markers.values()) {
      if (!m.isSimulated && this.appMode === "lab") {
        m.layer.remove();
      } else if (!this.map.hasLayer(m.layer)) {
        m.layer.addTo(this.map);
      }
    }
    for (const r of this.routes.values()) {
      if (!r.isSimulated && this.appMode === "lab") {
        r.layer.remove();
      } else if (!this.map.hasLayer(r.layer)) {
        r.layer.addTo(this.map);
      }
    }
  }

  async setLayer(type, fileHandleOrFile) {
    this.map.removeLayer(this.currentLayer);

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
      const p = new pmtiles.PMTiles(source);
      this.currentLayer = pmtiles.leafletLayer(p);
    } else if (type === "osm") {
      this.currentLayer = L.tileLayer(
        "https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",
        {
          maxZoom: 19,
          attribution: "© OpenStreetMap",
        },
      );
    } else {
      this.currentLayer = new this.gridLayer();
    }

    this.currentLayer.addTo(this.map);
  }

  updateTracker(hash, point, name, isSimulated = false) {
    if (!this.markers.has(hash)) {
      const marker = L.circleMarker([point.latitude, point.longitude], {
        radius: 8,
        color: isSimulated ? "#ff7800" : "#3388ff",
        fillColor: isSimulated ? "#ff7800" : "#3388ff",
        fillOpacity: 0.8,
      }).addTo(this.map);
      marker.bindTooltip(name);
      this.markers.set(hash, { layer: marker, isSimulated });
    } else {
      this.markers.get(hash).layer.setLatLng([point.latitude, point.longitude]);
    }
    this._updateVisibility();
  }

  drawRoute(hash, points, isSimulated = false) {
    if (this.routes.has(hash)) {
      this.map.removeLayer(this.routes.get(hash).layer);
    }
    if (points.length === 0) return;

    const latlngs = points.map((p) => [p.latitude, p.longitude]);
    const route = L.polyline(latlngs, {
      color: isSimulated ? "#ff7800" : "#3388ff",
      dashArray: isSimulated ? "5, 5" : null,
      weight: 3,
    });
    this.routes.set(hash, { layer: route, isSimulated });
    this._updateVisibility();
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

  clearSimulated() {
    for (const [hash, m] of this.markers.entries()) {
      if (m.isSimulated) {
        m.layer.remove();
        this.markers.delete(hash);
      }
    }
    for (const [hash, r] of this.routes.entries()) {
      if (r.isSimulated) {
        r.layer.remove();
        this.routes.delete(hash);
      }
    }
  }
}
