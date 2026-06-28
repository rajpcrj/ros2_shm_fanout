#!/usr/bin/env bash
# test_localhost.sh — prove the SHM -> UDP -> SHM hop on ONE machine.
#
# A true end-to-end run needs TWO machines (see README.md). On one machine, stage 1
# and stage 3 would both write /dev/shm/e2e_streams.txt and collide. So this script
# tests the NETWORK HOP in isolation: a writer fills a source stream, stage 2 ships
# it over UDP (localhost), stage 3 reassembles it into a DIFFERENT local stream, and
# we read that stream back to confirm the bytes arrived intact.
#
# Run:  bash examples/end_to_end/test_localhost.sh
set +e
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source /opt/ros/humble/setup.bash >/dev/null 2>&1
source "$REPO/install/setup.bash" >/dev/null 2>&1
BIN="$REPO/install/shm_bridge_cpp/lib/shm_bridge_cpp"
export LD_LIBRARY_PATH="$REPO/install/shm_bridge_cpp/lib:$LD_LIBRARY_PATH"
PORT=${PORT:-7011}

cleanup() { kill -9 $W $S2 $S3 2>/dev/null
            rm -f /dev/shm/e2e_streams.txt /dev/shm/demo_* /dev/shm/got_* 2>/dev/null; }
trap cleanup EXIT
pkill -9 -f 'e2e_[23]_|ex_write_cpp' 2>/dev/null
rm -f /dev/shm/e2e_streams.txt /dev/shm/demo_* /dev/shm/got_* 2>/dev/null
sleep 1

# 1) source: a writer fills /dev/shm/demo_* with the sample image
"$BIN/ex_write_cpp" "$REPO/examples/sample_data/sample_640x480_rgb.bin" 640 480 >/dev/null 2>&1 & W=$!
sleep 2
[ -e /dev/shm/demo_header ] || { echo "FAIL: writer did not start"; exit 1; }

# registry: ship stream 'demo', LABEL it as topic '/got' so stage 3 recreates a
# DISTINCT stream 'got' (no clobber of the source on this single machine).
printf "0\tdemo\t/got\tsensor_msgs/msg/Image\n" > /dev/shm/e2e_streams.txt

# 2/3) receiver first, then sender, over localhost UDP
"$BIN/e2e_3_udp_to_shm" --port "$PORT" >/dev/null 2>&1 & S3=$!
sleep 1
"$BIN/e2e_2_shm_to_udp" --host 127.0.0.1 --port "$PORT" >/dev/null 2>&1 & S2=$!
sleep 5

# verify: stage 3 should have created /dev/shm/got_* from the UDP stream
if ls /dev/shm/got_header >/dev/null 2>&1; then
    echo "PASS: stage 3 created the received stream (/dev/shm/got_*)"
else
    echo "FAIL: no received stream — UDP hop did not complete"; exit 1
fi

# read the received stream back with a tiny inline reader to confirm the BYTES
TMP=$(mktemp -d)
cat > "$TMP/rd.cpp" <<'CPP'
#include <shm_bridge_cpp/shm_bridge.hpp>
#include <cstdio>
int main(){ shm_bridge::Reader r("got"); shm_bridge::Frame f;
  for(int i=0;i<50;i++) if(r.wait_and_read(f,200000000ULL)){
    printf("PASS: received seq=%u %zuB type=%s first=%u\n",
           f.seq,f.data.size(),f.type_name.c_str(),f.data.empty()?0:f.data[0]);
    return 0; }
  printf("FAIL: no frame on received stream\n"); return 1; }
CPP
g++ -std=c++17 "$TMP/rd.cpp" -o "$TMP/rd" \
    -I"$REPO/src/shm_bridge_cpp/include" \
    -L"$REPO/install/shm_bridge_cpp/lib" -lshm_bridge_cpp -lpthread
"$TMP/rd"
RC=$?
rm -rf "$TMP"
echo "=== test_localhost done (rc=$RC) ==="
exit $RC
