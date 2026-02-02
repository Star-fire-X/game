import { test, expect } from '@playwright/test'

test.describe('Alert Management', () => {
  // PRD Story 3.1-3.3: Alert notification

  test('should display alert history', async ({ page }) => {
    await page.goto('/alerts/history')
    await expect(page.locator('table')).toBeVisible()
  })

  test('should show alert config for Owner', async ({ page }) => {
    await page.goto('/alerts/config')
    await expect(page.locator('form')).toBeVisible()
  })

  test('should have preset alert rules', async ({ page }) => {
    await page.goto('/alerts/config')
    // PRD: Preset rules
    await expect(page.locator('text=/service_down|cpu_high/i')).toBeVisible()
  })
})
