#!/bin/bash
sudo nbd-client -d /dev/nbd0
sudo pkill nbdkit
sudo fuser -k 10809/tcp

sudo nbd-client -d /dev/nbd0
sudo pkill nbdkit
sudo fuser -k 10809/tcp
exit 0
