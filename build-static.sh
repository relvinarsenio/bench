#!/bin/bash
# =============================================================================
# build-static.sh - Build fully static musl binary using Docker
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "🐳 Building static binary with Docker (Alpine/musl)..."
echo "=============================================="

# Create dist directory
mkdir -p dist

# Build using Docker (target builder stage only)
docker build --target builder -t bench-builder .

# Extract the binary
echo ""
echo "📦 Extracting binary..."
docker rm -f bench-extract 2>/dev/null || true
docker create --name bench-extract bench-builder /bin/true
docker cp bench-extract:/src/build/bench ./dist/bench
docker rm bench-extract

# Show results
echo ""
echo "✅ Build complete!"
echo "=============================================="
file ./dist/bench
ls -lh ./dist/bench
echo ""
echo "Binary location: ./dist/bench"
echo ""
echo "Test with: ./dist/bench"
