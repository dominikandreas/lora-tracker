import { test, expect } from '@playwright/test';

test.describe('BLE Mock Transport Integration', () => {
  test.beforeEach(async ({ page }) => {
    // Go to local server
    await page.goto('http://localhost:8080');
  });

  test('BleTransport gracefully handles lack of Web Bluetooth', async ({ page }) => {
    // Evaluate in page context
    const isSupported = await page.evaluate(async () => {
      const module = await import('./onboarding.js');
      const transport = new module.BleTransport(null); // mock unsupported adapter
      return transport.isSupported;
    });
    
    expect(isSupported).toBe(false);
    
    const errorMsg = await page.evaluate(async () => {
      try {
        const module = await import('./onboarding.js');
        const transport = new module.BleTransport(null);
        await transport.connect();
        return null;
      } catch (e) {
        return e.message;
      }
    });
    
    expect(errorMsg).toContain('Web Bluetooth is not supported');
  });

  test('BleTransport chunking and sequencing logic (mocked adapter)', async ({ page }) => {
    // Evaluate an isolated mock adapter
    const result = await page.evaluate(async () => {
      const module = await import('./onboarding.js');
      
      let writtenChunks = [];
      const mockTx = {
        addEventListener: () => {},
        removeEventListener: () => {},
        startNotifications: async () => {},
      };
      
      const mockRx = {
        writeValueWithResponse: async (data) => {
          writtenChunks.push(Array.from(data));
        }
      };

      const mockDevice = {
        addEventListener: () => {},
        removeEventListener: () => {},
        gatt: {
          connect: async () => ({
            getPrimaryService: async () => ({
              getCharacteristic: async (uuid) => {
                if (uuid === '6e400003-b5a3-f393-e0a9-e50e24dcca9e') return mockTx;
                return mockRx;
              }
            })
          }),
          connected: true,
          disconnect: () => {}
        }
      };

      const mockAdapter = {
        requestDevice: async () => mockDevice
      };

      const transport = new module.BleTransport(mockAdapter);
      await transport.connect();
      
      // Don't await because it expects a response, we just want to see what it writes
      transport.sendCommand('THIS IS A VERY LONG COMMAND THAT EXCEEDS EIGHTEEN BYTES').catch(() => {});
      
      // Wait for the async processQueue loop to run a bit
      await new Promise(r => setTimeout(r, 50));
      
      return writtenChunks;
    });

    // Expect the command to be chunked into <= 18 bytes
    // "THIS IS A VERY LON" (18 bytes)
    // "G COMMAND THAT EXC" (18 bytes)
    // "EEDS EIGHTEEN BYTE" (18 bytes)
    // "S\n" (2 bytes)
    expect(result.length).toBeGreaterThan(1);
    expect(result[0].length).toBeLessThanOrEqual(18);
    
    // Check that the last chunk ends with a newline (10)
    const lastChunk = result[result.length - 1];
    expect(lastChunk[lastChunk.length - 1]).toBe(10);
  });
});
