#!/bin/bash
set -e

echo "🧹 Cleaning old binaries..."
rm -f clive-cli signaling-server clive-controller

echo "🔨 Building signaling server..."
go build -o signaling-server ./cmd/signaling

echo "🔨 Building CLI client..."
go build -o clive-cli ./cmd/cli

echo "🔨 Building Controller server..."
go build -o clive-controller ./cmd/controller

echo "✅ Build complete!"
echo ""
ls -lh clive-cli signaling-server clive-controller
echo ""
echo "Run ./clive-controller to start the control server"
echo "Run ./signaling-server to start the server directly"
echo "Run ./clive-cli for the client directly"
