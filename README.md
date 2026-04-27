# nxe-json

JSON library for nginx modules (NginX Extension JSON Library),
distributed as a git submodule.

## Why this exists

nginx modules that consume JSON tend to grow near-duplicate wrappers
on top of jansson. nxe-json consolidates that work into a single
submodule with a uniform API, standardising on the safest variant of
each operation: DoS-limited parsing for untrusted input, fail-closed
numeric comparison, and binary-safe object lookups.

It wraps [jansson](https://github.com/akheron/jansson) behind an
opaque `nxe_json_t *` handle, so consumers do not have to include
`<jansson.h>` in their own headers.

## Features

- **Opaque handle** — `nxe_json_t` hides jansson types from consumers.
- **Two parse modes**:
  - `nxe_json_parse` — size and duplicate-key checks for JSON produced
    by components under the same trust boundary (e.g. a trusted session
    store).
  - `nxe_json_parse_untrusted` — additional DoS limits (depth 10,
    array size 100, string length 4096, object keys 256, total size
    1 MiB) for network-sourced inputs, including signed tokens — a
    valid signature proves authenticity, not structural safety.
- **Fail-closed numeric comparison** — `nxe_json_compare` rejects the
  lossy double path for two integer operands whose magnitude exceeds
  2^53, so authorisation decisions never silently collapse distinct
  values onto the same double.
- **Binary-safe construction** — `nxe_json_from_string` and
  `nxe_json_object_get_ns` use length-based jansson APIs, so embedded
  NUL bytes on the caller side are preserved. (Parse-time NUL bytes
  are rejected by jansson's default, which is intentional.)
- **nginx-friendly allocation model** — copied strings and stringify
  results are allocated from `ngx_pool_t`; JSON handles are
  jansson-owned and must be released with `nxe_json_free()`. The only
  runtime dependency is jansson.
- **Compact and pretty serialisation** — `nxe_json_stringify_compact`
  and `nxe_json_stringify_pretty` (wraps `JSON_INDENT`, clamped to
  `[1, 31]`).

## API overview

See [`src/nxe_json.h`](src/nxe_json.h) for full documentation. Quick
reference:

| Function | Purpose |
|---------|---------|
| `nxe_json_parse` / `_parse_untrusted` | Parse JSON bytes |
| `nxe_json_free` | Release the handle tree |
| `nxe_json_type` / `_is_*` | Type inspection |
| `nxe_json_object_get` / `_get_ns` / `_get_string` / `_get_integer` / `_get_boolean` | Object access |
| `nxe_json_object_size` / `_object_iter` / `_object_iter_next` / `_object_iter_key` / `_object_iter_value` | Object iteration |
| `nxe_json_array_size` / `_array_get` | Array access |
| `nxe_json_string` / `_integer` / `_real` / `_boolean` / `_number` | Scalar extraction |
| `nxe_json_from_string` | Construct a string value |
| `nxe_json_equal` / `_compare` | Comparison |
| `nxe_json_stringify_compact` / `_stringify_pretty` | Serialisation |

## Dependencies

- [jansson](https://github.com/akheron/jansson) — tested against 2.14.
  The binary-safe APIs (`json_object_getn`, `json_stringn`) require
  2.10 or newer.
- nginx core headers — types only (`ngx_pool_t`, `ngx_str_t`,
  `ngx_log_t`, ...). HTTP module headers are deliberately not used
  so nxe-json can be shared with non-HTTP (e.g. stream) modules.

## Integrating as a submodule

From a host nginx module:

```sh
git submodule add -b <branch> <url> nxe-json
```

In the host module's `config`:

```sh
nxe_json_dir="$ngx_addon_dir/nxe-json"

if [ ! -f "$nxe_json_dir/config.ngx" ]; then
    echo "$0: error: $nxe_json_dir/config.ngx not found" >&2
    exit 1
fi

. "$nxe_json_dir/config.ngx"

ngx_module_deps="... $nxe_json_module_deps"
ngx_module_incs="... $nxe_json_module_incs"
ngx_module_srcs="... $nxe_json_module_srcs"
ngx_module_libs="... $nxe_json_module_libs"
```

## Building and testing

nxe-json is not built standalone; it is compiled as part of the host
nginx module. The unit test suite uses `tests/ngx_compat/` —
malloc-backed stubs of the nginx core types — so no nginx source tree
is required:

```sh
cd tests
make test                           # default build + run
make test-asan                      # AddressSanitizer
make test-cov                       # gcov summary over src/
NXE_JSON_TEST_VERBOSE=1 make test   # show stub log output
```

## License

MIT. See [`LICENSE`](LICENSE).
