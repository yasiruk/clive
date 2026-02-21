#!/bin/bash
set -e

echo "🧹 Cleaning old binaries..."
rm -f clive-cli signaling-server

echo "🔨 Building signaling server..."
go build -o signaling-server ./cmd/signaling

echo "🔨 Building CLI client..."
go build -o clive-cli ./cmd/cli

echo "✅ Build complete!"
echo ""
ls -lh clive-cli signaling-server
echo ""
echo "Run ./signaling-server to start the server"
echo "Run ./clive-cli for the client"
