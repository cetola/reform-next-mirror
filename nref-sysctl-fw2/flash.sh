#!/bin/bash

sudo reform-mcu-tool bootsel next-sysctl-1.0
sleep 0.5
sudo picotool load -f build/sysctl.uf2
sleep 0.5
sudo picotool reboot

