#!/bin/bash
cd "$(dirname "$0")"
./main $1  | xxd -o$1 -g8 -e | grep -E '4[0-9a-c]4[0-9a-c]4[0-9a-c]4[0-9a-c]4[0-9a-c]'
