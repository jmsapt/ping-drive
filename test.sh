#!/bin/bash

rm dump -f
sudo dd if=data of=/dev/nbd0 count=1
sleep 1
sudo dd  of=/dev/nbd0 of=dump count=1
