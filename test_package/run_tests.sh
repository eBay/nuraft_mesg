#!/usr/bin/env bash

set -eu
export ASAN_OPTIONS=detect_leaks=0
SERVER_COUNT=4

SERVER_PIDS=()
echo """
[==========] Running 1 test from 1 test case.
[----------] Global test environment set-up.
[----------] 1 test from raft_test
[          ] Cleaning client"
./bin/raft_client -v 3 -c --clean
echo "[ RUN      ] Echo_Server.Members${SERVER_COUNT}"
for i in $(seq 0 $((${SERVER_COUNT} - 1)))
do
  echo "[          ] Starting server_${i}"
  ./bin/raft_server -v 1 --log_mods raft_core --server_id $i >/dev/null &
  SERVER_PIDS+=($!)
done

echo "[          ] Settling members"
sleep 3

for i in $(seq 1 $((${SERVER_COUNT} - 2)))
do
  echo "[          ] Adding Server $i"
  ./bin/raft_client -v 3 -c --add $i --server 0
done

echo "[          ] Settling group"
sleep 3

echo "[          ] Writing Message"
./bin/raft_client -v 3 -c -m 'test::message'
echo "[          ] Adding Server $((${SERVER_COUNT} - 1))"
./bin/raft_client -v 3 -c --add $((${SERVER_COUNT} - 1))
echo "[          ] Letting new member sync"
sleep 3
echo "[          ] Counting Server Processes"
alive_servers="$(ps -ef | grep -E '\./bin/raft_server' | grep -v grep | wc -l)"

for pid in "${SERVER_PIDS[@]}"
do
  kill -HUP ${pid} 2>&1 > /dev/null
done
wait 2>&1 > /dev/null

if test ${alive_servers} -ne ${SERVER_COUNT}
then
  echo "[   Failed ] Echo_Server.Members${SERVER_COUNT}"
  echo "[----------] 1 test from EchoServer"
  echo "[==========] 1 test from 1 test case ran."
  echo "[  FAILED  ] 1 test."
  exit -1
fi

echo "[          ] Checking Stores"
for i in $(seq 0 $((${SERVER_COUNT} - 1)))
do
  if strings store${i}/log0000_00000000 | grep -q 'test::message'; then
    echo "[       OK ]    Server:${i}"
  else
    echo "[   Failed ]    Server:${i}"
    echo "[----------] 1 test from EchoServer"
    echo "[==========] 1 test from 1 test case ran."
    echo "[  FAILED  ] 1 test."
    exit -1
  fi
done

echo "[       OK ] Echo_Server.Members${SERVER_COUNT}"
echo "[----------] 1 test from EchoServer"
echo "[==========] 1 test from 1 test case ran."
echo "[  PASSED  ] 1 test."
