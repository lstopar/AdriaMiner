#!/bin/bash

AM_BUILD=build

rm lock
$AM_BUILD/aminer start -conf=$AM_BUILD/../config/adria_miner.conf
