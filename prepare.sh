set -eu

echo -n "Exporting custom recipes..."
echo -n "nuraft."
conan export 3rd_party/nuraft nuraft/2.4.4@
echo "done."
