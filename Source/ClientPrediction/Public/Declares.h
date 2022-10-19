#pragma once

static constexpr Chaos::FReal kFixedDt = 0.0133333333;
static const uint32 kTicksPerSecond = FMath::CeilToInt(1.0 / kFixedDt);
static constexpr uint32 kInvalidFrame = -1;

// Authority

static constexpr uint32 kSyncFrames = 2;
static constexpr uint32 kAuthorityTargetInputBufferSize = 5;

// Auto proxy

static constexpr uint32 kInputWindowSize = 3;

// Sim proxy

static constexpr float kDesiredInterpolationBufferMs = 100.0;