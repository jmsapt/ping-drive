#!/bin/bash

STEP=1

BINARY=/home/james/ping-fs/bazel-bin/src/ping
DIR=/home/james/ping-fs/ip

for i in $(seq $1 $STEP $2); do
    k=$(($i + $STEP - 1))
    echo pinging $i.0.0.0 to $k.0.0.0 ...
    for j in $(seq $i $k); do
        echo " -> $j.0.0.0"
        sudo bash -c "$BINARY $DIR/$j.txt $j.0.0.0 $((j + 1)).0.0.0" > /dev/null &
    done
    wait
done

