-- Migration: 001_create_admin_schema (down)
DROP TABLE IF EXISTS admin.users;
DROP SCHEMA IF EXISTS admin;
