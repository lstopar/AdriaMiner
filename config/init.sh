AM_BUILD=build

rm -f lock
rm db/*
rm db-temp/*

rm qm.log
rm qm-*.log
touch qm.log

$AM_BUILD/aminer create -conf=$AM_BUILD/../config/adria_miner.conf -def=$AM_BUILD/../config/vesna_table.def

cp -R db/* db-temp/
