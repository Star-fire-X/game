import { test, expect } from '@playwright/test'

test.describe('Login Flow', () => {
  test('should show login page', async ({ page }) => {
    await page.goto('/login')
    await expect(page.locator('form')).toBeVisible()
  })

  test('should show error for invalid credentials', async ({ page }) => {
    await page.goto('/login')
    await page.fill('input[name="username"]', 'invalid')
    await page.fill('input[name="password"]', 'wrong')
    await page.click('button[type="submit"]')
    await expect(page.locator('.el-message--error')).toBeVisible()
  })

  test('should redirect to login when not authenticated', async ({ page }) => {
    await page.goto('/')
    await expect(page).toHaveURL(/\/login/)
  })

  // PRD Story 1.1: Account lockout after 5 failures
  test('should show lockout message after 5 failed attempts', async ({ page }) => {
    await page.goto('/login')
    for (let i = 0; i < 5; i++) {
      await page.fill('input[name="username"]', 'admin')
      await page.fill('input[name="password"]', 'wrongpassword')
      await page.click('button[type="submit"]')
      await page.waitForTimeout(500)
    }
    // After 5 failures, should show lockout message
    await expect(page.locator('text=/locked|15/i')).toBeVisible({ timeout: 5000 })
  })

  // PRD Story 1.2: 2FA verification step
  test('should show 2FA input after valid credentials', async ({ page }) => {
    await page.goto('/login')
    await page.fill('input[name="username"]', 'admin')
    await page.fill('input[name="password"]', 'correctpassword')
    await page.click('button[type="submit"]')
    // Should show TOTP input field
    await expect(page.locator('input[name="totp"]')).toBeVisible({ timeout: 5000 })
  })
})
