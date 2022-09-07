rem TODO: Make this build the bin folder if it has to, just like you do in the bash version of the build script.
cl /DPLATFORM_WINDOWS /std:c++20 /Wall /O2 /MT main.cpp NetworkShepherd.cpp /link /out:nc.exe
