# Clustered Forward Shading Demo with Vulkan

This demo is based on the [slides][slides-link] and [OpenGL code][ogl-code-link] by _Ola Olsson_. The following modifications are made comparing to the original implementation:
- light list generation using compute shaders
- simplified view\_z to grid\_z conversion
  `
  grid_z = log( ( - view_z - cam_near ) / ( cam_far - cam_near ) + 1.0 )
  `

## Notes

- The demo currently only runs on Windows platform.
- External dependencies such as _glm_, _gli_, and _assimp_ are set as git submodules.
- Makefile is generated using CMake.

## Controls

- orbit: arrow keys
- zoom: mousewheel
- pan: A/D/R/F
- forward/backward: W/S
- decrease/increase lights: NUM_9/NUM_0

---

Issues and pull requests are welcome!

<img src="./screenshots/1.png" width="100%" align="center">

<img src="./screenshots/2.png" width="100%" align="center">

<img src="./screenshots/3.png" width="100%" align="center">

[slides-link]: <http://efficientshading.com/2015/01/01/real-time-many-light-management-and-shadows-with-clustered-shading/>
[ogl-code-link]: <https://gitlab.com/efficient_shading/clustered_forward_demo>