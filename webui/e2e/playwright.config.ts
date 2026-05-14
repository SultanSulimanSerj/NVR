import { defineConfig, devices } from "@playwright/test";

export default defineConfig({
  testDir: ".",
  timeout: 30_000,
  expect: { timeout: 8_000 },
  reporter: process.env.CI ? [["github"], ["list"]] : "list",
  fullyParallel: true,
  retries: process.env.CI ? 2 : 0,

  use: {
    baseURL: process.env.NVR_E2E_BASE_URL || "http://localhost:8080",
    ignoreHTTPSErrors: true,
    headless: true,
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
    video: "retain-on-failure",
  },

  projects: [
    { name: "chromium", use: { ...devices["Desktop Chrome"] } },
    { name: "firefox",  use: { ...devices["Desktop Firefox"] } },
  ],
});
