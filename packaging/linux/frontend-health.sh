#!/bin/sh
set -eu

busctl --system call org.cecinjector.Mate /org/cecinjector/Mate \
  org.cecinjector.Mate1 GetStatus >/dev/null
echo "PASS D-Bus GetStatus"
busctl --system call org.cecinjector.Mate /org/cecinjector/Mate \
  org.cecinjector.Mate1 GetConfiguration >/dev/null
echo "PASS D-Bus GetConfiguration"
