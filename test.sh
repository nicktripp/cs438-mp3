#!/bin/sh

sudo tc qdisc del dev eth0 root 2>/dev/null
sudo tc qdisc add dev eth0 root handle 1:0 netem delay 20ms loss 75%
sudo tc qdisc add dev eth0 parent 1:1 handle 10: tbf rate 100Mbit burst 40mb latency 25ms

make clean
make

mkdir -p test

./reliable_receiver 12345 test/dracula.txt & ./reliable_sender 127.0.0.1 12345 data/dracula.txt 10000 &

sleep 20
sudo tc qdisc del dev eth0 root 2>/dev/null
