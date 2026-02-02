import { test, expect } from '@playwright/test'

test.describe('Player Query', () => {
  test.beforeEach(async ({ page }) => {
    await page.addInitScript(() => {
      localStorage.setItem('token', 'mock-token')
    })
  })

  test('should display online players list', async ({ page }) => {
    await page.goto('/players/online')
    await expect(page.locator('.el-table')).toBeVisible()
  })

  test('should navigate to player detail', async ({ page }) => {
    await page.goto('/players/1')
    await expect(page.locator('.player-detail')).toBeVisible()
  })
})
