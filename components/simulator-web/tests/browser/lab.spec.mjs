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
