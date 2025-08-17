#!/bin/bash

c++ -std=c++17 src/hello_world.mm src/SkiaDisplay.mm -o hello_world \
  -Iexternal \
  -Iexternal/skia \
  -Iexternal/yoga \
  -Iexternal/lexbor/source \
  external/yoga/build/yoga/libyogacore.a \
  external/skia/out/Static/libskia.a \
  external/lexbor/build/liblexbor_static.a \
  external/quickjs/libquickjs.a \
  -lpthread -lm \
  -framework Cocoa \
  -framework CoreFoundation \
  -framework CoreGraphics \
  -framework CoreText \
  -framework CoreServices \
  -framework ApplicationServices
