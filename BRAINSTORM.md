# SA2 MR — Brainstorm & Plan (v1, 2026-11-21)

Working title: **"Chao in Your Room"** (avoid SEGA branding in the app name)
Target: Valve Steam Frame (standalone), Quest 3 as dev proxy / secondary target.
Inspiration: lucas-martinic/mario64mr.

---

## 1. What the research established

### Steam Frame (what we can and can't use)
| Have | Don't have (documented) |
|---|---|
| Snapdragon 8 Gen 3, 16 GB, Vulkan, native Linux ARM64 / Android (Lepton) / Proton+FEX | Scene mesh, plane detection, room scan, spatial anchors |
| Full-gamepad controllers + capacitive finger curl + 6DoF | Hand tracking (not at launch) |
| Eye tracking (`XR_EXT_eye_gaze_interaction`) + foveation | Color passthrough (IR cameras → **monochrome**; color add-on unverified) |
| Optional **Arcturus Vision** full-color camera accessory (confirmed by user) — **mod must NOT require it**; treat as an enhancement tier | |
| Passthrough alpha-blend (confirmed for Lepton APKs via mario64mr, and for streamed PC apps in SteamOS 0.5.8 beta) | App access to the user's drawn chaperone polygon (STAGE bounds = rectangle at best) |
| IR passthrough works in the dark | |

mario64mr on the Frame: runs as a Lepton APK, floor-only (no room scan), grey passthrough, grip = "hand".
Install gotcha: use `lepton install_app dev <apk>`, not plain `adb install`.

### How mario64mr works (the pattern we copy)
- Unity host APK + the **whole game** (sm64ex decomp) compiled as a native `.so` with a swapped render/input/audio backend.
- Unity passes the camera/projection matrices in and renders the game's batched geometry in true stereo.
- Room triangles are fed into the game's **own** collision by swapping the static surface partition. Hands are dynamic surfaces.
- UX ladder: a cartridge, then a window/portal, then pulling Mario out onto your furniture, then a diorama.
- Legal model: no Nintendo data or code shipped. The user supplies a hash-verified ROM and it is built locally.

### Codebase options, ranked
| # | Base | Verdict |
|---|---|---|
| 1 | **OpenSA2** (kittycache9) — GPL static recomp of the 2012 Steam PC exe; D3D9 calls recorded into a stream and replayed via SDL3/Vulkan; runs on Android arm64 | **Primary.** The closest SA2 analogue to sm64ex. The recorded draw stream is the stereo insertion point, and X-Hax docs map its camera, collision and physics structs. Risk: prerelease (Metal Harbor and City Escape playable, bosses untested, Chao World only boots). |
| 2 | **Godot 4 host** + assets extracted from the user's files (SA Tools) + reimplemented Sonic-lite physics | **Prototype engine, and the fallback for shipping.** Full control over MR collision and the generator. Costs fidelity. Chao AI reference: sa2dc (Dreamcast decomp, Chao behaviours 100% matched; check its license). |
| 3 | **DolphinXR** + SA2B ISO | **Zero-code comfort/scale test only.** No physics access. |
| — | SA2B GC decomp (4.8%), sa2dc, dreamcastRecomp (no GPU yet), sa2-nx (Switch-only 360 recomp), SA2 Mod Loader (x86 DLLs), SA2 Redux (closed UE5) | References or watch list; not usable as a base |

No SA2/SADX VR or MR mod exists. This would be the first.

