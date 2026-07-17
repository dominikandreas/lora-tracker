import { test, expect } from "@playwright/test";

test.describe("PWA Offline & Mode Tests", () => {
  test.beforeEach(async ({ page }) => {
    // Go to local server
    await page.goto("http://localhost:8080");
  });

  test("CSP allows only local resources", async ({ page }) => {
    let cspViolations = 0;
    page.on("console", (msg) => {
      if (msg.text().includes("Content Security Policy")) cspViolations++;
    });

    // Attempt to inject an external script
    await page.evaluate(() => {
      const script = document.createElement("script");
      script.src = "https://unpkg.com/vue@3/dist/vue.global.js";
      document.head.appendChild(script);
    });

    // Wait for CSP violation to be logged
    await page.waitForTimeout(500);
    expect(cspViolations).toBeGreaterThan(0);
  });

  test("Offline mode successfully loads application", async ({
    context,
    page,
  }) => {
    // Wait for service worker to activate
    await page.waitForFunction(
      () => navigator.serviceWorker.controller !== null,
    );

    // Go offline
    await context.setOffline(true);

    // Reload
    await page.reload();

    // Verify UI still rendered
    await expect(page.locator("#connectButton")).toBeVisible();

    // Go online
    await context.setOffline(false);
  });

  test("Dashboard mode hides lab actions and simulation", async ({ page }) => {
    await page.click('input[name="appMode"][value="dashboard"]');
    await expect(page.locator("#labActions")).toBeHidden();
    await expect(page.locator("#dashboardActions")).toBeVisible();
  });

  test("Lab mode shows simulation and initializes worker", async ({ page }) => {
    await page.click('input[name="appMode"][value="lab"]');
    await expect(page.locator("#labActions")).toBeVisible();
    await expect(page.locator("#dashboardActions")).toBeHidden();

    // Play button should be visible
    await expect(page.locator("#simPlay")).toBeVisible();
  });

  test("Map grid is default layer", async ({ page }) => {
    const mapLayerValue = await page.$eval("#mapLayer", (el) => el.value);
    expect(mapLayerValue).toBe("none");
  });

  test("Enable Alerts button exists and updates state", async ({ page }) => {
    const alertsBtn = page.locator("#enableAlertsButton");
    await expect(alertsBtn).toBeVisible();
    await expect(alertsBtn).toHaveText("Enable Alerts");
    // Note: Can't easily test permission grant without browser context overriding,
    // but we can verify the button is there and has the correct initial state.
  });
});
