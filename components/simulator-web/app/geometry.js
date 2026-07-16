const EARTH_RADIUS_M = 6_371_008.8;

export function isPolygonObstacle(obstacle) {
  return Array.isArray(obstacle.points) && obstacle.points.length >= 3;
}

export function pointInPolygon(point, points) {
  let inside = false;
  for (let index = 0, previous = points.length - 1; index < points.length; previous = index++) {
    const a = points[index]; const b = points[previous];
    if ((a.y > point.y) !== (b.y > point.y) &&
        point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
  }
  return inside;
}

export function pointInObstacle(point, obstacle) {
  if (isPolygonObstacle(obstacle)) return pointInPolygon(point, obstacle.points);
  return point.x >= obstacle.x && point.x <= obstacle.x + obstacle.width &&
    point.y >= obstacle.y && point.y <= obstacle.y + obstacle.height;
}

export function geoPosition(scenario, point) {
  const map = scenario.map;
  if (!map || !Number.isFinite(map.centerLat) || !Number.isFinite(map.centerLng)) return null;
  const originX = scenario.world.widthM / 2;
  const originY = scenario.world.heightM / 2;
  const latitude = map.centerLat - (point.y - originY) / EARTH_RADIUS_M * 180 / Math.PI;
  const longitude = map.centerLng + (point.x - originX) /
    (EARTH_RADIUS_M * Math.cos(map.centerLat * Math.PI / 180)) * 180 / Math.PI;
  return { latitude, longitude };
}

export function haversineDistanceM(a, b) {
  const lat1 = a.latitude * Math.PI / 180; const lat2 = b.latitude * Math.PI / 180;
  const dLat = lat2 - lat1; const dLng = (b.longitude - a.longitude) * Math.PI / 180;
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLng / 2) ** 2;
  return 2 * EARTH_RADIUS_M * Math.asin(Math.min(1, Math.sqrt(h)));
}

export function exactDistanceM(scenario, a, b) {
  const geoA = geoPosition(scenario, a); const geoB = geoPosition(scenario, b);
  return geoA && geoB ? haversineDistanceM(geoA, geoB) : Math.hypot(a.x - b.x, a.y - b.y);
}

export function polygonCenter(points) {
  const total = points.reduce((sum, point) => ({ x: sum.x + point.x, y: sum.y + point.y }), { x: 0, y: 0 });
  return { x: total.x / points.length, y: total.y / points.length };
}
