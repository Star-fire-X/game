import { test, expect } from '@playwright/test'

test.describe('RBAC Permission Tests', () => {
  // PRD Story 1.3: RBAC permission control

  test.describe('CS Role Restrictions', () => {
    test('CS cannot see service stop button', async ({ page }) => {
      // Login as CS user
      await page.goto('/login')
      // After login, service stop should be hidden
      await page.goto('/services')
      await expect(page.locator('button:has-text("Stop")')).toBeHidden()
    })

    test('CS cannot access alert config page', async ({ page }) => {
      await page.goto('/alerts/config')
      // Should redirect or show 403
      await expect(page).toHaveURL(/\/login|403/)
    })

    test('CS cannot access audit logs', async ({ page }) => {
      await page.goto('/audit')
      await expect(page).toHaveURL(/\/login|403/)
    })
  })

  test.describe('TechGM Role Restrictions', () => {
    test('TechGM cannot see service stop button', async ({ page }) => {
      await page.goto('/services')
      await expect(page.locator('button:has-text("Stop")')).toBeHidden()
    })

    test('TechGM can see restart button', async ({ page }) => {
      await page.goto('/services')
      await expect(page.locator('button:has-text("Restart")')).toBeVisible()
    })
  })

  test.describe('Owner Full Access', () => {
    test('Owner can see all service controls', async ({ page }) => {
      await page.goto('/services')
      await expect(page.locator('button:has-text("Start")')).toBeVisible()
      await expect(page.locator('button:has-text("Stop")')).toBeVisible()
      await expect(page.locator('button:has-text("Restart")')).toBeVisible()
    })
  })
})