### What fans want (the parts that drive design)
- The Chao Garden is *the single most requested thing* Sonic Team gets (Iizuka). Fans want more interaction, petting, breeding control, races and karate, a portable Chao (VMU / Tiny Chao Garden), all gardens back, and Chao in stages.
- Speed: City Escape, rail grinding, Final Rush, rival duels, music, rank chasing. Crazy Gadget-style gravity stages are hated.
- Top complaint: the camera.
- VR lesson: first-person Sonic is "utterly nauseating". A third-person diorama view (Lucky's Tale, Astro Bot, Moss) is comfortable.

---

## 2. The critic's key insights

1. **SA2 is not SM64.** SM64 courses are small enclosed boxes, which is perfect for a diorama. SA2 action stages are kilometres long and linear, and the game culls and spawns by distance from a chase camera. Shrunk to a table, Sonic becomes a speck, and an outside camera sees half-loaded chunks.
   → **The Chao Garden is the SA2 space that has SM64's shape.** It is small, fully loaded and slow, so it should be the hero diorama.
2. **The Chao in your room shouldn't need the SA2 engine running.** Use a small host-side "room Chao brain" (modelled on the sa2dc behaviours) that reads and writes the **real Chao save data**. OpenSA2 renders the garden portal and the stages, and a Chao raised in your room is the same Chao in the garden.
3. **The Frame's monochrome passthrough is an art direction, not a bug.** A black-and-white room with SA2 in full color stands out.
   - **Baseline = monochrome.** Every feature must work and look intentional in grey passthrough.
   - **Enhancement tier: Arcturus Vision color camera** (optional accessory). When it's detected, use color passthrough, and optionally a "color bloom" effect where saturation spreads out from a happy Chao. Detect it at runtime and never require it.

---

## 3. The product (ranked feature set)

### Pillar A: Chao in Your Room (HEADLINE: fun 9, wow 10)
- **Pull a Chao out of the garden portal.** A glowing Chao Garden window on your wall: reach in with grip, pull a Chao out, set it on your floor. This is the mario64mr viral moment applied to the most-wanted thing in Sonic fandom.
- **Reverse portal:** drop or toss the Chao back into the garden. The room and the garden become one connected space.
- **User requirement (2026-11-21): pick up Chao and give them objects, just like the game's characters do.**
  - Carry, hold and throw Chao with the controllers.
  - Hand them fruit/nuts (feeding), small animals, Chaos Drives, hats/toys and eggs.
  - The Chao must react exactly as in SA2: eat, absorb, wear; stat, appearance and alignment changes. Ideally drive the game's own interaction code.
  - **Plan (user approved, 2026-11-21):** the hand interactions call the game's own functions. See `external/OpenSA2/docs/chao-internals.md`.
    - Release can be a gentle put-down (no unhappiness) or a throw, using the hand's velocity.
    - **Sonic must stay fully controllable** with the stick and buttons while the hands interact.
    - So the hand hold should not use Sonic's hold slot. Use it only as a fallback, at the cost of blocking his actions and showing a carry pose.
- **Backlog (user, 2026-11-21, after Chao interactions work):** grab the diorama with both grips to move, rotate and scale it.
- **First Frame garden test (2026-11-21):** it works, with passthrough. Issues found:
  - Steering should follow the viewer's head direction. **Fixed.**
  - The world shifts as the game camera moves. **Fixed.**
  - Objects pop in and out. **Fixed** (`vr-object-culling`).
  - Vibration, from 60 fps on a 72 Hz display. **Fixed** by setting the Frame to 120 Hz in SteamVR (60 Hz also works). SteamVR's refresh extension offers the app only 72 Hz.
- **Second Frame garden test (2026-11-21):** all of the above confirmed by the user. Remaining: the world shifts slightly when the head moves (xelee had this too: a stale pose prediction).
  - Fixed with a pose lead. The user says it's "a lot smaller now" and chose to move on.
  - The remaining trace is on repeated frames. Submitting depth would fix it; that's on the backlog.
- **Next (2026-11-21):** Chao hand interactions in the garden diorama: pick up, put down or throw, give items, pet.
  - **User feedback:** at the garden scale (0.0022 m/unit) Chao are too small to reach for. So the grip world move/rotate/scale moves out of the backlog and is built with the hand interactions.
  - A grip in empty space grabs the world; both grips scale it. A grip near an object grabs the object.
  - **User request (2026-11-21):** a picked-up Chao grows to hand scale, about twice as tall as the hand is wide (≈18 cm), and shrinks back on release. Built on the `SA2S_MARK_CHAO` draw marks, the first step toward Chao in Your Room.
    - The user extended this to **every held object** (fruit, drives, animals, hats, eggs, toys), using the same scale factor so relative sizes stay true.
    - Of the suggested garden improvements, the user picked **Chao in Your Room** as next.
    - **Built (2026-11-21), v1, verified offscreen; not yet on the Frame:**
      - Let go of a held Chao outside the garden and it lives on the real floor or a touched table, inside the play area. It can be picked up, petted and fed there, with a blob shadow and its bubble.
      - Bring it back to return it to the garden.
      - In the game it is parked in a dry lawn pen (configurable).
      - One room Chao at a time.
    - **Frame test 2 (2026-11-21):** the gloves sat on the wrong hands and bent sideways. A live tuning mode now lets the user fit them on the headset, and the result becomes the defaults.
      - Held objects are bigger (30 cm Chao).
      - The bubble sits further out.
      - Action stages are back to seated VR; the garden stays a diorama.
  - **Built (2026-11-21), verified under Monado with scripted hands; not yet on the Frame:**
    - World grab: one hand moves the world (it turned it too, until the Frame tune test); two hands move, turn and scale it, clamped to 0.0022-0.06 m/unit. The placement is kept between runs.
    - The default garden scale is now 0.004 m/unit: the island is about 1.8 m across and a Chao about 3 cm. The grab reach is a fixed 8 cm.
    - Pick up, gentle put-down (no unhappiness), throw (the game's -5), pet (+1), and giving fruit (eaten), drives and animals (absorbed) and hats (worn), all through the game's own code (`vr-chao-hands`).
    - Sonic keeps the pad the whole time.
  - **Frame test (2026-11-21):** all of the above works on the Frame. Problem: the user couldn't see their hands. So:
    - **Gloves:** after "horrendous" and "sausages", the user chose **Sonic's own hand meshes**, read at run time from their installed game files (never shipped). They show open or fist and glow when something is in reach. The procedural glove remains the fallback.
    - **Petting** uses the game's own stroke sphere, with animation and reward.
    - A held Chao faces the viewer.
    - Each hand holds its own object. A held Chao won't take an item (the game's rule).
  - **Frame tune test (2026-11-21):** the user tuned the gloves live (pitch -45, roll 60 on the in-game hand) and reported four issues. All four were addressed (verified offscreen, not yet on the Frame):
    - Held objects grew downward. They now sit on the palm and grow upward.
    - A one-hand grab turned the garden. One hand now only moves it; two hands still move, turn and scale.
    - Passthrough showed through in the action stages. Seated levels are now fully opaque.
    - The fist looked like the fingers "stretching". The gloves are now **Sonic's cutscene hands** (event e0000: 449 vertices a hand, with authored fist, relaxed and open poses blended along the squeeze), read from the user's install. They need a re-tune on the Frame.
  - **Backlog (user, 2026-11-21): Chao in Your Room is finicky, and a Chao can't be placed far away.** Improve it later; leave it as is for now.
  - **Gloves (Frame tune tests 2 and 3, 2026-11-21):** the cutscene hands needed their meshes swapped (0x2a79c is the right hand). After that, the user's final tune makes the gloves "good now": yaw 0, pitch −15, roll 15, held Chao 34 cm, bubble 0.60 m. These are now the defaults.
  - **Performance (user's "option 2", 2026-11-21):** single-pass stereo is built and is the default. Each world draw is issued once with two instances; the eye is picked in the shader. On an x86 replay of City Escape it cuts frame time by 37% in the heavy part, and the images match two-pass. The Frame measurement is next (it was 49–52 fps with level culling).
  - **Open:** the Chao stats panel as a floating panel near the Chao (user request). Not reproduced yet: holding a temporary Chao, by hand or by Sonic, with or without Y, shows no stats panel. Need to know when the user saw it.
- **Petting via the controller:** proximity, stroke speed, finger curl and haptics, mapped onto SA2's existing hidden happiness and Hero/Dark logic (gentle petting raises happiness; shaking or throwing pushes toward Dark).
- **Eye tracking:** the Chao notices when you look at it, waves and waddles over, and gets shy if you stare too long. This uses the Frame's unique sensor and is cheap.
- **Controller as perch:** the Chao sits on your hand and haptics convey its weight as you carry it around.
- **Lives here:** real-time clock. It sleeps in a cocoon on your shelf at night (still visible with IR passthrough in the dark). Fruit, grass and flower decals grow on your floor as happiness rises; this replaces the impossible "color bloom".
- **Furniture marking:** about 10 seconds per box (two trigger clicks plus drag to set height), because there's no scene mesh. Boxes double as collision, Chao landmarks and **occlusion masks**.

### Pillar B: Room Run (YOUR IDEA, reframed: fun 8, wow 9)

**User decisions (2026-11-21):**
- **Auto-generate is the default.** The player draws boundaries, and the game builds a level that fits. Manual building and editing (rail pen, piece swapping) remain *optional*, because most players won't want to hand-build.
- **Character scale: Sonic stands 4–6 in (10–15 cm) tall.** With SA2 Sonic about 10 units tall, that's roughly 1 unit ≈ 1–1.5 cm. Everything below should use this scale rather than the 5 cm option.
  - Consequence: a 3×4 m room's ~9 m perimeter is only ~600–900 SA2 units of track per lap, about 2–3 s at run speed.
  - Mitigations, in priority order: verticality (stacked layers, helix), figure-8 crossings, changing laps, slower set pieces (homing chains, enemy plazas), and scaling Sonic's speed down by about 0.6× at toy scale. The speed scale is a tunable to test for feel.

"Sonic laps around you through your living room." **Verdict: feasible, with guaranteed traversal, if it's built as a Hot Wheels kit, not free-form generation.**
- **Setup (about 60 s):** trace the floor boundary with the controller, mark furniture boxes, and the spot where you stand becomes a 0.7 m exclusion cylinder.
- **Shape:** an inset perimeter, figure-8, or helix around you, as a spline. A vertical profile passes over couch and table tops. The track floats at **0.4–1.4 m** ("Hot Wheels in the air"), so you're not staring at your feet.
- **Fill:** about 15 hand-authored modules. Each module has a *certified* entry/exit speed and heading envelope, and modules only connect when their envelopes fit.
  - **Controlled-motion modules** (springs, dash panels, rails, loops, homing chains) take over control and reset Sonic's state. That's SA2's own design language, and it's what makes guarantees possible.
  - **Free jumps** use conservative arcs (60% of computed range) with a catch net or lower track below.
- **Validate:** a bot runs the real physics about 32 times with perturbations and a "bad player" profile. A repair loop widens track, adds a dash panel, or swaps a gap for a spring. The all-safe-modules fallback is valid by construction, so **it can never fail to output a course**.
- **Scale reality:** at toy scale (the user picked Sonic at 10–15 cm, 1 SA2 unit ≈ 1–1.5 cm), a 3×4 m room perimeter takes about 3–4 s. So: 30–60 s mission-style runs, laps that change (lap 2 opens the upper skill route, lap 3 adds enemies), and verticality. (The 5 cm Sonic option was dropped: 10–15 cm is the target.)
- **Controls:** track-relative steering on the critical path (stick-up = forward along the spline), because "camera-relative" makes no sense when your head is the camera.
- **Rank:** the expert bot's time sets the A-rank and the average bot's time × 1.3 sets C, so thresholds fit every generated room automatically.
- **Grind-rail pen:** draw a rail from the bookshelf to the couch with the controller. It snaps to the track, shows green or red instantly via the bot, and the generator adds supports. This is the level editor fans wished for, kept scoped.
- **Reuse:** the same generator with a slow-module library (water, climb, fruit) builds **Chao Race tracks around your room**. Cheering raises speed (mic volume).
- **Ghosts:** stored as per-frame transforms, not inputs, because determinism isn't guaranteed. A-rank emblems land as trophies on your real shelf.
- **Sharing:** course DNA re-fits to a friend's room; canonical S/M/L rooms allow leaderboards.

### Pillar C: The loop that ties A and B together
- **Chaos Drives** from Room Run (and real stages) pop out onto your real floor for your Chao to eat. Speed play and pet care feed each other.
- **Wrist VMU:** a virtual Chao Adventure–style menu and status screen on your wrist.

### Pillar D: Classic SA2 stages in VR (supporting)
- **Portal window** on the wall: play the real stages through it, and lean in to be inside.
- **Moving diorama:** a table-sized slice of the stage that scrolls with Sonic, the only honest tabletop for linear stages.
- **Comfort ladder:** diorama (default), then chase cam, then opt-in first-person (vignette, fixed horizon, no camera roll, grid cage). Marketing line: "the camera complaint is solved — *you* are the camera."
- **User roadmap (2026-11-21), in order:**
  1. Quick fixes:
     - bake in the tuned gloves;
     - held objects grow upward;
     - one-hand garden grab without rotation;
     - opaque stage VR (no passthrough);
     - better fist closing.
  2. **VR performance: single-pass stereo** (now in progress).
  3. **2D mode:** a floating game window, moved and resized with the grips, toggled with VR mode from the **pause menu**. Reach into the window and pull Sonic out into the room (mario64mr-style), in his falling animation while held.
     - **User decision:** once dropped, Sonic **runs around the room under the player's control**, like Mario in mario64mr, not frozen.
     - This needs proxy room collision in the game (`DynCol_Add` 0x0047D6B0, untested), which is shared groundwork with Room Run.
     - Hooks are in `external/OpenSA2/docs/vr-game-hooks.md`.
  4. **First-person VR mode**, where looking down shows Sonic's body and feet, for the sense of speed.
  5. Then **Room Run**: generate a level in the player's room.
  - Chao in Your Room v1 is finicky, and Chao can't be placed far. Improve later.

### Set piece for the clip
- **GUN truck:** the truck's nose smashes through a wall portal and chases toy Sonic along the Room Run track, then he escapes back into the portal. This is a 10–20 s authored scene, not simulated City Escape.

### Parked or cut for v1
- Crazy Gadget gravity cube: hated stage, expensive hacks.
- Sonic vs Shadow physical-dodge duel: bosses untested, scale makes no sense, comfort risk. Later: a Shadow ghost race on Room Run.
- Online shared gardens: netcode, moderation. Cheap stand-in: async Chao trading via save files.
- Full-VR Chao-scale garden: maybe a teleport-only toggle later.
- Live & Learn finale: replace with a static pose/photo mode, and watch music DMCA in clips.
- Chao rides on Sonic's head into stages: v2.

---

## 4. The 30-second WOW clip (what we build toward)
1. **0–8 s:** a colorful Chao Garden portal glows on the wall of a black-and-white room. Reach in, pull out a Chao. It drops onto the real floor, waddles, and looks up at you because you're looking at it.
2. **8–15 s:** pet it, hearts appear, and fruit and grass sprout around it.
3. **15–25 s:** draw a rail from the couch to the shelf. The track snaps around the room, Sonic bursts from the portal, grinds the couch rail and laps around you.
4. **25–30 s:** Sonic drops a Chaos Drive, the Chao eats it, and an A-rank emblem lands on your shelf.

---

## 5. Roadmap

### Deployment path (from the sibling projects ../xelee, ../mario64mr, ../VivecraftMod-openxr)
- **Primary: a native Linux ARM64 tarball, following xelee's approach.**
  - Build inside the Steam Linux Runtime 3 (sniper) arm64 SDK container. Use QEMU binfmt on NixOS (`boot.binfmt.emulatedSystems = ["aarch64-linux"]`) or an arm64 CI runner.
  - Bundle the libraries, keep glibc symbols at 2.31 or older, and ship `lib/openxr/libopenxr_loader.so.1`.
  - Deploy with a clone of `xelee/tools/frame_deploy.sh`: rsync to `~/Applications`, launch with Steam's environment, add a Steam shortcut.
- **OpenXR:**
  - Graphics: `XR_KHR_vulkan_enable2`, and `tu_emulate_second_queue=true` because Turnip exposes a single queue.
  - Passthrough: alpha blend with a source-alpha projection layer, clear to alpha 0. The game layer goes first (SteamVR reprojects only layer 0), with the HUD as a quad above it.
  - Controllers: `XR_VALVE_frame_controller_interaction` with aurora's binding-variant fallback (`xr_policy.cpp`).
  - Refresh rate: the runtime picks it, so tell users to fix the Frame at 120 Hz for the 60 fps game.
- **Stereo:** replay the OpenSA2 draw stream once per eye with the eye pose times an anchor transform. Interpolate 60 fps up to the display rate the way mario64mr does (render between the previous and current frame).
- **Headset-free testing:** Monado plus aurora's `xr_sim_layer`, which mimics the Frame.
- **User decision: no APK/Lepton route on the Steam Frame.** The Frame build is native Linux ARM64 only.
- **Quest is a later target, once the Frame version works.** Quest requires an APK: a thin Android wrapper (OpenSA2 `host/android` plus the Khronos Android OpenXR loader). It adds optional Meta extras: scene mesh, hand tracking, color passthrough.
  - To keep that cheap, the OpenXR host must stay portable: pure Vulkan plus core OpenXR. Frame-specific bits (the `XR_VALVE_frame_controller_interaction` profile, the Turnip queue workaround) sit behind runtime capability checks.

### Phase 0: Prove the engine (2 weeks). Gate: if this fails, Godot becomes the primary engine.
1. Fork OpenSA2. Replay its recorded D3D9 stream twice with OpenXR per-eye view and projection, with a head-tracked camera override, **in the Chao Garden** (fully loaded, which avoids the culling problem). Anchor it as a diorama on the floor. Run on the Frame via a Lepton APK (the proven path) or a native Linux ARM64 build.
2. A crude host-side Chao (real model, wander, hop) walks out of the diorama onto floor-plane collision.
3. Grip to pick up and put down, plus one petting reaction with haptics.

**In parallel (Godot, desktop):** the Room Run generator prototype.
- Week 1: 2D room editor, offset and medial axis, skeleton spline, 6 modules.
- Week 2: Sonic-lite controller with SA2-like constants, path-locked rail and loop, the bot, validation and repair.
- **Batch harness over 1,000 random rooms.** Target: 100% completable after repair, more than 90% without fallback.

### Phase 1: Chao in Your Room v0.1 (about month 2)
- Furniture marking, room Chao brain on real save data, eye-gaze reactions, sleep/clock, decals, reverse portal.

### Phase 2: Room Run in MR (about month 3)
- The generator on the Frame with passthrough, track-relative steering, and a comfort/fun test at 10 cm vs 15 cm Sonic, and speed-scale tuning.
- Then the OpenSA2 LandTable/SET writer, to run on **real SA2 physics**: re-certify the module envelopes through headless OpenSA2.
- Chaos Drive loop, ghosts, trophies.

### Phase 3: Polish and clip
- Grind-rail pen, Chao Race reuse, GUN truck set piece, portal/moving-diorama stages, comfort ladder, wrist VMU.

---

## 6. Top risks
1. **OpenSA2 maturity:** prerelease. Stereo plus camera override has to survive the game's culling; Chao World only boots; runtime LandTable/SET injection and loop/rail path data are unknowns.
   *Mitigation:* Chao Garden first, room Chao in host code, Godot fallback gate at the end of Phase 0.
2. **Frame platform gaps:** no mesh or anchors (placement drifts, so re-anchor to the floor plus a user origin plus marked boxes). Passthrough is proven on both paths: Lepton APKs (mario64mr) and native Linux ARM64 via `XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND` (the user's xelee and VivecraftMod-openxr projects). Develop against standard OpenXR, with Quest 3 as a proxy (it adds scene mesh, hands and color as optional extras).
3. **Legal:** ship zero SEGA code or data. The user supplies their own Steam SA2 (hash-verified) and the build/extraction happens locally (mario64mr model). Keep it free and GPL (inherited from OpenSA2), with no SEGA branding in the app name; watch music in promo clips. Don't paste SA2 Mod Loader headers (no-commercial license) into a GPL tree; re-derive the offsets.

---

## 7. Open questions for you
- Do you want to get **Chao in Your Room** (the critic's pick) proven first, or the **Room Run** generator (your idea)? They can run in parallel, as above.
- Lepton APK (proven, as mario64mr does) vs native Linux ARM64?
- Should Quest 3 be a first-class second target (richer MR: scene mesh, hands, color)?
- Do you own the 2012 Steam SA2 (needed for OpenSA2)? And the SA2B ISO (for a quick DolphinXR comfort test)?
