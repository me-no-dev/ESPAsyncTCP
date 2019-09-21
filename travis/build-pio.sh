#!/bin/bash

echo -e "travis_fold:start:install_pio"

pip install -U platformio
if [ $? -ne 0 ]; then exit 1; fi

python -m platformio lib --storage-dir $PWD
if [ $? -ne 0 ]; then exit 1; fi

echo -e "travis_fold:end:install_pio"

echo -e "travis_fold:start:test_pio"
for EXAMPLE in $PWD/examples/*/*.ino; do
    python -m platformio ci $EXAMPLE -l '.' -b esp12e
	if [ $? -ne 0 ]; then exit 1; fi
done
echo -e "travis_fold:end:test_pio"
