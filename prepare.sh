set -eu

echo -n "Exporting custom recipes..."
echo -n "nuraft."
conan export 3rd_party/nuraft nuraft/2.4.6@
echo "done."
