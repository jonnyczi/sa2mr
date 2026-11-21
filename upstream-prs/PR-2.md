# Take the first of a GPU the Vulkan loader lists twice

Branch: `d3d9gpu-identical-devices` (one commit on `b8cdfb1`)

Related to #36. That issue is the same name match finding no device. This change does not address it.

## Problem

`query_device_facts` (`host/d3d9gpu/d9g_shader_vulkan.cpp`) and `query_vulkan_device` (`host/d3d9gpu/d9g_gpu_sdl.cpp`) look for SDL_gpu's device by `deviceName` and need exactly one match.

When two ICD manifests load the same driver, the loader lists the GPU twice, with the same name, UUIDs and driver version. This happens when a container ships its own copy of Mesa beside the host's, for example an FHS or Steam-runtime environment, or a Flatpak, next to the host drivers. The facts query then finds "2 physical devices named …". The renderer refuses with "the shader translator's device facts could not be read", `Direct3DCreate9` returns NULL, and the player exits with status 3.

Setting `VK_DRIVER_FILES` to one manifest works around it.

## Change

- `same_device` compares two devices of one name by `deviceUUID` and `driverUUID` (`VkPhysicalDeviceIDProperties`, core in 1.1), vendor ID, device ID and driver version. A device UUID of zeros is never treated as the same device.
- In `query_device_facts`, later matches that are the same device as the first are not counted. The first is read, and `how` says so.
- `one_device_named` answers the same question for `query_vulkan_device`. That query runs on a 1.0 instance, so `one_device_named` uses the facts query's throwaway instance instead.
- Devices of one name that differ in any of these values are still refused, with the same message as before.
- `test_d3d9gpu`:
  - The stub loader fills `VkPhysicalDeviceIDProperties`.
  - `test_one_device_listed_twice` covers two copies (with a CPU device between them), which are accepted. Their facts are the same as one copy's.
  - It also covers cases that are still refused: another driver UUID, another device UUID, another driver version, zero UUIDs, and three devices of one name of which two are copies.

## Validation

- `TEST_D3D9GPU_OK 5455 checks`. With the change to `d9g_shader_vulkan.cpp` reverted, 4 of the new checks fail.
- `D3D9GPU_BUILD_OK`.
- On Linux x86-64, with an AMD integrated GPU on RADV (Mesa 26.1) inside an FHS container that adds its own RADV and llvmpipe manifests:
  - The loader lists the GPU twice, and `vulkaninfo` shows identical device and driver UUIDs for the two entries.
  - `sa2_player_gpu --clear-matrix --hidden` (offscreen video driver) from main exits 3 with the device-facts refusal. From this branch it exits 0.
  - The clear-matrix output from this branch is line-for-line the output from main with `VK_DRIVER_FILES` naming one manifest.
- `python3 tools/public_check.py`: 0 failures.

## Limits

- The game itself was not run with this branch. The clear matrix is the device-creation path the renderer takes, but no stage or menu was drawn with the duplicated manifests.
- Only the identical case is accepted. Two different drivers for one GPU are still refused. For example, RADV and AMDVLK, or two Mesa builds whose driver UUIDs differ. Choosing between them is left to `VK_DRIVER_FILES`.
