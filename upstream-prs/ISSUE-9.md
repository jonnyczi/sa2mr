<!-- Draft comment for open issue #51 (infinite animals and Chaos Drives), not a new issue. -->

Some leads for this tweak, from tracing a Chao taking items in Chao World (the 2012 Steam executable that OpenSA2 pins):

- **Hand-off.** A Chao's per-frame checks (`0x0053DB10` and its callees) look for its body being touched by the held item's "offer" sphere. Animals and Chaos Drives use collision kind `0x87`.
  - For that kind, `0x00566BC0` is the check, `0x00566C40` the reach, and `0x00566DF0` the absorb.
  - The take step calls `0x0046E5E0` to empty the player's hands. It then calls `0x0056CFB0` (ESI = Chao, ECX = item), which links the two garden entries and sets the item's held-by-Chao flag (the flags word at entry `+6`, value `2`).
- **Item loaders.** The Chaos Drive loader is `0x00545600` (kind 0–3, position, velocity, save slot), and the animal loader is `0x00548D30`.
  - The save-slot pointer passed to a loader is what keeps a dropped item in the save. With a null slot, the item is temporary.
  - Inventory animals are at `0x019F6B18`. The garden master `0x0052E920` spawns saved drives and fruit at load.
- **Effects.**
  - Absorbing applies one row of the stat table at `0x008A6240` (animals and drives) through `0x00535F00(2)`.
  - It also sets PowerRun (`0x00536180`) and FlySwim (`0x005361F0`).
  - An animal also writes its part bits and parts.
  - For a red Chaos Drive (kind 2), calling the absorb directly gave PowerRun +0.05 and points: power +24, luck +40, intelligence +20.
- **A possible shape:** wrap the absorb (`0x00566DF0`). After it returns, respawn the same kind at the player through the item's loader, with the same save slot or a null one. Alternatively, skip whatever the absorb does to remove the item, though I did not trace that part.

These are addresses and behaviour only, nothing more. The absorb's handling of the item task and its save slot is the part that still needs reading.
