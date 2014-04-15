#!/bin/bash

AM_BUILD=/home/pi/adria/qminer/AdriaMiner/build

while true; do
	rm lock
	$AM_BUILD/aminer start -conf=$AM_BUILD/../config/adriaberry.conf
done
