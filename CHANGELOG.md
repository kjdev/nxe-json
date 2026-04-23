# Changelog

## [d650274](../../commit/d650274) - 2026-04-24

### Fixed

- Zero-clear the output parameter on every non-OK exit of the scalar
  extractors (`nxe_json_string`, `_integer`, `_real`, `_boolean`,
  `_number`), the object convenience helpers (`_object_get_string`,
  `_object_get_integer`, `_object_get_boolean`), and
  `nxe_json_compare` (`*diff`)
  - Closes the silent-downgrade window raised by the security audit
    on `nxe_json_object_get_integer` / `_boolean`
    (tasks/issues/008.md): callers that forget to check the return
    value now read a deterministic zero (`0` / `0.0` / empty
    `ngx_str_t` / false) instead of whatever happened to be on their
    stack, so garbage can no longer propagate into authorization
    decisions
  - Out-param is still written only after the `NULL` guard on the
    output pointer, so the pre-existing `NGX_ERROR` on `value == NULL`
    is unchanged and no caller semantics break
- Document the `*value is only meaningful when NGX_OK is returned`
  contract explicitly in every extractor's header comment so future
  API additions follow the same convention

### Changed

- Unit test suite adds `extractor_zero_clears_on_failure` exercising
  every extractor with a seeded "poison" value to assert the
  zero-clear on type-mismatch, missing-key, and `NULL` handle paths

## [e52d4c1](../../commit/e52d4c1) - 2026-04-23

### Added

- Add `nxe_json_object_get_integer(json, key, *int64_t)` and
  `nxe_json_object_get_boolean(json, key, *ngx_flag_t)` convenience
  helpers
  - Mirror the existing `nxe_json_object_get_string` pattern so typed
    members can be fetched in one call instead of chaining
    `nxe_json_object_get` with `nxe_json_integer` /
    `nxe_json_boolean`
  - Unlike `_get_string`, neither helper takes a pool argument
    because the extracted value types do not require allocation
  - Return values follow the same tri-state convention as
    `_get_string`: `NGX_OK` on success, `NGX_DECLINED` if the key is
    missing or the value is of the wrong type, `NGX_ERROR` if the
    output pointer is NULL
  - Unit tests cover the happy path, missing keys, type mismatches
    (string / real vs integer, integer / string vs boolean), NULL
    out pointer, and NULL root

## [d182aea](../../commit/d182aea) - 2026-04-21

### Added

- Add `nxe_json_stringify_pretty(json, pool, indent)` API
  - Wraps jansson's `JSON_INDENT(n)` for admin-facing UIs where
    human readability of the serialised JSON matters
  - `indent` is clamped to `[1, 31]`; `0` is disallowed because it
    would still emit newlines (and thus be mistaken for
    `stringify_compact` while differing in behaviour)
  - Internal `nxe_json_stringify_flags` helper introduced so both
    `stringify_compact` and `stringify_pretty` share the same
    alloc / copy path; only the jansson flag and error tag differ
  - Unit tests cover newline / indent emission, round-trip parse,
    indent clamping at both ends, and NULL json / pool guards

## [a20c772](../../commit/a20c772) - 2026-04-21

### Added

- Initial jansson wrapper for submodule use
  - Opaque `nxe_json_t` hiding jansson's `json_t *` from consumers
  - Two parse entry points:
    - `nxe_json_parse` — trusted input, enforces `NXE_JSON_MAX_SIZE`
      (1 MiB) and `JSON_REJECT_DUPLICATES` only
    - `nxe_json_parse_untrusted` — additional DoS limits
      (`NXE_JSON_MAX_DEPTH=10`,
      `NXE_JSON_MAX_ARRAY_SIZE=100`,
      `NXE_JSON_MAX_STRING_LENGTH=4096`,
      `NXE_JSON_MAX_OBJECT_KEYS=256`)
  - Type inspection via `nxe_json_type` and static-inline
    `nxe_json_is_*` predicates
  - Object access:
    - `nxe_json_object_get` (C-string key)
    - `nxe_json_object_get_ns` (binary-safe `ngx_str_t` key via
      `json_object_getn`)
    - `nxe_json_object_get_string` (pool-allocated string convenience
      returning `NGX_OK` / `NGX_DECLINED` / `NGX_ERROR`)
  - Array access: `nxe_json_array_size`, `nxe_json_array_get`
  - Scalar extraction: `nxe_json_string` (binary-safe `ngx_str_t`
    output), `_integer`, `_real`, `_boolean`, `_number`
    (integer-or-real as `double`)
  - Value construction: `nxe_json_from_string` (binary-safe via
    `json_stringn`)
  - Deep equality `nxe_json_equal` and precision-aware
    `nxe_json_compare`; fail-closed on lossy `int64` → `double`
    conversion for two integer operands whose magnitude exceeds 2^53
  - Compact serialisation via `nxe_json_stringify_compact`
- Build artefacts for parent-module integration: `config.ngx`
  exposing `nxe_json_module_{deps,srcs,incs,libs}`
- Unit test suite covering every public API, including DoS-limit
  boundary conditions (oversize input, excessive depth, arrays,
  strings, key counts), binary-safe construction, and the fail-closed
  comparison semantics
- Malloc-backed `ngx_compat` stubs so the test suite runs without a
  real nginx build
