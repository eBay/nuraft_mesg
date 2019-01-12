#!/usr/bin/env bash

set -eu
export ASAN_OPTIONS=detect_leaks=0
LOG_MODS="raft_core"
CLIENT_VERBOSITY=1
SERVER_VERBOSITY=1

CLIENT_CLI_OPTS="-csv ${CLIENT_VERBOSITY} --log_mods ${LOG_MODS}"
SERVER_CLI_OPTS="--flush_every 1 -sv ${SERVER_VERBOSITY} --log_mods ${LOG_MODS}"

SERVER_COUNT=4

SERVER_PIDS=()
echo """
[==========] Running 1 test from 1 test case.
[----------] Global test environment set-up.
[----------] 1 test from raft_test
[          ] Cleaning client"
./bin/raft_client ${CLIENT_CLI_OPTS} --clean
echo "[ RUN      ] Echo_Server.Members${SERVER_COUNT}"
for i in $(seq 0 $((${SERVER_COUNT} - 1)))
do
  echo "[          ] Starting server_${i}"
  ./bin/raft_server ${SERVER_CLI_OPTS} --server_id $i >/dev/null &
  SERVER_PIDS+=($!)
done

echo "[          ] Settling members"
sleep 3

for i in $(seq 1 $((${SERVER_COUNT} - 2)))
do
  echo "[          ] Adding Server $i"
  ./bin/raft_client ${CLIENT_CLI_OPTS} --add $i --server 0
  sleep 1
done

echo "[          ] Settling group"
sleep 3

echo "[          ] Writing Message"
./bin/raft_client ${CLIENT_CLI_OPTS} -m 'test::message' --server 1
echo "[          ] Adding Server $((${SERVER_COUNT} - 1))"
./bin/raft_client ${CLIENT_CLI_OPTS} --add $((${SERVER_COUNT} - 1)) --server 2
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
  if strings server_${i}_log | grep -q 'test::message'; then
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
