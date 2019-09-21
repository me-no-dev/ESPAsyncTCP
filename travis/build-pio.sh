#!/bin/bash

echo -e "travis_fold:start:install_pio"
pip install -U platformio
echo -e "travis_fold:end:install_pio"

echo -e "travis_fold:start:test_pio"
for EXAMPLE in $PWD/examples/*/*.ino; do
    platformio ci $EXAMPLE -l '.' -b esp12e
done
echo -e "travis_fold:end:test_pio"
