#!/bin/bash
# Script to run comprehensive tests on DMDEVFS module using fs_tester from dmvfs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "================================================"
echo "  DMDEVFS Integration Test Runner"
echo "================================================"
echo ""

# Check if fs_tester path is provided
FS_TESTER_PATH="${1:-}"
DMDEVFS_MODULE="${2:-}"

if [ -z "$FS_TESTER_PATH" ] || [ -z "$DMDEVFS_MODULE" ]; then
    echo "Usage: $0 <path_to_fs_tester> <path_to_dmdevfs.dmf>"
    echo ""
    echo "Example:"
    echo "  $0 /path/to/dmvfs/build/tests/fs_tester /path/to/dmdevfs/build/dmf/dmdevfs.dmf"
    echo ""
    echo "To build fs_tester:"
    echo "  git clone https://github.com/choco-technologies/dmvfs.git"
    echo "  cd dmvfs && mkdir build && cd build"
    echo "  cmake .. -DDMVFS_BUILD_TESTS=ON"
    echo "  cmake --build ."
    exit 1
fi

# Check if files exist
if [ ! -f "$FS_TESTER_PATH" ]; then
    echo -e "${RED}ERROR: fs_tester not found at: $FS_TESTER_PATH${NC}"
    exit 1
fi

if [ ! -f "$DMDEVFS_MODULE" ]; then
    echo -e "${RED}ERROR: dmdevfs module not found at: $DMDEVFS_MODULE${NC}"
    exit 1
fi

echo -e "${GREEN}✓${NC} fs_tester found: $FS_TESTER_PATH"
echo -e "${GREEN}✓${NC} dmdevfs module found: $DMDEVFS_MODULE"
echo ""

# Make fs_tester executable
chmod +x "$FS_TESTER_PATH"

# Run tests in read-only mode
echo "Running fs_tester in read-only mode..."
echo "========================================"
echo ""

if "$FS_TESTER_PATH" --read-only-fs "$DMDEVFS_MODULE"; then
    echo ""
    echo -e "${GREEN}================================================${NC}"
    echo -e "${GREEN}  All tests PASSED!${NC}"
    echo -e "${GREEN}================================================${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}================================================${NC}"
    echo -e "${RED}  Some tests FAILED${NC}"
    echo -e "${RED}================================================${NC}"
    exit 1
fi
