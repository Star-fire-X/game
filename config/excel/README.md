# Gameplay Excel Source

`config/excel/` is the single source of truth for gameplay tables.

Rules:
- One workbook per table.
- The workbook must contain exactly one sheet.
- The sheet name must exactly match the table name.
- Row 1 is the canonical header row.
- Column names must match the exporter registry exactly.
- Column order may change; lookup is by header name.
- IDs and foreign keys are positive integers.
- Booleans must use `TRUE/FALSE`, `true/false`, or `1/0`.
- Enums must use the canonical registry values.

Do not edit `config/runtime/` by hand. Generate it from these workbooks.
