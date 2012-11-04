#!/bin/sh

mkdir -p build
git archive HEAD --format=zip --prefix=Deep\ Sleep.wdgt/ > build/deepsleep-1.3.zip
