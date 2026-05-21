A hacky codebase that allows you to visualise the haptic device pointer and a vector at some fixed displacement(g_blueOffsetLocal in main.cpp) from the the pointer in its local coordinate frame

- Geomagic runs OOB if you have the drivers installed. I tested it on windows. Pass `-DHAVE_OPENHAPTICS` to cmake
- Haption requires you to install the haption drivers. Refer to Piyush's documentation where he details how to setup his codebase. there is a section dedicated to installing these drivers. Pass `-DENABLE_HAPTION` to cmake

you will need to change hardcoded paths in the cmake.
