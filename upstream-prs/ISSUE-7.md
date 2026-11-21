# The function map's sha256 differs from the documented one, while the counts and LIFT_IR_OK match

Following the getting-started guide with the pinned inputs gives a different function map:

- Ghidra 12.1.3_PUBLIC and Temurin 21.0.12.1+1.
- The same driver and exporter script.
- The Mod Loader header at commit `611d686f`. The header's sha256 starts `93d19525`.

The resulting map hashes to `023dedf41a8bd6a9a299f0fefae3e95cd47f64d41b070b8f368ed5325eed6ca1`. The guide and the receipts have `11b82ae844467a97ff6d9e99faa827f6ab8f2411e942fb415344ed12c96e622e`.

Everything downstream agrees:

- the function count is 12,683, with the same regions and seed counts;
- `lift_ir_receipt.py` gives `LIFT_IR_OK` for all three groups (12,683, 5,711 and 5,646 modules).

The ordering is deterministic: the exporter uses sorted collections, and the driver sorts by address. Ghidra's log shows no analysis timeouts. The likeliest source is a set of per-function fields that Ghidra derives and nothing in `tools/` reads: `calling_convention`, `signature_source`, `ghidra_name_source` and `fid`. That is a guess until it can be compared with your map.

## Projection hashes

The rows are sorted by address. Each hash is the sha256 of `json.dumps(rows, separators=(",", ":"))`, where each row is the list of the named fields of one function:

| Fields | sha256 |
|---|---|
| address, size, region | `c71b6ad4286f5b6319178d6d44e7b8d6e1df4ab4293749c34abf1e824b70cbd4` |
| address, size, region, jump_tables, direct_calls, import_calls, data_refs, fs0_sites, thunk | `a31d80027428a56e7010be302d54bfc62580ea6d481593f8f6fcdef0817c58f2` |
| address, calling_convention, signature_source, ghidra_name, ghidra_name_source, fid | `7c18ea826fd83a3874873dedeff2159f177e872d26c08cc04db7bb9d1c5a2fca` |

The top-level fields, as the first 16 hex digits of the same hash of each field's value:

| Field | sha256 (first 16 digits) |
|---|---|
| `memory_map` | `168581dffab655cf` |
| `imports` | `e06f98b03355f796` |
| `entry_points` | `0563cb031980f9e0` |
| `string_clusters` | `5f2ceafc20a4fb2e` |
| `modloader_conflicts` | `eec0b17576ec148f` |

If your map gives the same first two row hashes, the difference is confined to fields the lift does not read. In that case it may help to pin a lift-facing projection hash next to the full one, or to say in the guide that `LIFT_IR_OK` is the authoritative check and the full-map hash may differ.
