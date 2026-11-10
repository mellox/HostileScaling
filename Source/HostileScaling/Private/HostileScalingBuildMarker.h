#pragma once

// Single source of the build marker. Narrow literal on purpose: it is pasted
// inside TEXT() by adjacent-literal concatenation, so the startup line in
// HostileScaling.cpp stays ONE string literal in the DLL, and McpDescribe /
// McpSnapshot report the identical text.
#define HOSTILESCALING_BUILD_MARKER "v1.1.2-mcp3 2026-11-10"
