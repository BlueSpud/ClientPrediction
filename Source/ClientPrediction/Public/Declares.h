#pragma once

static constexpr Chaos::FReal kFixedDt = 0.0133333333;
static constexpr uint32 kInvalidFrame = -1;

// Authority

static constexpr uint32 kBufferHealthTimeMs = 200;
static const uint32 kBufferHealthTicks = FMath::CeilToInt(static_cast<Chaos::FReal>(kBufferHealthTimeMs) / 1000.0 / kFixedDt);

// Auto proxy

static constexpr uint32 kInputWindowSize = 3;

// Sim proxy

static constexpr float kDesiredInterpolationBufferMs = 100.0;