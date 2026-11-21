#!/bin/bash
os=`uname -s`
if [[ $os = "Linux" ]]; then
	g++ -Wl,-rpath -Wl,'$ORIGIN' -L. -g -o demoexternaltrigger demoexternaltrigger.cpp -lamcam
else
	clang++ -Wl,-rpath -Wl,. -L. -g -o demoexternaltrigger demoexternaltrigger.cpp -lamcam
fi
