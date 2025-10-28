set -eu

echo -n "Exporting custom recipes..."
echo -n "forestdb."
conan export 3rd_party/forestdb --name forestdb --version cci.20250315 >/dev/null
echo -n "jungle."
conan export 3rd_party/jungle --name jungle --version cci.20250316 >/dev/null
echo -n "nuraft."
conan export 3rd_party/nuraft --name nuraft --version 2.4.8 >/dev/null
echo "done."
