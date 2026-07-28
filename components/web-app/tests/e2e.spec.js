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

  test("Map grid is default layer", async ({ page }) => {
    const mapLayerValue = await page.$eval("#mapLayer", (el) => el.value);
    expect(mapLayerValue).toBe("none");
  });

  test("browser MQTT password persists across reloads", async ({ page }) => {
    await page.evaluate(() =>
      localStorage.setItem("lora-tracker.mqtt-password", "stored-test-secret"),
    );
    await page.reload();
    await expect(page.locator("#password")).toHaveValue("stored-test-secret");
    await page.evaluate(() => localStorage.removeItem("lora-tracker.mqtt-password"));
  });

  test("bundled map and PMTiles controls initialize", async ({ page }) => {
    await expect(page.locator("#map.leaflet-container")).toBeVisible();
    await expect(page.locator("#importPmtilesButton")).toBeEnabled();
  });

  test("device manager discovers unclaimed Bluetooth devices without a PIN or bond", async ({ page }) => {
    await page.locator("#onboardingButton").click();
    await expect(page.locator("#addBleDevice")).toHaveText("Claim nearby device");
    await expect(page.locator("#deviceCredentialRow")).toHaveCount(0);
    await expect(page.locator("#blePassword")).toHaveCount(0);
    await expect(page.locator("#replaceDeviceCredential")).toHaveCount(0);
    await expect(page.locator("#unpairedDeviceInventory")).toBeAttached();
    await expect(page.locator("#importDeviceQrButton")).toBeVisible();
  });


  test("Network Lab is bundled with the application", async ({
    page,
  }) => {
    await expect(page.getByRole("link", { name: /Open Network Lab/ })).toHaveAttribute(
      "href",
      "./lab/",
    );
    expect((await page.request.get("http://localhost:8080/lab/index.html")).ok()).toBe(true);
  });

  test("Enable Alerts button exists and updates state", async ({ page }) => {
    const alertsBtn = page.locator("#enableAlertsButton");
    await expect(alertsBtn).toBeVisible();
    await expect(alertsBtn).toHaveText("Enable Alerts");
    // Note: Can't easily test permission grant without browser context overriding,
    // but we can verify the button is there and has the correct initial state.
  });

  test("MQTT reconnect status retains the transport failure", async ({
    page,
  }) => {
    await page.evaluate(() => {
      class FailingWebSocket {
        static OPEN = 1;

        constructor() {
          this.readyState = 0;
          setTimeout(() => {
            this.onerror?.(new Event("error"));
            this.onclose?.({ code: 1006 });
          }, 0);
        }

        close() {}
        send() {}
      }
      window.WebSocket = FailingWebSocket;
    });
    await page.locator("#brokerUrl").fill("ws://192.168.1.2:9001/mqtt");
    await page.locator("#connectButton").click();
    await expect(page.locator("#connectionMessage")).toContainText(
      "WebSocket connection failed",
    );
    await expect(page.locator("#connectionMessage")).toContainText(
      "Retrying in 1 seconds",
    );
    await page.locator("#connectButton").click();
  });
});
