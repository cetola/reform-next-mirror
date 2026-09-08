#!/bin/bash

sudo reform-mcu-tool bootsel next-sysctl-1.0
sleep 1.0
sudo picotool load build/sysctl.uf2
sleep 0.5
sudo picotool reboot

