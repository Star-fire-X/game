import { test, expect } from '@playwright/test'

test.describe('Service Status', () => {
  test.beforeEach(async ({ page }) => {
    // Mock authenticated state
    await page.addInitScript(() => {
      localStorage.setItem('token', 'mock-token')
    })
  })

  test('should display service list', async ({ page }) => {
    await page.goto('/services')
    await expect(page.locator('.service-card')).toBeVisible()
  })

  test('should show service status indicators', async ({ page }) => {
    await page.goto('/services')
    const statusBadge = page.locator('.el-tag')
    await expect(statusBadge.first()).toBeVisible()
  })
})
