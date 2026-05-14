import { test, expect } from "@playwright/test";

test("loads login screen", async ({ page }) => {
  await page.goto("/login");
  await expect(page.getByRole("button", { name: /sign in|войти/i })).toBeVisible();
});

test("rejects bad credentials", async ({ page }) => {
  await page.goto("/login");
  await page.locator("input").nth(1).fill("wrong-pass");
  await page.getByRole("button", { name: /sign in|войти/i }).click();
  await expect(page.locator("text=Invalid").or(page.locator("text=Неверные"))).toBeVisible();
});

test("login as admin, see dashboard", async ({ page }) => {
  const pw = process.env.NVR_E2E_ADMIN_PASSWORD;
  test.skip(!pw, "NVR_E2E_ADMIN_PASSWORD not set");
  await page.goto("/login");
  await page.locator("input").first().fill("admin");
  await page.locator("input").nth(1).fill(pw!);
  await page.getByRole("button", { name: /sign in|войти/i }).click();
  await expect(page.locator("nav")).toBeVisible();
});
