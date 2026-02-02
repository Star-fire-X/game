import { test, expect } from '@playwright/test'

test.describe('Audit Log', () => {
  // PRD Story 9.1-9.2: Audit logging

  test('should display audit log list', async ({ page }) => {
    await page.goto('/audit')
    await expect(page.locator('table')).toBeVisible()
  })

  test('should support filtering by operator', async ({ page }) => {
    await page.goto('/audit')
    await expect(page.locator('input[placeholder*="operator"]')).toBeVisible()
  })

  test('should support CSV export', async ({ page }) => {
    await page.goto('/audit')
    await expect(page.locator('button:has-text("Export")')).toBeVisible()
  })
})
