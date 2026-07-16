import { expect, test } from '@playwright/test';

test('loads the real WASM core and steps a deterministic scenario', async ({ page }) => {
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto('/');
  await expect(page.locator('#core-status')).toContainText('Firmware core WASM v1');
  await expect(page.locator('#map')).toBeVisible();
  await expect(page.locator('#clock')).toContainText('07:00:00');
  await page.locator('#step').click();
  await expect(page.locator('#clock')).toContainText('07:01:00');
  await expect(page.locator('#events .event')).not.toHaveCount(0);
  expect(errors).toEqual([]);
});

test('adds, places and configures a virtual tracker', async ({ page }) => {
  await page.goto('/');
  await expect(page.locator('#core-status')).toContainText('WASM v1');
  await page.getByRole('button', { name: '+ Tracker' }).click();
  const box = await page.locator('#map').boundingBox();
  await page.mouse.click(box.x + box.width * 0.48, box.y + box.height * 0.5);
  await expect(page.locator('#selection-title')).toHaveText('New tracker');
  const speed = page.locator('[data-path="speedKmh"]');
  await speed.fill('12.5');
  await speed.press('Enter');
  await expect(speed).toHaveValue('12.5');
  const power = page.locator('[data-path="radio.txPowerDbm"]');
  await power.fill('15');
  await power.press('Enter');
  await expect(page.locator('#toast')).toContainText('Germany radio profile');
  await expect(page.locator('[data-path="radio.txPowerDbm"]')).toHaveValue('14');
  await page.getByRole('button', { name: 'Waypoint' }).click();
  await page.mouse.click(box.x + box.width * 0.7, box.y + box.height * 0.65);
  await expect(page.locator('#events')).toContainText('Waypoint added');
});

test('models an MQTT outage and remains usable at mobile width', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto('/');
  await expect(page.locator('#core-status')).toContainText('WASM v1');
  await page.locator('#mqtt-online').uncheck();
  await page.locator('#speed').selectOption('1800');
  await page.locator('#play').click();
  await page.waitForTimeout(1300);
  await page.locator('#play').click();
  await expect(page.locator('#events')).toContainText('MQTT');
  await expect(page.locator('#map')).toBeVisible();
  const bodyWidth = await page.evaluate(() => document.body.scrollWidth);
  expect(bodyWidth).toBeLessThanOrEqual(390);
});

test('edits polygons, removes waypoints and accepts a manual satellite location', async ({ page }) => {
  await page.goto('/');
  await expect(page.locator('#core-status')).toContainText('WASM v1');
  await page.locator('#map-mode').selectOption('satellite');
  await page.locator('#map-latitude').fill('52.520008');
  await page.locator('#map-latitude').press('Enter');
  await expect(page.locator('#map-location')).toContainText('52.520008');
  const box = await page.locator('#map').boundingBox();
  // Select the forest, then use its top edge midpoint to insert a polygon point.
  await page.mouse.click(box.x + box.width * .5, box.y + box.height * .25);
  await expect(page.locator('#selection-title')).toHaveText('Mixed forest');
  await page.mouse.move(box.x + box.width * .505, box.y + box.height * (150 / 620));
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * .506, box.y + box.height * (151 / 620));
  await page.mouse.up();
  await expect(page.locator('#device-form')).toContainText('5 corners');
  // The tracker inspector exposes removal without allowing its final waypoint to disappear.
  await page.mouse.click(box.x + box.width * .12, box.y + box.height * .69);
  await expect(page.locator('#selection-title')).toHaveText('Pasture tracker');
  await page.locator('[data-remove-waypoint="1"]').click();
  await expect(page.locator('[data-remove-waypoint]')).toHaveCount(3);
});

test('shows local device state and timeline/archive viewers', async ({ page }) => {
  await page.goto('/');
  const box = await page.locator('#map').boundingBox();
  await page.mouse.click(box.x + box.width * .12, box.y + box.height * .69);
  await expect(page.locator('summary')).toHaveText('Local device state');
  await page.locator('summary').click();
  await expect(page.locator('.local-state pre')).toContainText('airtimeTokensMs');
  for (let index = 0; index < 10; index += 1) await page.locator('#step').click();
  await expect(page.locator('#track-summary')).toContainText('points');
  await expect(page.locator('#archive-gateway')).toHaveValue('gateway-1');
  await expect(page.locator('#archive-summary')).toContainText('committed');
});

test('pans, zooms and restores the map scenario after refresh', async ({ page }) => {
  await page.goto('/');
  await page.evaluate(() => localStorage.clear());
  await page.reload();
  await expect(page.locator('#core-status')).toContainText('WASM v1');
  const box = await page.locator('#map').boundingBox();
  await page.locator('#zoom-in').click();
  await page.getByRole('button', { name: 'Pan' }).click();
  await page.mouse.move(box.x + box.width * .5, box.y + box.height * .5);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * .6, box.y + box.height * .55);
  await page.mouse.up();
  const view = await page.evaluate(() => JSON.parse(localStorage.getItem('lora-tracker.network-lab.view.v1')));
  expect(view.zoom).toBeGreaterThan(1);
  expect(Math.abs(view.panX)).toBeGreaterThan(1);
  await page.locator('#map-latitude').fill('52.520008');
  await page.locator('#map-latitude').press('Enter');
  await page.waitForTimeout(350);
  await page.reload();
  await expect(page.locator('#map-latitude')).toHaveValue('52.520008');
});
