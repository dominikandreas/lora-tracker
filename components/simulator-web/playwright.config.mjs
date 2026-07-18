import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './tests/browser',
  timeout: 30_000,
  use: { baseURL: 'http://127.0.0.1:4173' },
  webServer: {
    command: 'node scripts/serve.mjs 4173 app',
    port: 4173,
    reuseExistingServer: true,
  },
});
