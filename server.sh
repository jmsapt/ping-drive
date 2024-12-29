#!/bin/bash

sudo pkill nbdkit
sudo fuser -k 10809/tcp
sudo nbd-client -d /dev/nbd0

sudo pkill nbdkit
sudo fuser -k 10809/tcp
sudo nbd-client -d /dev/nbd0

sudo nbdkit -fvv ./bazel-bin/src/libping-nbd.so &
sleep 1
sudo nbd-client 127.0.0.1 10809 -block-size 1024 -Nx /dev/nbd0
wait
