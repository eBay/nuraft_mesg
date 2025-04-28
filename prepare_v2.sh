set -eu

echo -n "Exporting custom recipes..."
echo -n "nuraft."
conan export 3rd_party/nuraft --name nuraft --version 2.4.6 >/dev/null
echo "done."
