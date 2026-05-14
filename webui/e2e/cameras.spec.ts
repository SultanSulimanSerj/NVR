import { test, expect } from "@playwright/test";

// All authenticated specs share one logged-in storage state. The fixture
// `admin` performs the login once and reuses the cookie jar for subsequent
// tests in the file.
test.use({
  baseURL: process.env.NVR_E2E_BASE_URL || "http://localhost:8080",
});

async function loginAsAdmin(page: any) {
  const pw = process.env.NVR_E2E_ADMIN_PASSWORD;
  test.skip(!pw, "NVR_E2E_ADMIN_PASSWORD not set");
  await page.goto("/login");
  await page.locator("input").first().fill("admin");
  await page.locator("input").nth(1).fill(pw!);
  await page.getByRole("button", { name: /sign in|войти/i }).click();
  await expect(page.locator("nav")).toBeVisible();
}

test.describe("Cameras CRUD", () => {
  const id   = `e2e-${Date.now().toString(36)}`;
  const name = `e2e ${id}`;

  test("create / update / delete", async ({ page }) => {
    await loginAsAdmin(page);
    await page.goto("/cameras");
    await page.getByRole("button", { name: /добавить|add/i }).click();

    await page.locator('input[name="id"], input').nth(0).fill(id);
    await page.locator('input[name="name"], input').nth(1).fill(name);
    await page.locator('input').nth(2).fill("rtsp://demo.invalid/stream1");
    await page.getByRole("button", { name: /сохранить|save/i }).click();

    await expect(page.locator(`text=${id}`)).toBeVisible();

    await page.locator(`tr:has-text("${id}") button:has-text("edit"),
                        tr:has-text("${id}") button:has-text("ред")`).first().click();
    await page.getByRole("button", { name: /сохранить|save/i }).click();

    page.once("dialog", d => d.accept());
    await page.locator(`tr:has-text("${id}") button:has-text("delete"),
                        tr:has-text("${id}") button:has-text("удалить")`).first().click();
    await expect(page.locator(`text=${id}`)).toHaveCount(0);
  });
});

test.describe("RBAC", () => {
  test("viewer is denied operator-only routes", async ({ page }) => {
    const pw = process.env.NVR_E2E_VIEWER_PASSWORD;
    test.skip(!pw, "NVR_E2E_VIEWER_PASSWORD not set");
    await page.goto("/login");
    await page.locator("input").first().fill(process.env.NVR_E2E_VIEWER_LOGIN || "viewer");
    await page.locator("input").nth(1).fill(pw!);
    await page.getByRole("button", { name: /sign in|войти/i }).click();

    await page.goto("/cameras");
    await expect(page.locator("text=Недостаточно прав").or(page.locator("text=permission"))).toBeVisible();
  });
});

test.describe("Archive", () => {
  test("loads list", async ({ page }) => {
    await loginAsAdmin(page);
    await page.goto("/archive");
    await expect(page.locator("h1")).toContainText(/архив|archive/i);
  });
});

test.describe("Setup happy-path", () => {
  // Only run when an unconfigured test instance is reachable. Wraps the
  // setup_token via a CI secret; on normal CI we just skip.
  test.skip(!process.env.NVR_E2E_SETUP_TOKEN, "NVR_E2E_SETUP_TOKEN not set");

  test("walks through setup wizard", async ({ page }) => {
    await page.goto("/setup");
    await page.getByText(/принимаю|accept/i).click();
    await page.locator('input').fill(process.env.NVR_E2E_SETUP_TOKEN!);
    await page.getByText(/далее|next/i).click();
    // host
    await page.getByText(/далее|next/i).click();
    // admin
    await page.locator("input[type=password]").nth(0).fill("Adm1nPass!!");
    await page.locator("input[type=password]").nth(1).fill("Adm1nPass!!");
    await page.getByText(/далее|next/i).click();
    // archive
    await page.getByText(/далее|next/i).click();
    // scan -> done
    await page.getByText(/далее|next/i).click();
    await page.getByRole("button", { name: /завершить|finish/i }).click();
    await expect(page).toHaveURL(/\/$/);
  });
});
