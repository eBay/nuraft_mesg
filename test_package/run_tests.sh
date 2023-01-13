#!/usr/bin/env bash

set -eu
export ASAN_OPTIONS=detect_leaks=0
LOG_MODS="nuraft_mesg:2"
CLIENT_VERBOSITY=3
SERVER_VERBOSITY=1

CLIENT_CLI_OPTS="-csv ${CLIENT_VERBOSITY} --log_mods ${LOG_MODS}"
SERVER_CLI_OPTS="--flush_every 1 -sv ${SERVER_VERBOSITY} --log_mods ${LOG_MODS}"

GROUPS_COUNT=2
SERVER_COUNT=$((${GROUPS_COUNT} + 3))

SERVER_PIDS=()
echo """
[==========] Running 1 test from 1 test case.
[----------] Global test environment set-up.
[----------] 1 test from raft_test
[          ] Cleaning client"
./bin/raft_client ${CLIENT_CLI_OPTS} --clean
echo "[ RUN      ] Echo_Server.Members${SERVER_COUNT} Groups${GROUPS_COUNT}"
for s in $(seq 0 $((${SERVER_COUNT} - 1)))
do
  if test ${s} -lt ${GROUPS_COUNT}
  then
    echo "[          ] Starting server_${s} as Leader of Group ${s}"
    ./bin/raft_server ${SERVER_CLI_OPTS} --msg_metrics --server_id $s --create $s >/dev/null &
    SERVER_PIDS+=($!)
  else
    echo "[          ] Starting server_${s}"
    ./bin/raft_server ${SERVER_CLI_OPTS} --server_id $s >/dev/null &
    SERVER_PIDS+=($!)
  fi
done

echo "[          ] Settling servers"
sleep 2
echo "[       OK ]"

for g in $(seq 0 $((${GROUPS_COUNT} - 1)))
do
  for o in 1 2
  do
    echo "[          ] Adding Server $(($g + $o)) to Group ${g}"
    ./bin/raft_client ${CLIENT_CLI_OPTS} -g ${g} --add $(($g + $o)) --server 0
    sleep 2
  done
done

echo "[          ] Settling groups"

for g in $(seq 0 $((${GROUPS_COUNT} - 1)))
do
  echo "[          ] Writing Message to Group ${g}"
  ./bin/raft_client ${CLIENT_CLI_OPTS} -g ${g} -m "test::message${g}" --server 1
  echo "[       OK ]"
  echo "[          ] Adding Server $((${g} + 3)) to Group ${g}"
  ./bin/raft_client ${CLIENT_CLI_OPTS} -g ${g} --add $((${g} + 3)) --server 2
done
echo "[          ] Letting new members sync"
sleep 5
echo "[       OK ]"
for g in $(seq 0 $((${GROUPS_COUNT} - 1)))
do
  echo "[          ] Removing member from Group ${g}"
  ./bin/raft_client ${CLIENT_CLI_OPTS} -g ${g} --remove $((${g} + 2)) --server 2
done
echo "[       OK ]"
echo "[          ] Counting Server Processes"
alive_servers="$(ps -ef | grep -E '\./bin/raft_server' | grep -v grep | wc -l)"

for pid in "${SERVER_PIDS[@]}"
do
  kill -TERM ${pid} 2>&1 > /dev/null
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

for g in $(seq 0 $((${GROUPS_COUNT} - 1)))
do
  echo "[          ] Checking Group ${g} Stores"
  for s in $(seq ${g} $((${g} + 3)))
  do
    if strings logs/*/server_${s}_log | grep -q "test::message${g}"; then
      echo "[       OK ] Group:${g} Server:${s}"
    else
      echo "[   Failed ] Group:${g} Server:${s}"
      echo "[----------] 1 test from EchoServer"
      echo "[==========] 1 test from 1 test case ran."
      echo "[  FAILED  ] 1 test."
      exit -1
    fi
  done
done

echo "[       OK ] Echo_Server.Members${SERVER_COUNT}"
echo "[----------] 1 test from EchoServer"
echo "[==========] 1 test from 1 test case ran."
echo "[  PASSED  ] 1 test."
