libhaptics_pose: a library to get haption and geomagic position+rotation

calibrate.py: script to find the local displacement of endoscope, origin of haptic coordinate space and scale factor between haptic coordinates and base_skull.stl. This depends on libhaptics_pose.

To run on geomagic:
```
py .\calibrate.py calibrate --recording-file .\recording.json
```
but geomagic doesn't give good results, probably some issue with the hardware or our setup

To run on haption:
```
python3 calibrate.py --backend haption calibrate --recording-file recording.json
```

test_haptics: A hacky codebase that allows you to visualise the haptic device pointer and a vector at some fixed displacement(g_blueOffsetLocal in main.cpp) from the the pointer in its local coordinate frame. This can be used to test the results on python script by setting computed d value in `g_blueOffsetLocal` in main.cpp. The axes might be rotated.

Build caveats:
- Geomagic runs OOB if you have the drivers installed. I tested it on windows. Pass `-DHAVE_OPENHAPTICS` to cmake
- Haption requires you to install the haption drivers. Refer to Piyush's documentation where he details how to setup his codebase. there is a section dedicated to installing these drivers. Pass `-DENABLE_HAPTION` to cmake

you will need to change hardcoded paths in the cmake.
